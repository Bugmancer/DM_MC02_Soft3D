#ifndef SOFT3D_DAMAGE_H
#define SOFT3D_DAMAGE_H

#include "soft3d.h"

#include <stdbool.h>
#include <stdint.h>

#define SOFT3D_BAND_ROWS 16U
#define SOFT3D_ALL_BANDS 0x7FFFU

/* Covers every prepared triangle on the fixed 280x240 display. Invalid context
 * or bounds conservatively request all bands; an empty valid frame returns 0.
 */
uint16_t Soft3D_GeometryBands(const Soft3D_Context *ctx);
/* pause_overlay must be true if either the displayed or next frame is paused.
 * A display reset or background-color change requires full_refresh.
 */
uint16_t Soft3D_DamageBands(uint16_t previous_geometry, uint16_t current_geometry,
                          bool full_refresh, bool pause_overlay);

#endif
