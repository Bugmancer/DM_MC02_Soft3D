#include "soft3d_lcd.h"
#include "main.h"
#include "spi.h"
#include "cmsis_os2.h"

#include <stddef.h>

#define LCD_FLAG_DONE (1UL << 0)
#define LCD_FLAG_ERROR (1UL << 1)
#define LCD_FLAGS (LCD_FLAG_DONE | LCD_FLAG_ERROR)
#define LCD_COMMAND_TIMEOUT_MS 50U
#define LCD_X_OFFSET 20U

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[14];
} LCD_InitStep;

static const LCD_InitStep s_init_steps[] = {
    {0x36U, 1U, {0x70U}},
    {0x3AU, 1U, {0x05U}},
    {0xB2U, 5U, {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U}},
    {0xB7U, 1U, {0x35U}},
    {0xBBU, 1U, {0x32U}},
    {0xC2U, 1U, {0x01U}},
    {0xC3U, 1U, {0x15U}},
    {0xC4U, 1U, {0x20U}},
    {0xC6U, 1U, {0x0FU}},
    {0xD0U, 2U, {0xA4U, 0xA1U}},
    {0xE0U, 14U, {0xD0U, 0x08U, 0x0EU, 0x09U, 0x09U, 0x05U, 0x31U,
                  0x33U, 0x48U, 0x17U, 0x14U, 0x15U, 0x31U, 0x34U}},
    {0xE1U, 14U, {0xD0U, 0x08U, 0x0EU, 0x09U, 0x09U, 0x15U, 0x31U,
                  0x33U, 0x48U, 0x17U, 0x14U, 0x15U, 0x31U, 0x34U}},
    {0x21U, 0U, {0U}},
    {0x29U, 0U, {0U}}
};

static osThreadId_t s_owner;
static bool s_initialized;
static volatile bool s_inflight;
static volatile uint32_t s_completion;
static bool s_dma_disabled;
static Soft3D_LCDError s_error;

static uint32_t lcd_ticks(uint32_t milliseconds)
{
    uint64_t ticks;
    if (milliseconds == osWaitForever) {
        return osWaitForever;
    }
    ticks = ((uint64_t)milliseconds * osKernelGetTickFreq() + 999U) / 1000U;
    return ticks >= osWaitForever ? osWaitForever - 1U : (uint32_t)ticks;
}

static void lcd_cs(GPIO_PinState state)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, state);
}

void Soft3D_LCD_Backlight(bool on)
{
    HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

Soft3D_LCDError Soft3D_LCD_LastError(void)
{
    return s_error;
}

static bool lcd_reject(Soft3D_LCDError error)
{
    s_error = error;
    return false;
}

static void lcd_stop(void)
{
    /* Suppress late IRQs while the synchronous abort retires the DMA buffer. */
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
    (void)HAL_SPI_Abort(&hspi1);
    if (hspi1.hdmatx != NULL && hspi1.hdmatx->Instance != NULL) {
        (void)HAL_DMA_Abort(hspi1.hdmatx);
        if ((((DMA_Stream_TypeDef *)hspi1.hdmatx->Instance)->CR & DMA_SxCR_EN) != 0U) {
            /* DMA1 is dedicated to this LCD in this project. Retire a stuck
             * stream before buffer reuse, then retain polling-only operation. */
            __HAL_RCC_DMA1_FORCE_RESET();
            __DSB();
            __HAL_RCC_DMA1_RELEASE_RESET();
            s_dma_disabled = true;
            s_error = SOFT3D_LCD_ERROR_ABORT;
        }
    }
    __HAL_SPI_DISABLE(&hspi1);
    lcd_cs(GPIO_PIN_SET);
    s_inflight = false;
    s_completion = 0U;
    HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_ClearPendingIRQ(SPI1_IRQn);
    if (s_owner != NULL) (void)osThreadFlagsClear(LCD_FLAGS);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
}

static bool lcd_command(uint8_t command, const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef result;
    uint16_t i;
    lcd_cs(GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&hspi1, &command, 1U, LCD_COMMAND_TIMEOUT_MS);
    lcd_cs(GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    /* Match the known-good panel driver: each command/parameter is a CS packet. */
    for (i = 0U; result == HAL_OK && i < length; ++i) {
        lcd_cs(GPIO_PIN_RESET);
        result = HAL_SPI_Transmit(&hspi1, (uint8_t *)&data[i], 1U, LCD_COMMAND_TIMEOUT_MS);
        lcd_cs(GPIO_PIN_SET);
    }
    lcd_cs(GPIO_PIN_SET);
    if (result != HAL_OK) {
        s_error = SOFT3D_LCD_ERROR_COMMAND;
        lcd_stop();
        return false;
    }
    return true;
}

static void lcd_delay(uint32_t milliseconds, bool rtos)
{
    if (rtos) {
        (void)osDelay(lcd_ticks(milliseconds) + 1U);
    } else {
        HAL_Delay(milliseconds);
    }
}

static bool lcd_initialize(bool rtos)
{
    size_t i;
    if (s_initialized) return true;
    s_completion = 0U;
    lcd_cs(GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_RESET);
    lcd_delay(100U, rtos);
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET);
    lcd_delay(100U, rtos);
    Soft3D_LCD_Backlight(true);
    lcd_delay(100U, rtos);
    if (!lcd_command(0x11U, NULL, 0U)) {
        return false;
    }
    lcd_delay(120U, rtos);
    for (i = 0U; i < sizeof(s_init_steps) / sizeof(s_init_steps[0]); ++i) {
        if (!lcd_command(s_init_steps[i].command, s_init_steps[i].data,
                         s_init_steps[i].length)) {
            return false;
        }
    }
    s_initialized = true;
    return true;
}

bool Soft3D_LCD_InitBlocking(void)
{
    if (s_owner != NULL) return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    if (s_inflight) return lcd_reject(SOFT3D_LCD_ERROR_BUSY);
    return lcd_initialize(false);
}

bool Soft3D_LCD_Attach(void)
{
    osThreadId_t caller = osThreadGetId();
    if (caller == NULL || (s_owner != NULL && s_owner != caller)) {
        return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    }
    if (!s_initialized) return lcd_reject(SOFT3D_LCD_ERROR_INITIALIZATION);
    if (s_inflight) return lcd_reject(SOFT3D_LCD_ERROR_BUSY);
    s_owner = caller;
    (void)osThreadFlagsClear(LCD_FLAGS);
    return true;
}

bool Soft3D_LCD_Init(void)
{
    osThreadId_t caller = osThreadGetId();
    if (caller == NULL || osKernelGetTickFreq() == 0U ||
        (s_owner != NULL && s_owner != caller)) {
        return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    }
    if (s_inflight) return lcd_reject(SOFT3D_LCD_ERROR_BUSY);
    if (!lcd_initialize(true)) return false;
    return Soft3D_LCD_Attach();
}

bool Soft3D_LCD_Wait(uint32_t timeout_ms)
{
    if (s_owner == NULL || s_owner != osThreadGetId()) {
        return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    }
    if (!s_initialized) return lcd_reject(SOFT3D_LCD_ERROR_INITIALIZATION);
    if (!s_inflight) {
        return true;
    }
    if (s_completion == 0U) {
        (void)osThreadFlagsWait(LCD_FLAGS, osFlagsWaitAny, lcd_ticks(timeout_ms));
    }
    if (s_completion != LCD_FLAG_DONE) {
        s_error = s_completion == LCD_FLAG_ERROR ? SOFT3D_LCD_ERROR_DMA_TRANSFER :
                                                 SOFT3D_LCD_ERROR_DMA_TIMEOUT;
        lcd_stop();
        return false;
    }
    s_inflight = false;
    s_completion = 0U;
    (void)osThreadFlagsClear(LCD_FLAGS);
    return true;
}

static bool lcd_validate_band(uint16_t y, uint16_t rows, const uint8_t *pixels)
{
    uint32_t length = (uint32_t)rows * SOFT3D_LCD_WIDTH * 2U;
    if (!s_initialized) return lcd_reject(SOFT3D_LCD_ERROR_INITIALIZATION);
    if (s_owner != NULL && s_owner != osThreadGetId()) return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    if (s_inflight) return lcd_reject(SOFT3D_LCD_ERROR_BUSY);
    if (pixels == NULL || rows == 0U || length > UINT16_MAX ||
        y >= SOFT3D_LCD_HEIGHT || rows > SOFT3D_LCD_HEIGHT - y) {
        return lcd_reject(SOFT3D_LCD_ERROR_ARGUMENT);
    }
    return true;
}

static bool lcd_window(uint16_t y, uint16_t rows)
{
    uint16_t last_y;
    uint8_t column[4] = {0U, LCD_X_OFFSET, 1U, 0x2BU};
    uint8_t row[4];
    last_y = (uint16_t)(y + rows - 1U);
    row[0] = (uint8_t)(y >> 8);
    row[1] = (uint8_t)y;
    row[2] = (uint8_t)(last_y >> 8);
    row[3] = (uint8_t)last_y;
    return lcd_command(0x2AU, column, sizeof(column)) &&
           lcd_command(0x2BU, row, sizeof(row)) && lcd_command(0x2CU, NULL, 0U);
}

bool Soft3D_LCD_WriteBlocking(uint16_t y, uint16_t rows, uint8_t *big_endian_pixels)
{
    uint32_t length = (uint32_t)rows * SOFT3D_LCD_WIDTH * 2U;
    HAL_StatusTypeDef result;
    if (!lcd_validate_band(y, rows, big_endian_pixels) || !lcd_window(y, rows)) return false;
    lcd_cs(GPIO_PIN_RESET);
    result = HAL_SPI_Transmit(&hspi1, big_endian_pixels, (uint16_t)length, LCD_COMMAND_TIMEOUT_MS);
    lcd_cs(GPIO_PIN_SET);
    if (result != HAL_OK) {
        s_error = SOFT3D_LCD_ERROR_POLLING;
        lcd_stop();
        return false;
    }
    return true;
}

bool Soft3D_LCD_Submit(uint16_t y, uint16_t rows, uint8_t *big_endian_pixels)
{
    uint32_t length = (uint32_t)rows * SOFT3D_LCD_WIDTH * 2U;
    if (s_owner == NULL) return lcd_reject(SOFT3D_LCD_ERROR_OWNER);
    if (!lcd_validate_band(y, rows, big_endian_pixels)) return false;
    if (s_dma_disabled || hspi1.hdmatx == NULL || hspi1.hdmatx->Instance == NULL) {
        return lcd_reject(SOFT3D_LCD_ERROR_DMA_START);
    }
    if (!lcd_window(y, rows)) return false;

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        uintptr_t first = (uintptr_t)big_endian_pixels & ~(uintptr_t)31U;
        uintptr_t end = ((uintptr_t)big_endian_pixels + length + 31U) & ~(uintptr_t)31U;
        SCB_CleanDCache_by_Addr((uint32_t *)first, (int32_t)(end - first));
    }
#endif
    __DSB();
    lcd_cs(GPIO_PIN_RESET);
    (void)osThreadFlagsClear(LCD_FLAGS);
    s_completion = 0U;
    s_inflight = true;
    if (HAL_SPI_Transmit_DMA(&hspi1, big_endian_pixels, (uint16_t)length) != HAL_OK) {
        s_error = SOFT3D_LCD_ERROR_DMA_START;
        lcd_stop();
        return false;
    }
    return true;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *spi)
{
    if (spi == &hspi1 && s_inflight && s_owner != NULL) {
        /* H7 normal-mode DMA reaches this callback from SPI EOT, not DMA TC. */
        lcd_cs(GPIO_PIN_SET);
        s_completion = LCD_FLAG_DONE;
        (void)osThreadFlagsSet(s_owner, LCD_FLAG_DONE);
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
    if (spi == &hspi1 && s_inflight && s_owner != NULL) {
        s_completion = LCD_FLAG_ERROR;
        (void)osThreadFlagsSet(s_owner, LCD_FLAG_ERROR);
    }
}
