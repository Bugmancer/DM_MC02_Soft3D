#ifndef SOFT3D_LCD_H
#define SOFT3D_LCD_H

#include <stdbool.h>
#include <stdint.h>

#define SOFT3D_LCD_WIDTH 280U
#define SOFT3D_LCD_HEIGHT 240U

typedef enum {
    SOFT3D_LCD_ERROR_NONE = 0,
    SOFT3D_LCD_ERROR_INITIALIZATION,
    SOFT3D_LCD_ERROR_COMMAND,
    SOFT3D_LCD_ERROR_DMA_START,
    SOFT3D_LCD_ERROR_DMA_TIMEOUT,
    SOFT3D_LCD_ERROR_DMA_TRANSFER,
    SOFT3D_LCD_ERROR_BUSY,
    SOFT3D_LCD_ERROR_OWNER,
    SOFT3D_LCD_ERROR_ARGUMENT,
    SOFT3D_LCD_ERROR_POLLING,
    SOFT3D_LCD_ERROR_ABORT
} Soft3D_LCDError;

/* Boot phase: GPIO, DMA controller clock and SPI1 are ready, HAL tick is running.
 * No RTOS API or DMA transfer is used. Init is idempotent and does not blank an
 * already initialized panel. Cold initialization uses the proven panel reset
 * sequence, including backlight-on before sleep-out.
 */
bool Soft3D_LCD_InitBlocking(void);
bool Soft3D_LCD_WriteBlocking(uint16_t y, uint16_t rows, uint8_t *big_endian_pixels);
/* RTOS phase: RenderTask adopts the boot display without reset or backlight change.
 * After attachment all transfer APIs are restricted to that task. Flags 0/1 are
 * reserved. Blocking writes remain available as a DMA-independent fallback.
 */
bool Soft3D_LCD_Attach(void);
/* Convenience cold initialization from one RTOS thread; also idempotent. */
bool Soft3D_LCD_Init(void);
/* DMA failure retires the buffer and preserves the visible panel for polling. */
bool Soft3D_LCD_Wait(uint32_t timeout_ms);
/* Wait must consume the previous band before Submit. Keep pixels immutable until Wait. */
bool Soft3D_LCD_Submit(uint16_t y, uint16_t rows, uint8_t *big_endian_pixels);
/* Last failure is sticky; successful fallback writes do not erase diagnostics. */
Soft3D_LCDError Soft3D_LCD_LastError(void);
/* Cold initialization enables the backlight; transfer errors never switch it off. */
void Soft3D_LCD_Backlight(bool on);

#endif
