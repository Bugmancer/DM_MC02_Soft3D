#include "soft3d_motion.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const FusionAhrsSettings s_ahrs_settings = {
    100.0f, FusionConventionNwu, 0.5f, 2000.0f, 10.0f, 0.0f, 5.0f
};
static const FusionBiasSettings s_bias_settings = {100.0f, 0.5f, 3.0f};

static bool within(float value, float minimum, float maximum)
{
    /* Ordered comparisons reject NaN as well as infinities. */
    return value >= minimum && value <= maximum;
}

static float norm_squared(const float vector[3])
{
    return vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
}

static void reset_calibration(Soft3D_Motion *motion)
{
    motion->calibration_seconds = 0.0f;
    motion->calibration_samples = 0U;
    memset(motion->calibration_mean, 0, sizeof(motion->calibration_mean));
    memset(motion->calibration_m2, 0, sizeof(motion->calibration_m2));
}

void Soft3D_MotionInit(Soft3D_Motion *motion)
{
    if (motion == NULL) return;
    memset(motion, 0, sizeof(*motion));
    FusionAhrsInitialise(&motion->ahrs);
    FusionAhrsSetSettings(&motion->ahrs, &s_ahrs_settings);
    FusionBiasInitialise(&motion->bias);
    FusionBiasSetSettings(&motion->bias, &s_bias_settings);
    motion->output.quaternion[0] = 1.0f;
    motion->output.flags = SOFT3D_MOTION_INITIALIZING;
}

static bool initial_calibration(Soft3D_Motion *motion, const float accel[3],
                                 const float gyro[3], float dt)
{
    unsigned axis;
    float accel_norm = norm_squared(accel);
    if (!within(accel_norm, 0.95f * 0.95f, 1.05f * 1.05f) ||
        norm_squared(gyro) > 2.0f * 2.0f) {
        reset_calibration(motion);
        return false;
    }
    if (motion->calibration_samples != 0U) {
        for (axis = 0U; axis < 3U; ++axis) {
            if (fabsf(accel[axis] - motion->calibration_accel[axis]) > 0.02f ||
                fabsf(gyro[axis] - motion->calibration_mean[axis]) > 0.5f) {
                reset_calibration(motion);
                return false;
            }
        }
    } else {
        memcpy(motion->calibration_accel, accel, sizeof(motion->calibration_accel));
    }
    ++motion->calibration_samples;
    motion->calibration_seconds += dt;
    for (axis = 0U; axis < 3U; ++axis) {
        float delta = gyro[axis] - motion->calibration_mean[axis];
        motion->calibration_mean[axis] += delta / (float)motion->calibration_samples;
        motion->calibration_m2[axis] += delta * (gyro[axis] - motion->calibration_mean[axis]);
    }
    motion->output.flags |= SOFT3D_MOTION_STATIONARY;
    if (motion->calibration_seconds < 2.0f || motion->calibration_samples < 100U) return false;
    for (axis = 0U; axis < 3U; ++axis) {
        float variance = motion->calibration_m2[axis] / (float)(motion->calibration_samples - 1U);
        if (variance > 0.15f * 0.15f) {
            reset_calibration(motion);
            motion->output.flags &= ~SOFT3D_MOTION_STATIONARY;
            return false;
        }
    }
    {
        FusionVector offset;
        memcpy(offset.array, motion->calibration_mean, sizeof(offset.array));
        FusionBiasSetOffset(&motion->bias, offset);
    }
    motion->output.flags |= SOFT3D_MOTION_CALIBRATED;
    return true;
}

static FusionVector corrected_gyro(Soft3D_Motion *motion, FusionVector gyro,
                                    const float accel[3], bool accel_usable)
{
    FusionVector offset = FusionBiasGetOffset(&motion->bias);
    FusionVector corrected = FusionVectorSubtract(gyro, offset);
    float accel_norm = norm_squared(accel);
    bool stationary = accel_usable && motion->previous_accel_valid &&
        within(accel_norm, 0.97f * 0.97f, 1.03f * 1.03f) &&
        norm_squared(corrected.array) <= 0.5f * 0.5f;
    unsigned axis;
    for (axis = 0U; axis < 3U; ++axis) {
        if (fabsf(accel[axis] - motion->previous_accel[axis]) > 0.02f) stationary = false;
    }
    if (stationary) {
        motion->output.flags |= SOFT3D_MOTION_STATIONARY;
        return FusionBiasUpdate(&motion->bias, gyro);
    }
    /* Reset only the stationary timer; preserve the already learned bias. */
    FusionBiasInitialise(&motion->bias);
    FusionBiasSetSettings(&motion->bias, &s_bias_settings);
    FusionBiasSetOffset(&motion->bias, offset);
    return corrected;
}

static bool invalid_sample(Soft3D_Motion *motion)
{
    motion->output.flags &= ~(SOFT3D_MOTION_STATIONARY | SOFT3D_MOTION_ACCEL_REJECTED);
    motion->output.flags |= SOFT3D_MOTION_INVALID_SAMPLE;
    motion->previous_accel_valid = false;
    if ((motion->output.flags & SOFT3D_MOTION_CALIBRATED) == 0U) reset_calibration(motion);
    return false;
}

bool Soft3D_MotionUpdate(Soft3D_Motion *motion, const float accel_g[3],
                        const float gyro_dps[3], float dt)
{
    FusionVector accel, gyro;
    FusionQuaternion quaternion;
    FusionAhrsFlags flags;
    FusionAhrsInternalStates internal;
    float accel_norm;
    bool accel_usable;
    unsigned axis;
    if (motion == NULL) return false;
    if (accel_g == NULL || gyro_dps == NULL || !within(dt, 0.005f, 0.02f)) {
        return invalid_sample(motion);
    }
    for (axis = 0U; axis < 3U; ++axis) {
        if (!within(accel_g[axis], -6.01f, 6.01f) || !within(gyro_dps[axis], -2000.1f, 2000.1f)) {
            return invalid_sample(motion);
        }
    }
    motion->output.flags &= SOFT3D_MOTION_CALIBRATED;
    if ((motion->output.flags & SOFT3D_MOTION_CALIBRATED) == 0U) {
        motion->output.flags |= SOFT3D_MOTION_INITIALIZING;
        if (!initial_calibration(motion, accel_g, gyro_dps, dt)) return true;
    }

    accel_norm = norm_squared(accel_g);
    accel_usable = within(accel_norm, 0.75f * 0.75f, 1.25f * 1.25f);
    memcpy(gyro.array, gyro_dps, sizeof(gyro.array));
    memcpy(accel.array, accel_g, sizeof(accel.array));
    gyro = corrected_gyro(motion, gyro, accel_g, accel_usable);
    memcpy(motion->previous_accel, accel_g, sizeof(motion->previous_accel));
    motion->previous_accel_valid = accel_usable;
    if (!accel_usable) accel = FUSION_VECTOR_ZERO;
    FusionAhrsSetSamplePeriod(&motion->ahrs, dt);
    FusionAhrsUpdateNoMagnetometer(&motion->ahrs, gyro, accel);
    quaternion = FusionAhrsGetQuaternion(&motion->ahrs);
    for (axis = 0U; axis < 4U; ++axis) {
        if (!within(quaternion.array[axis], -1.001f, 1.001f)) {
            /* A numeric failure restarts calibration rather than publishing NaN. */
            Soft3D_MotionInit(motion);
            return invalid_sample(motion);
        }
    }
    memcpy(motion->output.quaternion, quaternion.array, sizeof(quaternion.array));
    gyro = FusionBiasGetOffset(&motion->bias);
    memcpy(motion->output.gyro_bias_dps, gyro.array, sizeof(gyro.array));
    flags = FusionAhrsGetFlags(&motion->ahrs);
    internal = FusionAhrsGetInternalStates(&motion->ahrs);
    if (flags.startup) motion->output.flags |= SOFT3D_MOTION_INITIALIZING;
    if (!accel_usable || internal.accelerometerIgnored) {
        motion->output.flags |= SOFT3D_MOTION_ACCEL_REJECTED;
    }
    return true;
}

void Soft3D_MotionGetOutput(const Soft3D_Motion *motion, Soft3D_MotionOutput *output)
{
    if (motion != NULL && output != NULL) *output = motion->output;
}
