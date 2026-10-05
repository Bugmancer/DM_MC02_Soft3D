#include "soft3d_profile.h"

#include <stddef.h>
#include <string.h>

static void current_snapshot(const Soft3D_ProfileState *state, Soft3D_ProfileSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->samples = state->count;
    if (state->count == 0U) return;
    snapshot->frame_mean_us = (uint32_t)(state->frame_sum / state->count);
    snapshot->frame_max_us = state->frame_max;
    snapshot->geometry_mean_us = (uint32_t)(state->geometry_sum / state->count);
    snapshot->raster_mean_us = (uint32_t)(state->raster_sum / state->count);
    snapshot->transport_mean_us = (uint32_t)(state->transport_sum / state->count);
    snapshot->candidate_triangles = (uint32_t)(state->candidate_sum / state->count);
    snapshot->potential_triangles = (uint32_t)(state->potential_sum / state->count);
}

void Soft3D_ProfileInit(Soft3D_ProfileState *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void Soft3D_ProfilePush(Soft3D_ProfileState *state, const Soft3D_ProfileSample *sample)
{
    unsigned i;
    if (state == NULL || sample == NULL) return;
    state->frame_us[state->count] = sample->frame_us;
    state->frame_sum += sample->frame_us;
    state->geometry_sum += sample->geometry_us;
    state->raster_sum += sample->raster_us;
    state->transport_sum += sample->transport_us;
    state->candidate_sum += sample->candidate_triangles;
    state->potential_sum += sample->potential_triangles;
    if (sample->frame_us > state->frame_max) state->frame_max = sample->frame_us;
    ++state->count;
    if (state->count != SOFT3D_PROFILE_WINDOW_SIZE) return;

    current_snapshot(state, &state->published);
    /* Finished samples can be sorted in place; the next window overwrites them. */
    for (i = 1U; i < SOFT3D_PROFILE_WINDOW_SIZE; ++i) {
        uint32_t value = state->frame_us[i];
        unsigned j = i;
        while (j > 0U && state->frame_us[j - 1U] > value) {
            state->frame_us[j] = state->frame_us[j - 1U];
            --j;
        }
        state->frame_us[j] = value;
    }
    state->published.frame_p95_us = state->frame_us[60U];
    state->count = state->frame_max = 0U;
    state->frame_sum = state->geometry_sum = state->raster_sum = state->transport_sum = 0U;
    state->candidate_sum = state->potential_sum = 0U;
}

void Soft3D_ProfileGet(const Soft3D_ProfileState *state, Soft3D_ProfileSnapshot *snapshot)
{
    Soft3D_ProfileSnapshot result;
    if (snapshot == NULL) return;
    memset(&result, 0, sizeof(result));
    if (state != NULL) {
        if (state->published.samples != 0U) result = state->published;
        else current_snapshot(state, &result);
    }
    *snapshot = result;
}

