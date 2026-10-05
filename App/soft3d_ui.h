#ifndef SOFT3D_UI_H
#define SOFT3D_UI_H

#include <stdint.h>

/* Draw into a host-endian RGB565 band; coordinates refer to the whole screen. */
void Soft3D_UI_Band(uint16_t *pixels, uint16_t width, uint16_t height,
                    uint16_t y, uint16_t rows, const char *title,
                    const char *mode, const char *metrics, uint8_t paused);

/* Standalone bright status/help screen, independent of the 3D renderer or RTOS.
 * Fills the complete band. Intended for 280x240; minimum size is 160x224.
 * Phase/detail use 2x font (22 characters at 280px); long labels are ellipsized.
 * Footer uses 1x font for the firmware tag or diagnostic counters.
 */
void Soft3D_UI_StatusBand(uint16_t *pixels, uint16_t width, uint16_t height,
                          uint16_t y, uint16_t rows, const char *phase,
                          const char *detail, const char *footer);

/* Fixed overlay coverage for 280x240/16-row bands: top 32 and bottom 16 rows. */
#define SOFT3D_MAZE_UI_BANDS 0x4003U
void Soft3D_UI_MazeBand(uint16_t *pixels, uint16_t width, uint16_t height,
                        uint16_t y, uint16_t rows, const char *state,
                        const char *metrics, uint8_t won);
void Soft3D_UI_MazeLevelBand(uint16_t *pixels, uint16_t width, uint16_t height,
                             uint16_t y, uint16_t rows, const char *state,
                             const char *metrics, uint8_t won,
                             uint32_t level);

void Soft3D_UI_ProfileBand(uint16_t *pixels, uint16_t width, uint16_t height,
                            uint16_t y, uint16_t rows, const char *title,
                            const char *mode, const char *timings, const char *metrics);

#endif
