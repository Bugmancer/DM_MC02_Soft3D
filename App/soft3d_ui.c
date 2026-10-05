#include "soft3d_ui.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const uint8_t s_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
};
static const uint8_t s_letters[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

static uint8_t glyph_column(char ch, unsigned column)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= '0' && ch <= '9') return s_digits[(unsigned)(ch - '0')][column];
    if (ch >= 'A' && ch <= 'Z') return s_letters[(unsigned)(ch - 'A')][column];
    if (ch == '.') return column == 2U ? 0x60U : 0U;
    if (ch == '-') return 0x08U;
    if (ch == ':') return column == 2U ? 0x24U : 0U;
    if (ch == '/') {
        static const uint8_t slash[5] = {0x60U, 0x18U, 0x06U, 0x01U, 0x00U};
        return slash[column];
    }
    return 0U;
}

static void scaled_line(uint16_t *pixels, uint16_t width, uint16_t band_y,
                         uint16_t rows, unsigned text_y, const char *text,
                         unsigned scale, uint16_t color)
{
    size_t length = strlen(text);
    unsigned maximum = ((unsigned)width - 16U) / (6U * scale);
    unsigned character;
    for (character = 0U; character < maximum && character < length; ++character) {
        unsigned column, bit, dx, dy;
        char ch = length > maximum && character + 3U >= maximum ? '.' : text[character];
        for (column = 0U; column < 5U; ++column) {
            uint8_t glyph = glyph_column(ch, column);
            for (bit = 0U; bit < 7U; ++bit) {
                if ((glyph & (1U << bit)) == 0U) continue;
                for (dy = 0U; dy < scale; ++dy) {
                    unsigned screen_y = text_y + bit * scale + dy;
                    if (screen_y < band_y || screen_y >= (unsigned)band_y + rows) continue;
                    for (dx = 0U; dx < scale; ++dx) {
                        unsigned screen_x = 8U + (character * 6U + column) * scale + dx;
                        pixels[(screen_y - band_y) * width + screen_x] = color;
                    }
                }
            }
        }
    }
}

void Soft3D_UI_StatusBand(uint16_t *pixels, uint16_t width, uint16_t height,
                          uint16_t y, uint16_t rows, const char *phase,
                          const char *detail, const char *footer)
{
    static const char *const controls[] = {
        "CENTER PLAY / PAUSE",
        "HOLD CENTER RESTART",
        "UP ZERO / DOWN LAB",
        "LEFT INDEX A/B IN LAB",
        "RIGHT NEXT SCENE",
        "HOLD DOWN STATUS"
    };
    unsigned row, x, line;
    unsigned control_step = height >= 240U ? 18U : 15U;
    uint16_t phase_color;
    if (pixels == NULL || phase == NULL || detail == NULL || footer == NULL ||
        width < 160U || height < 224U || y >= height || rows > height - y) return;
    for (row = 0U; row < rows; ++row) {
        unsigned screen_y = (unsigned)y + row;
        for (x = 0U; x < width; ++x) {
            uint16_t color = 0xFFFFU;
            if (screen_y >= 34U && screen_y < 42U) {
                color = x < width / 3U ? 0xF800U : x < (2U * width) / 3U ? 0x07E0U : 0x001FU;
            } else if (screen_y == 96U || screen_y == (unsigned)height - 26U) {
                color = 0xC618U;
            }
            pixels[row * width + x] = color;
        }
    }
    phase_color = strstr(phase, "FAIL") != NULL || strstr(phase, "ERROR") != NULL ? 0xB000U : 0x0000U;
    scaled_line(pixels, width, y, rows, 10U, "SOFT3D", 2U, 0x0000U);
    scaled_line(pixels, width, y, rows, 51U, phase, 2U, phase_color);
    scaled_line(pixels, width, y, rows, 73U, detail, 2U, 0x2104U);
    for (line = 0U; line < sizeof(controls) / sizeof(controls[0]); ++line) {
        scaled_line(pixels, width, y, rows, 103U + line * control_step, controls[line], 2U, 0x0000U);
    }
    scaled_line(pixels, width, y, rows, (unsigned)height - 17U, footer, 1U, 0x4208U);
}

static void text_band(uint16_t *pixels, uint16_t width, uint16_t band_y,
                      uint16_t rows, int x, int y, const char *text, uint16_t color)
{
    for (; *text != '\0' && x < (int)width; ++text, x += 6) {
        unsigned column, bit;
        for (column = 0U; column < 5U; ++column) {
            uint8_t glyph = glyph_column(*text, column);
            int px = x + (int)column;
            if (px < 0 || px >= (int)width) continue;
            for (bit = 0U; bit < 7U; ++bit) {
                int py = y + (int)bit - (int)band_y;
                if (py >= 0 && py < (int)rows && (glyph & (1U << bit)) != 0U) {
                    pixels[(unsigned)py * width + (unsigned)px] = color;
                }
            }
        }
    }
}

void Soft3D_UI_Band(uint16_t *pixels, uint16_t width, uint16_t height,
                    uint16_t y, uint16_t rows, const char *title,
                    const char *mode, const char *metrics, uint8_t paused)
{
    uint16_t row, x;
    if (pixels == NULL || title == NULL || mode == NULL || metrics == NULL ||
        width < 16U || height < 32U || y >= height || rows > height - y) return;
    for (row = 0U; row < rows; ++row) {
        uint16_t screen_y = (uint16_t)(row + y);
        if (screen_y < 16U || screen_y >= height - 16U) {
            uint16_t color = (screen_y == 15U || screen_y == height - 16U) ? 0x4208U : 0x18C3U;
            for (x = 0U; x < width; ++x) pixels[(uint32_t)row * width + x] = color;
        }
    }
    text_band(pixels, width, y, rows, 8, 4, title, 0x07DFU);
    text_band(pixels, width, y, rows, (int)width - 8 - (int)strlen(mode) * 6,
              4, mode, 0xEF7DU);
    text_band(pixels, width, y, rows, 8, (int)height - 11, metrics, 0xEF7DU);
    if (paused != 0U) {
        text_band(pixels, width, y, rows, (int)width - 44, 24, "PAUSED", 0xFDA0U);
    }
}

void Soft3D_UI_MazeLevelBand(uint16_t *pixels, uint16_t width, uint16_t height,
                             uint16_t y, uint16_t rows, const char *state,
                             const char *metrics, uint8_t won,
                             uint32_t level)
{
    unsigned row, x;
    size_t length = 0U;
    char title[16], status[13];
    if (pixels == NULL || state == NULL || metrics == NULL || width < 160U ||
        height < 64U || y >= height || rows > height - y) return;
    if (level < 1U) level = 1U;
    if (level > 9999U) level = 9999U;
    (void)snprintf(title, sizeof(title), "MAZE %02lu", (unsigned long)level);
    while (length < sizeof(status) - 1U && state[length] != '\0') {
        status[length] = state[length];
        ++length;
    }
    status[length] = '\0';
    for (row = 0U; row < rows; ++row) {
        unsigned screen_y = (unsigned)y + row;
        if (screen_y < 32U || screen_y >= (unsigned)height - 16U) {
            uint16_t color = screen_y < 32U && won != 0U ? 0x06A0U : 0x18C3U;
            if (screen_y == 31U || screen_y == (unsigned)height - 16U) color = 0x4208U;
            for (x = 0U; x < width; ++x) pixels[row * width + x] = color;
        }
    }
    scaled_line(pixels, width, y, rows, 4U, title,
                 width >= 224U ? 2U : 1U, won != 0U ? 0xFFFFU : 0x07DFU);
    text_band(pixels, width, y, rows, (int)width - 8 - (int)length * 6,
               4, status, 0xFFFFU);
    if (won != 0U) text_band(pixels, width, y, rows, 8, 22, "LEVEL CLEAR", 0xFFFFU);
    text_band(pixels, width, y, rows, 8, (int)height - 11, metrics, 0xEF7DU);
}

void Soft3D_UI_MazeBand(uint16_t *pixels, uint16_t width, uint16_t height,
                        uint16_t y, uint16_t rows, const char *state,
                        const char *metrics, uint8_t won)
{
    Soft3D_UI_MazeLevelBand(pixels, width, height, y, rows, state, metrics, won, 1U);
}

void Soft3D_UI_ProfileBand(uint16_t *pixels, uint16_t width, uint16_t height,
                            uint16_t y, uint16_t rows, const char *title,
                            const char *mode, const char *timings, const char *metrics)
{
    unsigned row, x;
    size_t length, title_limit;
    char title_text[32], mode_text[13];
    if (pixels == NULL || title == NULL || mode == NULL || timings == NULL ||
        metrics == NULL || width < 240U || height < 64U || y >= height || rows > height - y) return;
    for (row = 0U; row < rows; ++row) {
        unsigned screen_y = (unsigned)y + row;
        if (screen_y < 32U || screen_y >= (unsigned)height - 16U) {
            uint16_t color = screen_y == 31U || screen_y == (unsigned)height - 16U ? 0x4208U : 0x18C3U;
            for (x = 0U; x < width; ++x) pixels[row * width + x] = color;
        }
    }
    (void)snprintf(mode_text, sizeof(mode_text), "%s", mode);
    length = strlen(mode_text);
    title_limit = ((size_t)width - 16U) / 6U - length - 1U;
    if (title_limit >= sizeof(title_text)) title_limit = sizeof(title_text) - 1U;
    (void)snprintf(title_text, sizeof(title_text), "%.*s", (int)title_limit, title);
    text_band(pixels, width, y, rows, 8, 4, title_text, 0x07DFU);
    text_band(pixels, width, y, rows, (int)width - 8 - (int)length * 6, 4, mode_text, 0xFFFFU);
    scaled_line(pixels, width, y, rows, 20U, timings, 1U, 0xEF7DU);
    scaled_line(pixels, width, y, rows, (unsigned)height - 11U, metrics, 1U, 0xEF7DU);
}
