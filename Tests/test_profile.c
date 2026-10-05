#include "soft3d_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Soft3D_ProfileSample uniform_sample(uint32_t value)
{
    Soft3D_ProfileSample sample = {value, value, value, value, value, value};
    return sample;
}

static void assert_uniform(const Soft3D_ProfileSnapshot *snapshot, uint32_t count,
                           uint32_t value, uint32_t p95)
{
    assert(snapshot->samples == count);
    assert(snapshot->frame_mean_us == value && snapshot->frame_max_us == value);
    assert(snapshot->frame_p95_us == p95);
    assert(snapshot->geometry_mean_us == value && snapshot->raster_mean_us == value);
    assert(snapshot->transport_mean_us == value);
    assert(snapshot->candidate_triangles == value && snapshot->potential_triangles == value);
}

static void test_partial_and_null(void)
{
    Soft3D_ProfileState state, saved;
    Soft3D_ProfileSnapshot snapshot;
    const Soft3D_ProfileSample first = {100U, 200U, 300U, 400U, 500U, 600U};
    const Soft3D_ProfileSample second = {101U, 203U, 305U, 407U, 509U, 611U};
    Soft3D_ProfileInit(&state);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 0U, 0U, 0U);
    Soft3D_ProfilePush(&state, &first);
    Soft3D_ProfileGet(&state, &snapshot);
    assert(snapshot.samples == 1U && snapshot.frame_mean_us == 100U);
    assert(snapshot.frame_max_us == 100U && snapshot.frame_p95_us == 0U);
    assert(snapshot.geometry_mean_us == 200U && snapshot.raster_mean_us == 300U);
    assert(snapshot.transport_mean_us == 400U);
    assert(snapshot.candidate_triangles == 500U && snapshot.potential_triangles == 600U);
    Soft3D_ProfilePush(&state, &second);
    Soft3D_ProfileGet(&state, &snapshot);
    assert(snapshot.samples == 2U && snapshot.frame_mean_us == 100U);
    assert(snapshot.frame_max_us == 101U && snapshot.frame_p95_us == 0U);
    assert(snapshot.geometry_mean_us == 201U && snapshot.raster_mean_us == 302U);
    assert(snapshot.transport_mean_us == 403U);
    assert(snapshot.candidate_triangles == 504U && snapshot.potential_triangles == 605U);
    saved = state;
    Soft3D_ProfileInit(NULL);
    Soft3D_ProfilePush(NULL, &first);
    Soft3D_ProfilePush(&state, NULL);
    Soft3D_ProfileGet(&state, NULL);
    assert(memcmp(&state, &saved, sizeof(state)) == 0);
    Soft3D_ProfileGet(NULL, &snapshot);
    assert_uniform(&snapshot, 0U, 0U, 0U);
}

static void test_known_window(void)
{
    Soft3D_ProfileState state;
    Soft3D_ProfileSnapshot snapshot;
    unsigned i;
    Soft3D_ProfileInit(&state);
    for (i = 0U; i < 64U; ++i) {
        uint32_t value = (i * 17U) % 64U + 1U;
        Soft3D_ProfileSample sample = {value, value * 2U, value * 3U, value * 4U,
                                       value * 5U, value * 7U};
        Soft3D_ProfilePush(&state, &sample);
        if (i == 62U) {
            Soft3D_ProfileGet(&state, &snapshot);
            assert(snapshot.samples == 63U && snapshot.frame_p95_us == 0U);
        }
    }
    Soft3D_ProfileGet(&state, &snapshot);
    assert(snapshot.samples == 64U && snapshot.frame_mean_us == 32U);
    assert(snapshot.frame_p95_us == 61U && snapshot.frame_max_us == 64U);
    assert(snapshot.geometry_mean_us == 65U && snapshot.raster_mean_us == 97U);
    assert(snapshot.transport_mean_us == 130U);
    assert(snapshot.candidate_triangles == 162U && snapshot.potential_triangles == 227U);
}

static void test_window_isolation_and_reset(void)
{
    Soft3D_ProfileState state;
    Soft3D_ProfileSnapshot snapshot, completed;
    Soft3D_ProfileSample sample = uniform_sample(100U);
    unsigned i;
    Soft3D_ProfileInit(&state);
    for (i = 0U; i < 64U; ++i) Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &completed);
    assert_uniform(&completed, 64U, 100U, 100U);
    sample = uniform_sample(1000U);
    for (i = 0U; i < 63U; ++i) {
        Soft3D_ProfilePush(&state, &sample);
        Soft3D_ProfileGet(&state, &snapshot);
        assert(memcmp(&snapshot, &completed, sizeof(snapshot)) == 0);
    }
    Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 64U, 1000U, 1000U);
    sample = uniform_sample(0U);
    Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 64U, 1000U, 1000U);
    Soft3D_ProfileInit(&state);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 0U, 0U, 0U);
    sample = uniform_sample(7U);
    Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 1U, 7U, 0U);
    Soft3D_ProfileInit(&state);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 0U, 0U, 0U);
}

static void test_limits_and_rank(void)
{
    Soft3D_ProfileState state;
    Soft3D_ProfileSnapshot snapshot;
    Soft3D_ProfileSample sample = uniform_sample(UINT32_MAX);
    unsigned i;
    Soft3D_ProfileInit(&state);
    for (i = 0U; i < 63U; ++i) Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 63U, UINT32_MAX, 0U);
    Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 64U, UINT32_MAX, UINT32_MAX);
    sample = uniform_sample(0U);
    for (i = 0U; i < 64U; ++i) Soft3D_ProfilePush(&state, &sample);
    Soft3D_ProfileGet(&state, &snapshot);
    assert_uniform(&snapshot, 64U, 0U, 0U);
    for (i = 0U; i < 64U; ++i) {
        sample = uniform_sample(i < 60U ? 0U : UINT32_MAX);
        Soft3D_ProfilePush(&state, &sample);
    }
    Soft3D_ProfileGet(&state, &snapshot);
    assert(snapshot.frame_p95_us == UINT32_MAX && snapshot.frame_max_us == UINT32_MAX);
    assert(snapshot.frame_mean_us == (uint32_t)((uint64_t)UINT32_MAX * 4U / 64U));
    for (i = 0U; i < 64U; ++i) {
        sample = uniform_sample(i < 61U ? 0U : UINT32_MAX);
        Soft3D_ProfilePush(&state, &sample);
    }
    Soft3D_ProfileGet(&state, &snapshot);
    assert(snapshot.frame_p95_us == 0U && snapshot.frame_max_us == UINT32_MAX);
}

int main(void)
{
    test_partial_and_null();
    test_known_window();
    test_window_isolation_and_reset();
    test_limits_and_rank();
    assert(sizeof(Soft3D_ProfileState) <= 384U);
    printf("profile: 64-frame windows, partial progress, nearest-rank p95, overflow limits and reset passed; state=%lu bytes\n",
           (unsigned long)sizeof(Soft3D_ProfileState));
    return 0;
}
