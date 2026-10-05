#include "soft3d_boot.h"
#include "soft3d_lcd.h"
#include "soft3d_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROWS 8U
#define BANDS (SOFT3D_LCD_HEIGHT / ROWS)
#define FRAME_PIXELS (SOFT3D_LCD_WIDTH * SOFT3D_LCD_HEIGHT)
#define BAND_BYTES (SOFT3D_LCD_WIDTH * ROWS * 2U)
#define GUARD 0xA35C69E1U

static uint16_t expected[FRAME_PIXELS];
static struct {
    uint32_t before;
    uint8_t bytes[FRAME_PIXELS * 2U];
    uint32_t after;
} captured;
static unsigned init_calls;
static unsigned write_calls;
static unsigned accepted_bands;
static unsigned backlight_calls;
static unsigned fail_band;
static bool init_ok;
static bool expect_init;

static void reset(const char *phase, const char *detail, const char *footer)
{
    init_calls = 0U;
    write_calls = 0U;
    accepted_bands = 0U;
    backlight_calls = 0U;
    fail_band = 0U;
    init_ok = true;
    expect_init = false;
    captured.before = captured.after = GUARD;
    memset(captured.bytes, 0xA5, sizeof(captured.bytes));
    memset(expected, 0x12, sizeof(expected));
    Soft3D_UI_StatusBand(expected, SOFT3D_LCD_WIDTH, SOFT3D_LCD_HEIGHT,
                         0U, SOFT3D_LCD_HEIGHT, phase, detail, footer);
}

bool Soft3D_LCD_InitBlocking(void)
{
    assert(write_calls == 0U && backlight_calls == 0U);
    ++init_calls;
    return init_ok;
}

bool Soft3D_LCD_WriteBlocking(uint16_t y, uint16_t rows, uint8_t *wire_pixels)
{
    unsigned i;
    assert(wire_pixels != NULL);
    assert(rows == ROWS && y == write_calls * ROWS);
    assert(write_calls < BANDS);
    assert(backlight_calls == 0U);
    assert(init_calls == (expect_init ? 1U : 0U));
    ++write_calls;
    for (i = 0U; i < SOFT3D_LCD_WIDTH * ROWS; ++i) {
        uint16_t color = expected[(unsigned)y * SOFT3D_LCD_WIDTH + i];
        assert(wire_pixels[2U * i] == (uint8_t)(color >> 8));
        assert(wire_pixels[2U * i + 1U] == (uint8_t)color);
    }
    if (write_calls == fail_band) return false;
    memcpy(&captured.bytes[(unsigned)y * SOFT3D_LCD_WIDTH * 2U], wire_pixels, BAND_BYTES);
    ++accepted_bands;
    return true;
}

void Soft3D_LCD_Backlight(bool on)
{
    assert(on);
    assert(write_calls == BANDS && accepted_bands == BANDS);
    assert(backlight_calls == 0U);
    ++backlight_calls;
}

static void check_capture(unsigned bands)
{
    unsigned i;
    assert(captured.before == GUARD && captured.after == GUARD);
    for (i = 0U; i < bands * SOFT3D_LCD_WIDTH * ROWS; ++i) {
        assert(captured.bytes[2U * i] == (uint8_t)(expected[i] >> 8));
        assert(captured.bytes[2U * i + 1U] == (uint8_t)expected[i]);
    }
    for (i = bands * BAND_BYTES; i < sizeof(captured.bytes); ++i) {
        assert(captured.bytes[i] == 0xA5U);
    }
}

static void test_initialization(void)
{
    reset("LCD POLLING OK", "BOOT STARTING", SOFT3D_DISPLAY_BUILD);
    expect_init = true;
    assert(Soft3D_BootInit());
    assert(init_calls == 1U && write_calls == BANDS && backlight_calls == 1U);
    check_capture(BANDS);
    /* The color strip includes distinct high/low bytes, catching byte reversal. */
    assert(captured.bytes[(35U * SOFT3D_LCD_WIDTH + 20U) * 2U] == 0xF8U);
    assert(captured.bytes[(35U * SOFT3D_LCD_WIDTH + 20U) * 2U + 1U] == 0x00U);
    assert(captured.bytes[(35U * SOFT3D_LCD_WIDTH + 130U) * 2U] == 0x07U);
    assert(captured.bytes[(35U * SOFT3D_LCD_WIDTH + 130U) * 2U + 1U] == 0xE0U);

    reset("LCD POLLING OK", "BOOT STARTING", SOFT3D_DISPLAY_BUILD);
    expect_init = true;
    init_ok = false;
    assert(!Soft3D_BootInit());
    assert(init_calls == 1U && write_calls == 0U && backlight_calls == 0U);
    check_capture(0U);
}

static void test_failure_stops_every_band(void)
{
    unsigned failed;
    for (failed = 1U; failed <= BANDS; ++failed) {
        reset("LCD POLLING OK", "BOOT STARTING", SOFT3D_DISPLAY_BUILD);
        expect_init = true;
        fail_band = failed;
        assert(!Soft3D_BootInit());
        assert(init_calls == 1U && write_calls == failed);
        assert(accepted_bands == failed - 1U && backlight_calls == 0U);
        check_capture(failed - 1U);
    }
}

static void test_status_updates(void)
{
    reset("RTOS STARTING", "SPI POLLING", SOFT3D_DISPLAY_BUILD);
    assert(Soft3D_BootStatus("RTOS STARTING", "SPI POLLING", SOFT3D_DISPLAY_BUILD));
    assert(init_calls == 0U && write_calls == BANDS && backlight_calls == 1U);
    check_capture(BANDS);

    /* Static pixels from the prior call must be repainted before byte swapping. */
    reset("DMA FAILED", "POLLING RECOVERY", SOFT3D_DISPLAY_BUILD " ERR 5");
    assert(Soft3D_BootStatus("DMA FAILED", "POLLING RECOVERY", SOFT3D_DISPLAY_BUILD " ERR 5"));
    assert(init_calls == 0U && write_calls == BANDS && backlight_calls == 1U);
    check_capture(BANDS);

    reset("DMA FAILED", "POLLING RECOVERY", SOFT3D_DISPLAY_BUILD);
    fail_band = 4U;
    assert(!Soft3D_BootStatus("DMA FAILED", "POLLING RECOVERY", SOFT3D_DISPLAY_BUILD));
    assert(init_calls == 0U && write_calls == 4U && backlight_calls == 0U);
    check_capture(3U);
}

int main(void)
{
    assert(strcmp(SOFT3D_DISPLAY_BUILD, "ENGINE LAB V4") == 0);
    test_initialization();
    test_failure_stops_every_band();
    test_status_updates();
    puts("boot: all 30 polling bands verified, RGB565 wire order and failure stops passed; no HAL/RTOS linked");
    return 0;
}
