#include "soft3d_motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float s_gravity[3] = {0.0f, 0.0f, 1.0f};
static const float s_zero[3] = {0.0f, 0.0f, 0.0f};

static void check_unit(const float q[4])
{
    unsigned i;
    float sum = 0.0f;
    for (i = 0U; i < 4U; ++i) {
        assert(isfinite(q[i]));
        sum += q[i] * q[i];
    }
    assert(fabsf(sum - 1.0f) < 0.001f);
}

static void settle(Soft3D_Motion *motion, const float accel[3], const float bias[3])
{
    unsigned i;
    Soft3D_MotionInit(motion);
    for (i = 0U; i < 650U; ++i) {
        assert(Soft3D_MotionUpdate(motion, accel, bias, 0.01f));
        check_unit(motion->output.quaternion);
    }
    assert((motion->output.flags & SOFT3D_MOTION_CALIBRATED) != 0U);
    assert((motion->output.flags & SOFT3D_MOTION_INITIALIZING) == 0U);
}

static void test_stationary_calibration(void)
{
    Soft3D_Motion motion;
    Soft3D_MotionOutput output;
    const float bias[3] = {0.18f, -0.12f, 0.25f};
    unsigned i;
    Soft3D_MotionInit(&motion);
    Soft3D_MotionGetOutput(&motion, &output);
    assert(output.quaternion[0] == 1.0f && output.quaternion[1] == 0.0f);
    assert(output.flags == SOFT3D_MOTION_INITIALIZING);
    for (i = 0U; i < 150U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, bias, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) == 0U);
    assert(!Soft3D_MotionUpdate(&motion, NULL, NULL, 0.0f));
    for (i = 0U; i < 70U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, bias, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) == 0U);
    for (i = 0U; i < 140U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, bias, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) != 0U);
    settle(&motion, s_gravity, bias);
    assert((motion.output.flags & SOFT3D_MOTION_STATIONARY) != 0U);
    for (i = 0U; i < 3U; ++i) assert(fabsf(motion.output.gyro_bias_dps[i] - bias[i]) < 0.001f);
    assert(fabsf(motion.output.quaternion[0] - 1.0f) < 0.001f);
}

static void test_motion_does_not_calibrate(void)
{
    Soft3D_Motion motion;
    const float rotating[3] = {0.0f, 0.0f, 15.0f};
    const float accelerating[3] = {0.0f, 0.0f, 1.5f};
    unsigned i;
    Soft3D_MotionInit(&motion);
    for (i = 0U; i < 1000U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, rotating, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) == 0U);
    for (i = 0U; i < 1000U; ++i) assert(Soft3D_MotionUpdate(&motion, accelerating, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) == 0U);
    for (i = 0U; i < 1000U; ++i) {
        float accel[3] = {0.0f, sinf((float)i * 0.002f), cosf((float)i * 0.002f)};
        assert(Soft3D_MotionUpdate(&motion, accel, s_zero, 0.01f));
    }
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) == 0U);
    for (i = 0U; i < 650U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) != 0U);
}

static void test_tilt_and_rotation(void)
{
    Soft3D_Motion motion;
    const float tilt[3] = {0.0f, 0.70710678f, 0.70710678f};
    const float spin[3] = {0.0f, 0.0f, 90.0f};
    unsigned i;
    settle(&motion, tilt, s_zero);
    assert(fabsf(motion.output.quaternion[0] - 0.92387953f) < 0.002f);
    assert(fabsf(motion.output.quaternion[1] - 0.38268343f) < 0.002f);
    settle(&motion, s_gravity, s_zero);
    for (i = 0U; i < 100U; ++i) {
        assert(Soft3D_MotionUpdate(&motion, s_gravity, spin, i % 2U == 0U ? 0.009f : 0.011f));
    }
    assert(fabsf(motion.output.quaternion[0] - 0.70710678f) < 0.002f);
    assert(fabsf(motion.output.quaternion[3] - 0.70710678f) < 0.002f);
    assert((motion.output.flags & SOFT3D_MOTION_STATIONARY) == 0U);
    check_unit(motion.output.quaternion);
}

static void test_invalid_samples_hold_attitude(void)
{
    Soft3D_Motion motion;
    float before[4];
    const float invalid_dt[] = {0.0f, -0.01f, 0.1f, NAN, INFINITY};
    const float nan_accel[3] = {NAN, 0.0f, 1.0f};
    const float inf_gyro[3] = {0.0f, INFINITY, 0.0f};
    unsigned i;
    settle(&motion, s_gravity, s_zero);
    memcpy(before, motion.output.quaternion, sizeof(before));
    for (i = 0U; i < sizeof(invalid_dt) / sizeof(invalid_dt[0]); ++i) {
        assert(!Soft3D_MotionUpdate(&motion, s_gravity, s_zero, invalid_dt[i]));
        assert(memcmp(before, motion.output.quaternion, sizeof(before)) == 0);
        assert((motion.output.flags & SOFT3D_MOTION_INVALID_SAMPLE) != 0U);
    }
    assert(!Soft3D_MotionUpdate(&motion, nan_accel, s_zero, 0.01f));
    assert(!Soft3D_MotionUpdate(&motion, s_gravity, inf_gyro, 0.01f));
    assert(!Soft3D_MotionUpdate(&motion, NULL, s_zero, 0.01f));
    assert(!Soft3D_MotionUpdate(NULL, s_gravity, s_zero, 0.01f));
    assert(memcmp(before, motion.output.quaternion, sizeof(before)) == 0);
    assert(Soft3D_MotionUpdate(&motion, s_gravity, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_INVALID_SAMPLE) == 0U);
}

static void test_acceleration_rejection_and_fallback(void)
{
    Soft3D_Motion motion;
    const float sideways_gravity[3] = {0.5f, 0.0f, 0.8660254f};
    const float strong_accel[3] = {2.0f, 0.0f, 1.0f};
    const float spin[3] = {0.0f, 0.0f, 90.0f};
    unsigned i;
    settle(&motion, s_gravity, s_zero);
    assert(Soft3D_MotionUpdate(&motion, sideways_gravity, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_ACCEL_REJECTED) != 0U);
    assert(Soft3D_MotionUpdate(&motion, strong_accel, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_ACCEL_REJECTED) != 0U);
    for (i = 0U; i < 100U; ++i) assert(Soft3D_MotionUpdate(&motion, s_zero, spin, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_ACCEL_REJECTED) != 0U);
    assert(fabsf(motion.output.quaternion[3] - 0.70710678f) < 0.002f);
    assert((motion.output.flags & SOFT3D_MOTION_CALIBRATED) != 0U);
    check_unit(motion.output.quaternion);
    assert(Soft3D_MotionUpdate(&motion, s_gravity, s_zero, 0.01f));
    assert((motion.output.flags & SOFT3D_MOTION_ACCEL_REJECTED) == 0U);
}

static void test_adaptive_bias(void)
{
    Soft3D_Motion motion;
    const float changed_bias[3] = {0.1f, -0.08f, 0.12f};
    const float bad_accel[3] = {0.0f, 0.0f, 2.0f};
    unsigned i;
    settle(&motion, s_gravity, s_zero);
    for (i = 0U; i < 1000U; ++i) assert(Soft3D_MotionUpdate(&motion, bad_accel, changed_bias, 0.01f));
    assert(fabsf(motion.output.gyro_bias_dps[0]) < 0.001f);
    for (i = 0U; i < 5000U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, changed_bias, 0.01f));
    for (i = 0U; i < 3U; ++i) assert(fabsf(motion.output.gyro_bias_dps[i] - changed_bias[i]) < 0.005f);
}

static void test_adaptive_bias_waits_again_after_gap(void)
{
    Soft3D_Motion motion;
    const float new_bias[3] = {0.1f, 0.0f, 0.0f};
    unsigned i;
    settle(&motion, s_gravity, s_zero);
    assert(!Soft3D_MotionUpdate(&motion, NULL, NULL, 0.0f));
    for (i = 0U; i < 250U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, new_bias, 0.01f));
    assert(fabsf(motion.output.gyro_bias_dps[0]) < 0.000001f);
    for (i = 0U; i < 100U; ++i) assert(Soft3D_MotionUpdate(&motion, s_gravity, new_bias, 0.01f));
    assert(motion.output.gyro_bias_dps[0] > 0.001f);
}

int main(void)
{
    test_stationary_calibration();
    test_motion_does_not_calibrate();
    test_tilt_and_rotation();
    test_invalid_samples_hold_attitude();
    test_acceleration_rejection_and_fallback();
    test_adaptive_bias();
    test_adaptive_bias_waits_again_after_gap();
    puts("motion: all tests passed");
    return 0;
}
