#include "soft3d_damage.h"

#include <stddef.h>

uint16_t Soft3D_GeometryBands(const Soft3D_Context *ctx)
{
    uint16_t mask = 0U;
    unsigned i;
    if (ctx == NULL || ctx->frame_valid == 0U || ctx->width != 280U ||
        ctx->height != 240U || ctx->triangle_count > SOFT3D_MAX_TRIANGLES) {
        return SOFT3D_ALL_BANDS;
    }
    for (i = 0U; i < ctx->triangle_count; ++i) {
        const Soft3D_Triangle *triangle = &ctx->triangles[i];
        unsigned first, last, band;
        if (triangle->min_y > triangle->max_y || triangle->max_y >= ctx->height) {
            return SOFT3D_ALL_BANDS;
        }
        first = triangle->min_y / SOFT3D_BAND_ROWS;
        last = triangle->max_y / SOFT3D_BAND_ROWS;
        for (band = first; band <= last; ++band) mask |= (uint16_t)(1U << band);
    }
    return mask;
}

uint16_t Soft3D_DamageBands(uint16_t previous_geometry, uint16_t current_geometry,
                          bool full_refresh, bool pause_overlay)
{
    uint16_t mask;
    if (full_refresh) return SOFT3D_ALL_BANDS;
    mask = (uint16_t)((previous_geometry | current_geometry | 0x4001U) & SOFT3D_ALL_BANDS);
    if (pause_overlay) mask |= 0x0002U;
    return mask;
}
