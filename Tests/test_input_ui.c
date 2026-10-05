#include "soft3d_input.h"
#include "soft3d_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 280U
#define HEIGHT 240U
#define ROWS 16U

static uint16_t full[WIDTH * HEIGHT];
static uint16_t guarded[WIDTH * ROWS + 2U];
static uint16_t reference[WIDTH * HEIGHT];

static void test_keys(void)
{
    Soft3D_Debounce state = {SOFT3D_KEY_NONE, 0U, 0U};
    assert(Soft3D_KeyDecode(0U) == SOFT3D_KEY_SELECT);
    assert(Soft3D_KeyDecode(50U) == SOFT3D_KEY_SELECT);
    assert(Soft3D_KeyDecode(13000U) == SOFT3D_KEY_DOWN);
    assert(Soft3D_KeyDecode(26100U) == SOFT3D_KEY_UP);
    assert(Soft3D_KeyDecode(39100U) == SOFT3D_KEY_LEFT);
    assert(Soft3D_KeyDecode(52200U) == SOFT3D_KEY_RIGHT);
    assert(Soft3D_KeyDecode(65535U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyDecode(17000U) == SOFT3D_KEY_INVALID);
    assert(Soft3D_KeyUpdate(&state, 39100U, 10U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 65535U, 20U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 39100U, 30U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 39100U, 89U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 39100U, 90U) == SOFT3D_KEY_LEFT);
    assert(Soft3D_KeyUpdate(&state, 39100U, 1000U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 52200U, 1010U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 52200U, 1100U) == SOFT3D_KEY_NONE);
    /* Invalid ladder voltages must not rearm a held key. */
    assert(Soft3D_KeyUpdate(&state, 17000U, 1200U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 17000U, 1300U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 65535U, 1310U) == SOFT3D_KEY_NONE);
    assert(state.pressed == 1U);
    assert(Soft3D_KeyUpdate(&state, 65535U, 1360U) == SOFT3D_KEY_NONE);
    assert(state.pressed == 1U);
    assert(Soft3D_KeyUpdate(&state, 65535U, 1370U) == SOFT3D_KEY_NONE);
    assert(state.pressed == 0U);
    assert(Soft3D_KeyUpdate(&state, 52200U, 1400U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 52200U, 1460U) == SOFT3D_KEY_RIGHT);
    state.candidate = SOFT3D_KEY_NONE;
    state.pressed = 0U;
    assert(Soft3D_KeyUpdate(&state, 50U, UINT32_MAX - 20U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyUpdate(&state, 50U, 39U) == SOFT3D_KEY_SELECT);
}

static void test_ui(void)
{
    uint32_t i;
    uint16_t y;
    for (i = 0U; i < WIDTH * HEIGHT; ++i) full[i] = 0x1234U;
    Soft3D_UI_Band(full, WIDTH, HEIGHT, 0U, HEIGHT,
                   "SOFT3D TORUS", "TEXTURE", "FPS 12.3 MS 81 TRI 192", 1U);
    for (y = 0U; y < HEIGHT; y = (uint16_t)(y + ROWS)) {
        guarded[0] = guarded[WIDTH * ROWS + 1U] = 0xABCDU;
        for (i = 1U; i <= WIDTH * ROWS; ++i) guarded[i] = 0x1234U;
        Soft3D_UI_Band(&guarded[1], WIDTH, HEIGHT, y, ROWS,
                       "SOFT3D TORUS", "TEXTURE", "FPS 12.3 MS 81 TRI 192", 1U);
        assert(guarded[0] == 0xABCDU && guarded[WIDTH * ROWS + 1U] == 0xABCDU);
        assert(memcmp(&guarded[1], &full[(uint32_t)y * WIDTH], WIDTH * ROWS * sizeof(uint16_t)) == 0);
    }
    assert(full[100U * WIDTH + 100U] == 0x1234U);
    assert(full[15U * WIDTH] == 0x4208U);
    assert(full[224U * WIDTH] == 0x4208U);
    assert(full[4U * WIDTH + 9U] == 0x07DFU);
    /* Narrow surfaces and long labels must clip without out-of-bounds writes. */
    Soft3D_UI_Band(&guarded[1], 16U, 32U, 0U, 16U,
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "TEXTURE", "1234567890", 1U);
    assert(guarded[0] == 0xABCDU && guarded[WIDTH * ROWS + 1U] == 0xABCDU);
    guarded[1] = 0x5678U;
    Soft3D_UI_Band(&guarded[1], WIDTH, HEIGHT, 239U, 2U, "A", "B", "C", 0U);
    assert(guarded[1] == 0x5678U);
}

static void test_status_ui(void)
{
    uint32_t i, bright = 0U, ink = 0U;
    uint16_t y;
    memset(full, 0x12, sizeof(full));
    Soft3D_UI_StatusBand(full, WIDTH, HEIGHT, 0U, HEIGHT,
                         "LCD POLLING OK", "IMU CALIBRATING", "DM-MC02 V0.3");
    for (y = 0U; y < HEIGHT; y = (uint16_t)(y + ROWS)) {
        guarded[0] = guarded[WIDTH * ROWS + 1U] = 0xABCDU;
        memset(&guarded[1], 0x34, WIDTH * ROWS * sizeof(uint16_t));
        Soft3D_UI_StatusBand(&guarded[1], WIDTH, HEIGHT, y, ROWS,
                             "LCD POLLING OK", "IMU CALIBRATING", "DM-MC02 V0.3");
        assert(guarded[0] == 0xABCDU && guarded[WIDTH * ROWS + 1U] == 0xABCDU);
        assert(memcmp(&guarded[1], &full[(uint32_t)y * WIDTH], WIDTH * ROWS * sizeof(uint16_t)) == 0);
    }
    for (i = 0U; i < WIDTH * HEIGHT; ++i) {
        if (full[i] == 0xFFFFU) ++bright;
        if (full[i] == 0x0000U) ++ink;
    }
    assert(bright > 40000U && ink > 3000U);
    assert(full[35U * WIDTH + 20U] == 0xF800U);
    assert(full[35U * WIDTH + 130U] == 0x07E0U);
    assert(full[35U * WIDTH + 240U] == 0x001FU);
    assert(full[0] == 0xFFFFU && full[HEIGHT * WIDTH - 1U] == 0xFFFFU);
    /* Sixth control line fits above the footer at both supported heights. */
    assert(full[193U * WIDTH + 8U] == 0x0000U);
    for (i = 0U; i < WIDTH; ++i) assert(full[214U * WIDTH + i] == 0xC618U);
    /* Long diagnostics stay inside the margin and overwrite prior labels. */
    Soft3D_UI_StatusBand(full, WIDTH, HEIGHT, 0U, HEIGHT,
                         "DMA FAILED RESTART WITH SPI POLLING", "IMU NOT AVAILABLE", "FIRMWARE STATUS");
    ink = 0U;
    for (i = 51U * WIDTH; i < 65U * WIDTH; ++i) if (full[i] == 0xB000U) ++ink;
    assert(ink > 100U);
    for (y = 51U; y < 87U; ++y) {
        for (i = WIDTH - 8U; i < WIDTH; ++i) assert(full[(uint32_t)y * WIDTH + i] == 0xFFFFU);
    }
    guarded[1] = 0x5678U;
    Soft3D_UI_StatusBand(&guarded[1], WIDTH, HEIGHT, 239U, 2U, "A", "B", "C");
    assert(guarded[1] == 0x5678U);
    Soft3D_UI_StatusBand(&guarded[1], WIDTH, HEIGHT, 0U, ROWS, NULL, "B", "C");
    assert(guarded[1] == 0x5678U);
    assert(guarded[0] == 0xABCDU && guarded[WIDTH * ROWS + 1U] == 0xABCDU);
    Soft3D_UI_StatusBand(full, WIDTH, 224U, 0U, 224U, "READY", "IMU READY", "TEST");
    assert(full[178U * WIDTH + 8U] == 0x0000U);
    for (i = 0U; i < WIDTH; ++i) assert(full[198U * WIDTH + i] == 0xC618U);
}

static void test_maze_level_ui(void)
{
    static const uint16_t widths[] = {160U, WIDTH};
    unsigned surface, won;
    uint32_t i;
    uint16_t y;
    for (surface = 0U; surface < 2U; ++surface) {
        uint16_t width = widths[surface];
        for (won = 0U; won < 2U; ++won) {
            memset(full, 0x12, sizeof(full));
            Soft3D_UI_MazeLevelBand(full, width, HEIGHT, 0U, HEIGHT,
                "PAUSED", "TIME 123.4 BEST 100.0", (uint8_t)won, 9999U);
            for (y = 0U; y < HEIGHT; y = (uint16_t)(y + ROWS)) {
                guarded[0] = guarded[width * ROWS + 1U] = 0xABCDU;
                memset(&guarded[1], 0x12, width * ROWS * sizeof(uint16_t));
                Soft3D_UI_MazeLevelBand(&guarded[1], width, HEIGHT, y, ROWS,
                    "PAUSED", "TIME 123.4 BEST 100.0", (uint8_t)won, 9999U);
                assert(guarded[0] == 0xABCDU && guarded[width * ROWS + 1U] == 0xABCDU);
                assert(memcmp(&guarded[1], &full[(uint32_t)y * width],
                    width * ROWS * sizeof(uint16_t)) == 0);
            }
            for (i = 32U * width; i < (HEIGHT - 16U) * width; ++i) assert(full[i] == 0x1212U);
            for (y = 0U; y < 31U; ++y) {
                unsigned x;
                for (x = 0U; x < 8U; ++x) {
                    assert(full[(uint32_t)y * width + x] == (won ? 0x06A0U : 0x18C3U));
                    assert(full[(uint32_t)y * width + width - 1U - x] == (won ? 0x06A0U : 0x18C3U));
                }
            }
            if (won == 0U && width == WIDTH) {
                assert(full[6U * width + 113U] == 0x07DFU);
                for (y = 0U; y < 31U; ++y) {
                    unsigned x;
                    for (x = 120U; x < 190U; ++x) assert(full[(uint32_t)y * width + x] == 0x18C3U);
                }
            }
        }
    }
    memset(full, 0x12, sizeof(full));
    memset(reference, 0x12, sizeof(reference));
    Soft3D_UI_MazeBand(full, WIDTH, HEIGHT, 0U, HEIGHT, "IMU", "TIME 0.0", 0U);
    Soft3D_UI_MazeLevelBand(reference, WIDTH, HEIGHT, 0U, HEIGHT, "IMU", "TIME 0.0", 0U, 1U);
    assert(memcmp(full, reference, sizeof(full)) == 0);
    Soft3D_UI_MazeLevelBand(reference, WIDTH, HEIGHT, 0U, HEIGHT, "IMU", "TIME 0.0", 0U, 0U);
    assert(memcmp(full, reference, sizeof(full)) == 0);
    Soft3D_UI_MazeLevelBand(full, WIDTH, HEIGHT, 0U, HEIGHT, "PAUSED", "TIME 0.0", 0U, 9999U);
    Soft3D_UI_MazeLevelBand(reference, WIDTH, HEIGHT, 0U, HEIGHT, "PAUSED", "TIME 0.0", 0U, UINT32_MAX);
    assert(memcmp(full, reference, sizeof(full)) == 0);
    guarded[0] = guarded[160U * ROWS + 1U] = 0xABCDU;
    Soft3D_UI_MazeLevelBand(&guarded[1], 160U, HEIGHT, 0U, ROWS,
        "ABCDEFGHIJKLMNOPQRSTUVXYZ", "TEST", 0U, 9999U);
    assert(guarded[0] == 0xABCDU && guarded[160U * ROWS + 1U] == 0xABCDU);
}

static void test_profile_ui(void)
{
    uint16_t y;
    uint32_t i;
    memset(full, 0x12, sizeof(full));
    Soft3D_UI_ProfileBand(full, WIDTH, HEIGHT, 0U, HEIGHT, "LAB ORBIT HOLD", "INDEX OFF",
        "G9999.9 R9999.9 IO9999.9 MS", "F9999.9 P95 9999.9 SKIP 100 N64");
    for (y = 0U; y < HEIGHT; y = (uint16_t)(y + ROWS)) {
        guarded[0] = guarded[WIDTH * ROWS + 1U] = 0xABCDU;
        memset(&guarded[1], 0x12, WIDTH * ROWS * sizeof(uint16_t));
        Soft3D_UI_ProfileBand(&guarded[1], WIDTH, HEIGHT, y, ROWS, "LAB ORBIT HOLD", "INDEX OFF",
            "G9999.9 R9999.9 IO9999.9 MS", "F9999.9 P95 9999.9 SKIP 100 N64");
        assert(guarded[0] == 0xABCDU && guarded[WIDTH * ROWS + 1U] == 0xABCDU);
        assert(memcmp(&guarded[1], &full[(uint32_t)y * WIDTH], WIDTH * ROWS * sizeof(uint16_t)) == 0);
    }
    for (i = 32U * WIDTH; i < (HEIGHT - 16U) * WIDTH; ++i) assert(full[i] == 0x1212U);
    for (y = 0U; y < 31U; ++y) {
        for (i = 96U; i < 200U; ++i) {
            if (y < 12U) assert(full[(uint32_t)y * WIDTH + i] == 0x18C3U);
        }
    }
}

static void test_gestures(void)
{
    Soft3D_KeyGesture state = {{SOFT3D_KEY_NONE, 0U, 0U}, 0U, 0U, 0U};
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 10U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 70U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 200U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 260U) == SOFT3D_KEY_SELECT);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 400U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 500U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 560U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 1359U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 1360U) == SOFT3D_KEY_SELECT_HOLD);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 3000U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3100U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3160U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 52200U, 3200U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 52200U, 3260U) == SOFT3D_KEY_RIGHT);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3300U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3360U) == SOFT3D_KEY_NONE);
    /* Glitches cannot build up enough hold time to emit a long press. */
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 3400U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 3460U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 17000U, 4000U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 50U, 4300U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 4400U) == SOFT3D_KEY_NONE);
    assert(Soft3D_KeyGestureUpdate(&state, 65535U, 4460U) == SOFT3D_KEY_SELECT);
}

static void test_both_gesture_keys(void)
{
    static const uint16_t adc[] = {50U, 13000U};
    static const Soft3D_Key short_keys[] = {SOFT3D_KEY_SELECT, SOFT3D_KEY_DOWN};
    static const Soft3D_Key held_keys[] = {SOFT3D_KEY_SELECT_HOLD, SOFT3D_KEY_DOWN_HOLD};
    unsigned key;
    for (key = 0U; key < 2U; ++key) {
        Soft3D_KeyGesture state = {0};
        uint32_t start = UINT32_MAX - 400U;
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 0U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 59U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 60U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 859U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 860U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 919U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 920U) == short_keys[key]);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 1000U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 1100U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 1160U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 1959U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 1960U) == held_keys[key]);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 3000U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3100U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 3160U) == SOFT3D_KEY_NONE);

        memset(&state, 0, sizeof(state));
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], start) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], start + 60U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], start + 859U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], start + 860U) == held_keys[key]);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, start + 900U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, start + 960U) == SOFT3D_KEY_NONE);
    }
}

static void test_cancelled_gestures(void)
{
    static const uint16_t adc[] = {50U, 13000U};
    static const Soft3D_Key short_keys[] = {SOFT3D_KEY_SELECT, SOFT3D_KEY_DOWN};
    unsigned key;
    for (key = 0U; key < 2U; ++key) {
        Soft3D_KeyGesture state = {0};
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 0U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 60U) == SOFT3D_KEY_NONE);
        Soft3D_KeyGestureCancel(&state, 700U);
        assert(state.pending == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 710U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 800U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 2000U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[1U - key], 2100U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[1U - key], 2200U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2300U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2359U) == SOFT3D_KEY_NONE);
        assert(state.debounce.pressed != 0U);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2360U) == SOFT3D_KEY_NONE);
        assert(state.debounce.pressed == 0U);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 2400U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 2460U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2500U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2560U) == short_keys[key]);

        /* An I/O failure during release also discards the original short press. */
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 2600U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, adc[key], 2660U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2700U) == SOFT3D_KEY_NONE);
        Soft3D_KeyGestureCancel(&state, 2750U);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2800U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 2860U) == SOFT3D_KEY_NONE);
    }
    {
        Soft3D_KeyGesture state = {0};
        Soft3D_KeyGestureCancel(&state, UINT32_MAX - 20U);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, UINT32_MAX - 10U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 65535U, 49U) == SOFT3D_KEY_NONE);
        assert(state.debounce.pressed == 0U);
        assert(Soft3D_KeyGestureUpdate(&state, 52200U, 60U) == SOFT3D_KEY_NONE);
        assert(Soft3D_KeyGestureUpdate(&state, 52200U, 120U) == SOFT3D_KEY_RIGHT);
    }
}

int main(void)
{
    test_keys();
    test_gestures();
    test_both_gesture_keys();
    test_cancelled_gestures();
    test_ui();
    test_status_ui();
    test_maze_level_ui();
    test_profile_ui();
    puts("Input/UI tests passed.");
    return 0;
}
