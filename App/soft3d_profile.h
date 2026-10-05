#ifndef SOFT3D_PROFILE_H
#define SOFT3D_PROFILE_H

#include <stdint.h>

#define SOFT3D_PROFILE_WINDOW_SIZE 64U

typedef struct {
    uint32_t frame_us;
    uint32_t geometry_us;
    uint32_t raster_us;
    uint32_t transport_us;
    uint32_t candidate_triangles;
    uint32_t potential_triangles;
} Soft3D_ProfileSample;

typedef struct {
    uint32_t samples;
    uint32_t frame_mean_us;
    uint32_t frame_p95_us;
    uint32_t frame_max_us;
    uint32_t geometry_mean_us;
    uint32_t raster_mean_us;
    uint32_t transport_mean_us;
    /* Integer per-frame means, not window totals. */
    uint32_t candidate_triangles;
    uint32_t potential_triangles;
} Soft3D_ProfileSnapshot;

typedef struct {
    uint32_t frame_us[SOFT3D_PROFILE_WINDOW_SIZE];
    uint64_t frame_sum, geometry_sum, raster_sum, transport_sum;
    uint64_t candidate_sum, potential_sum;
    uint32_t count, frame_max;
    Soft3D_ProfileSnapshot published;
} Soft3D_ProfileState;

void Soft3D_ProfileInit(Soft3D_ProfileState *state);
void Soft3D_ProfilePush(Soft3D_ProfileState *state, const Soft3D_ProfileSample *sample);
/* Returns the latest completed, non-overlapping 64-frame window. Before the
 * first completion, returns current progress with p95=0. Means round down;
 * full-window p95 uses nearest rank (61st sorted frame). NULL state gives zero.
 */
void Soft3D_ProfileGet(const Soft3D_ProfileState *state, Soft3D_ProfileSnapshot *snapshot);

#endif
