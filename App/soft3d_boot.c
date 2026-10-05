#include "soft3d_boot.h"
#include "soft3d_ui.h"
#include "soft3d_lcd.h"

#include <stdint.h>

#define STATUS_ROWS 8U
static uint16_t s_status_pixels[SOFT3D_LCD_WIDTH * STATUS_ROWS];

bool Soft3D_BootStatus(const char *phase, const char *detail, const char *footer)
{
    uint16_t y;
    for (y = 0U; y < SOFT3D_LCD_HEIGHT; y = (uint16_t)(y + STATUS_ROWS)) {
        uint32_t i;
        Soft3D_UI_StatusBand(s_status_pixels, SOFT3D_LCD_WIDTH, SOFT3D_LCD_HEIGHT,
                             y, STATUS_ROWS, phase, detail, footer);
        for (i = 0U; i < SOFT3D_LCD_WIDTH * STATUS_ROWS; ++i) {
            uint16_t color = s_status_pixels[i];
            s_status_pixels[i] = (uint16_t)((color << 8) | (color >> 8));
        }
        if (!Soft3D_LCD_WriteBlocking(y, STATUS_ROWS, (uint8_t *)s_status_pixels)) return false;
    }
    Soft3D_LCD_Backlight(true);
    return true;
}

bool Soft3D_BootInit(void)
{
    if (!Soft3D_LCD_InitBlocking()) return false;
    return Soft3D_BootStatus("LCD POLLING OK", "BOOT STARTING", SOFT3D_DISPLAY_BUILD);
}
