#include "soft3d_lcd.h"
#include "main.h"
#include "spi.h"
#include "cmsis_os2.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t data[16];
    uint16_t length;
    GPIO_PinState dc;
    const uint8_t *address;
} Write;

static DMA_Stream_TypeDef s_dma_stream;
static DMA_HandleTypeDef s_dma = {&s_dma_stream};
SPI_HandleTypeDef hspi1 = {&s_dma};
SCB_Type test_scb;
static Write s_writes[128];
static unsigned s_write_count;
static GPIO_PinState s_cs = GPIO_PIN_SET, s_dc, s_backlight, s_reset;
static bool s_dma_active, s_dma_fail, s_dma_stuck, s_eot, s_kernel_running;
static bool s_irq_enabled[3] = {true, true, true};
static unsigned s_abort_count, s_dma_abort_count, s_dma_resets, s_reset_assertions;
static unsigned s_cache_clean_count, s_barriers, s_os_calls;
static uint8_t *s_dma_address;
static uint16_t s_dma_length;
static uintptr_t s_cache_address;
static int32_t s_cache_size;
static unsigned s_fail_write;
static uint32_t s_tick_frequency = 1000U, s_flags, s_wait_timeout;
static uint32_t s_delays[8];
static unsigned s_delay_count;
static osThreadId_t s_thread = (void *)1;
static int s_wait_action;
static uint8_t s_pixels[280U * 117U * 2U + 32U];

static void complete_eot(void)
{
    s_dma_active = false;
    s_dma_stream.CR = 0U;
    s_eot = true;
    HAL_SPI_TxCpltCallback(&hspi1);
    s_eot = false;
}

void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state)
{
    (void)port;
    if (pin == LCD_CS_Pin) {
        if (state == GPIO_PIN_SET) assert(!s_dma_active || s_eot);
        s_cs = state;
    } else if (pin == LCD_DC_Pin) {
        assert(!s_dma_active);
        s_dc = state;
    } else if (pin == LCD_BLK_Pin) {
        s_backlight = state;
    } else if (pin == LCD_RES_Pin) {
        assert(!s_dma_active);
        s_reset = state;
        if (state == GPIO_PIN_RESET) ++s_reset_assertions;
    } else {
        assert(false);
    }
}

void HAL_Delay(uint32_t milliseconds)
{
    assert(!s_kernel_running && s_delay_count < 8U);
    s_delays[s_delay_count++] = milliseconds;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *data,
                                  uint16_t length, uint32_t timeout)
{
    Write *write;
    assert(spi == &hspi1 && s_cs == GPIO_PIN_RESET && !s_dma_active);
    assert(timeout > 0U && timeout <= 100U && length > 0U);
    assert(s_write_count < sizeof(s_writes) / sizeof(s_writes[0]));
    write = &s_writes[s_write_count++];
    memcpy(write->data, data, length < sizeof(write->data) ? length : sizeof(write->data));
    write->length = length;
    write->dc = s_dc;
    write->address = data;
    return s_fail_write == s_write_count ? HAL_ERROR : HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *spi, uint8_t *data, uint16_t length)
{
    assert(s_kernel_running && spi == &hspi1 && s_cs == GPIO_PIN_RESET && s_dc == GPIO_PIN_SET);
    assert(!s_dma_active && s_barriers > 0U);
    if (s_dma_fail) return HAL_ERROR;
    s_dma_address = data;
    s_dma_length = length;
    s_dma_active = true;
    s_dma_stream.CR = DMA_SxCR_EN;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi)
{
    assert(spi == &hspi1 && !s_irq_enabled[SPI1_IRQn] && !s_irq_enabled[DMA1_Stream0_IRQn]);
    ++s_abort_count;
    /* Require independent DMA cleanup even when SPI abort itself reports failure. */
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma)
{
    assert(dma == &s_dma);
    assert(!s_irq_enabled[SPI1_IRQn] && !s_irq_enabled[DMA1_Stream0_IRQn]);
    ++s_dma_abort_count;
    if (s_dma_stuck) return HAL_ERROR;
    s_dma_active = false;
    s_dma_stream.CR = 0U;
    return HAL_OK;
}

void Test_DMA_Reset(int asserted)
{
    assert(!s_irq_enabled[SPI1_IRQn] && !s_irq_enabled[DMA1_Stream0_IRQn]);
    if (asserted) {
        ++s_dma_resets;
        s_dma_active = false;
        s_dma_stream.CR = 0U;
    }
}
void HAL_NVIC_DisableIRQ(int irq) {s_irq_enabled[irq] = false;}
void HAL_NVIC_EnableIRQ(int irq) {s_irq_enabled[irq] = true;}
void HAL_NVIC_ClearPendingIRQ(int irq) {assert(!s_irq_enabled[irq]);}
void SCB_CleanDCache_by_Addr(uint32_t *address, int32_t size)
{
    ++s_cache_clean_count;
    s_cache_address = (uintptr_t)address;
    s_cache_size = size;
}
void Test_DSB(void) {++s_barriers;}
void Test_SPI_Disable(SPI_HandleTypeDef *spi) {assert(spi == &hspi1 && !s_dma_active);}
void Error_Handler(void) {assert(false); abort();}

static void require_kernel(void) {assert(s_kernel_running); ++s_os_calls;}
uint32_t osKernelGetTickFreq(void) {require_kernel(); return s_tick_frequency;}
osThreadId_t osThreadGetId(void) {require_kernel(); return s_thread;}
osStatus_t osDelay(uint32_t ticks)
{
    require_kernel();
    assert(s_delay_count < 8U);
    s_delays[s_delay_count++] = ticks;
    return 0;
}
uint32_t osThreadFlagsClear(uint32_t flags)
{
    uint32_t before = s_flags;
    require_kernel();
    s_flags &= ~flags;
    return before;
}
uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout)
{
    require_kernel();
    assert(flags == 3U && options == osFlagsWaitAny);
    s_wait_timeout = timeout;
    if (s_wait_action == 1) complete_eot();
    if (s_wait_action == 2) HAL_SPI_ErrorCallback(&hspi1);
    return s_wait_action == 0 ? 0xFFFFFFFEU : s_flags;
}
uint32_t osThreadFlagsSet(osThreadId_t thread, uint32_t flags)
{
    require_kernel();
    assert(thread == (void *)1);
    s_flags |= flags;
    return s_flags;
}

static void clear_observation(void)
{
    s_write_count = 0U;
    s_fail_write = 0U;
    s_delay_count = 0U;
    s_dma_fail = false;
    s_wait_action = 0;
}

static void check_window(unsigned start, uint8_t y, uint8_t last_y)
{
    static const uint8_t expected[] = {0x2AU, 0U, 20U, 1U, 43U, 0x2BU, 0U, 0U, 0U, 0U, 0x2CU};
    unsigned i;
    for (i = 0U; i < sizeof(expected); ++i) {
        uint8_t byte = i == 7U ? y : (i == 9U ? last_y : expected[i]);
        assert(s_writes[start + i].length == 1U && s_writes[start + i].data[0] == byte);
        assert(s_writes[start + i].dc == (i == 0U || i == 5U || i == 10U ? GPIO_PIN_RESET : GPIO_PIN_SET));
    }
}

static void test_boot_without_rtos_or_dma(void)
{
    static const uint8_t commands[] = {
        0x11U, 0x36U, 0x3AU, 0xB2U, 0xB7U, 0xBBU, 0xC2U, 0xC3U,
        0xC4U, 0xC6U, 0xD0U, 0xE0U, 0xE1U, 0x21U, 0x29U
    };
    unsigned i, count = 0U, resets;
    assert(Soft3D_LCD_LastError() == SOFT3D_LCD_ERROR_NONE);
    assert(!Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
    hspi1.hdmatx = NULL;
    s_fail_write = 1U;
    assert(!Soft3D_LCD_InitBlocking());
    assert(Soft3D_LCD_LastError() == SOFT3D_LCD_ERROR_COMMAND);
    assert(s_dma_abort_count == 0U && s_os_calls == 0U);
    assert(s_backlight == GPIO_PIN_SET);
    clear_observation();
    assert(Soft3D_LCD_InitBlocking());
    assert(s_delay_count == 4U);
    assert(s_delays[0] == 100U && s_delays[1] == 100U && s_delays[2] == 100U && s_delays[3] == 120U);
    assert(s_backlight == GPIO_PIN_SET && s_reset == GPIO_PIN_SET);
    for (i = 0U; i < s_write_count; ++i) {
        assert(s_writes[i].length == 1U);
        if (s_writes[i].dc == GPIO_PIN_RESET) {
            assert(count < sizeof(commands) && s_writes[i].data[0] == commands[count++]);
        }
    }
    assert(count == sizeof(commands));
    assert(s_writes[2].data[0] == 0x70U && s_writes[4].data[0] == 0x05U);
    clear_observation();
    resets = s_reset_assertions;
    assert(Soft3D_LCD_InitBlocking());
    assert(s_write_count == 0U && s_delay_count == 0U && resets == s_reset_assertions);
    assert(Soft3D_LCD_WriteBlocking(208U, 32U, s_pixels));
    assert(s_write_count == 12U);
    check_window(0U, 208U, 239U);
    assert(s_writes[11].address == s_pixels && s_writes[11].length == 17920U);
    assert(s_writes[11].dc == GPIO_PIN_SET && s_cs == GPIO_PIN_SET);
    assert(s_os_calls == 0U && !s_dma_active && s_backlight == GPIO_PIN_SET);
    hspi1.hdmatx = &s_dma;
}

static void test_attach_preserves_boot_page(void)
{
    unsigned resets = s_reset_assertions;
    clear_observation();
    s_kernel_running = true;
    assert(Soft3D_LCD_Attach());
    assert(Soft3D_LCD_Init());
    assert(s_write_count == 0U && s_delay_count == 0U && resets == s_reset_assertions);
    assert(s_backlight == GPIO_PIN_SET);
    s_thread = (void *)2;
    assert(!Soft3D_LCD_Attach());
    assert(!Soft3D_LCD_Init());
    assert(!Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
    assert(!Soft3D_LCD_Submit(0U, 1U, s_pixels));
    assert(!Soft3D_LCD_Wait(100U));
    assert(Soft3D_LCD_LastError() == SOFT3D_LCD_ERROR_OWNER && s_write_count == 0U);
    s_thread = (void *)1;
}

static void test_dma_and_eot(void)
{
    SPI_HandleTypeDef other = {NULL};
    clear_observation();
    test_scb.CCR = SCB_CCR_DC_Msk;
    assert(Soft3D_LCD_Submit(208U, 32U, s_pixels + 1U));
    assert(s_write_count == 11U && s_dma_address == s_pixels + 1U && s_dma_length == 17920U);
    check_window(0U, 208U, 239U);
    assert(s_cache_clean_count == 1U && s_cache_address % 32U == 0U && s_cache_size % 32 == 0);
    assert(s_cache_address <= (uintptr_t)(s_pixels + 1U));
    assert(s_cache_address + (uintptr_t)s_cache_size >= (uintptr_t)(s_pixels + 1U + 17920U));
    assert(!Soft3D_LCD_Submit(0U, 1U, s_pixels));
    assert(!Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
    assert(!Soft3D_LCD_Attach() && !Soft3D_LCD_Init());
    assert(Soft3D_LCD_LastError() == SOFT3D_LCD_ERROR_BUSY && s_write_count == 11U);
    HAL_SPI_TxCpltCallback(&other);
    HAL_SPI_ErrorCallback(&other);
    s_dma_stream.CR = 0U;
    assert(s_cs == GPIO_PIN_RESET);
    complete_eot();
    assert(s_cs == GPIO_PIN_SET);
    assert(!Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
    assert(Soft3D_LCD_Wait(0U));
    assert(Soft3D_LCD_Wait(0U));
    assert(Soft3D_LCD_Submit(0U, 1U, s_pixels));
    s_wait_action = 1;
    s_tick_frequency = 128U;
    assert(Soft3D_LCD_Wait(1U) && s_wait_timeout == 1U);
    s_tick_frequency = 1000U;
}

static void test_bounds(void)
{
    clear_observation();
    assert(!Soft3D_LCD_WriteBlocking(240U, 1U, s_pixels));
    assert(!Soft3D_LCD_WriteBlocking(239U, 2U, s_pixels));
    assert(!Soft3D_LCD_WriteBlocking(0U, 0U, s_pixels));
    assert(!Soft3D_LCD_WriteBlocking(0U, 118U, s_pixels));
    assert(!Soft3D_LCD_WriteBlocking(0U, 1U, NULL));
    assert(!Soft3D_LCD_Submit(239U, 2U, s_pixels));
    assert(Soft3D_LCD_LastError() == SOFT3D_LCD_ERROR_ARGUMENT && s_write_count == 0U);
    assert(Soft3D_LCD_WriteBlocking(0U, 117U, s_pixels));
    assert(s_writes[11].length == 65520U);
}

static void check_polling_recovery(Soft3D_LCDError expected)
{
    unsigned resets = s_reset_assertions;
    assert(Soft3D_LCD_LastError() == expected);
    assert(!s_dma_active && s_dma_stream.CR == 0U);
    assert(s_backlight == GPIO_PIN_SET && s_cs == GPIO_PIN_SET);
    assert(s_irq_enabled[SPI1_IRQn] && s_irq_enabled[DMA1_Stream0_IRQn]);
    HAL_SPI_TxCpltCallback(&hspi1);
    HAL_SPI_ErrorCallback(&hspi1);
    assert(s_flags == 0U);
    clear_observation();
    assert(Soft3D_LCD_WriteBlocking(0U, 16U, s_pixels));
    assert(s_write_count == 12U && s_writes[11].length == 8960U);
    assert(Soft3D_LCD_LastError() == expected);
    assert(s_reset_assertions == resets && s_backlight == GPIO_PIN_SET);
}

static void test_dma_failure_keeps_visible_frame(void)
{
    clear_observation();
    assert(Soft3D_LCD_Submit(0U, 1U, s_pixels));
    assert(!Soft3D_LCD_Wait(3U));
    check_polling_recovery(SOFT3D_LCD_ERROR_DMA_TIMEOUT);
    clear_observation();
    assert(Soft3D_LCD_Submit(0U, 1U, s_pixels));
    s_wait_action = 2;
    assert(!Soft3D_LCD_Wait(100U));
    check_polling_recovery(SOFT3D_LCD_ERROR_DMA_TRANSFER);
    clear_observation();
    s_dma_fail = true;
    assert(!Soft3D_LCD_Submit(0U, 1U, s_pixels));
    check_polling_recovery(SOFT3D_LCD_ERROR_DMA_START);
}

static void test_polling_failures(void)
{
    unsigned i;
    for (i = 1U; i <= 12U; ++i) {
        clear_observation();
        s_fail_write = i;
        assert(!Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
        check_polling_recovery(i == 12U ? SOFT3D_LCD_ERROR_POLLING : SOFT3D_LCD_ERROR_COMMAND);
    }
}

static void test_stuck_dma_is_bounded_and_polling_survives(void)
{
    clear_observation();
    assert(Soft3D_LCD_Submit(0U, 1U, s_pixels));
    s_dma_stuck = true;
    assert(!Soft3D_LCD_Wait(1U));
    assert(s_dma_resets == 1U);
    s_dma_stuck = false;
    check_polling_recovery(SOFT3D_LCD_ERROR_ABORT);
    clear_observation();
    assert(!Soft3D_LCD_Submit(0U, 1U, s_pixels));
    assert(s_write_count == 0U);
    assert(Soft3D_LCD_WriteBlocking(0U, 1U, s_pixels));
}

int main(void)
{
    test_boot_without_rtos_or_dma();
    test_attach_preserves_boot_page();
    test_dma_and_eot();
    test_bounds();
    test_dma_failure_keeps_visible_frame();
    test_polling_failures();
    test_stuck_dma_is_bounded_and_polling_survives();
    puts("soft3d_lcd: boot polling, ownership, EOT and fallback tests passed");
    return 0;
}
