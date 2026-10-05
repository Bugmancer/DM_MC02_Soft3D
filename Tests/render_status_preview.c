#include "soft3d_ui.h"
#include "soft3d_boot.h"

#include <stdio.h>
#include <stdlib.h>

#define WIDTH 280U
#define HEIGHT 240U
#define ROWS 16U

static uint16_t pixels[WIDTH * ROWS];

int main(int argc, char **argv)
{
    static const char *const phases[] = {"RTOS STARTING", "LCD POLLING OK", "RENDER ERROR"};
    static const char *const details[] = {"SPI POLLING", "IMU CALIBRATING", "RASTER FAILED"};
    unsigned state = 1U, y;
    FILE *output;
    if (argc < 2 || argc > 3) return 1;
    if (argc == 3) {
        int parsed = atoi(argv[2]);
        if (parsed < 0 || parsed > 2) return 1;
        state = (unsigned)parsed;
    }
    output = fopen(argv[1], "wb");
    if (output == NULL) return 1;
    fprintf(output, "P6\n%u %u\n255\n", WIDTH, HEIGHT);
    for (y = 0U; y < HEIGHT; y += ROWS) {
        unsigned i;
        Soft3D_UI_StatusBand(pixels, WIDTH, HEIGHT, (uint16_t)y, ROWS,
                             phases[state], details[state], SOFT3D_DISPLAY_BUILD);
        for (i = 0U; i < WIDTH * ROWS; ++i) {
            uint16_t color = pixels[i];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)(((color >> 11) & 31U) * 255U / 31U);
            rgb[1] = (unsigned char)(((color >> 5) & 63U) * 255U / 63U);
            rgb[2] = (unsigned char)((color & 31U) * 255U / 31U);
            if (fwrite(rgb, 1, sizeof(rgb), output) != sizeof(rgb)) {
                fclose(output);
                return 1;
            }
        }
    }
    return fclose(output) == 0 ? 0 : 1;
}
