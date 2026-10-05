#ifndef SOFT3D_MOTION_H
#define SOFT3D_MOTION_H

#include "FusionAhrs.h"
#include "FusionBias.h"
#include <stdbool.h>
#include <stdint.h>

#define SOFT3D_MOTION_CALIBRATED (1UL << 0)
#define SOFT3D_MOTION_STATIONARY (1UL << 1)
#define SOFT3D_MOTION_ACCEL_REJECTED (1UL << 2)
#define SOFT3D_MOTION_INITIALIZING (1UL << 3)
#define SOFT3D_MOTION_INVALID_SAMPLE (1UL << 4)

typedef struct {
    /* Sensor-to-Earth NWU rotation, quaternion order w, x, y, z. */
    float quaternion[4];
    float gyro_bias_dps[3];
    uint32_t flags;
} Soft3D_MotionOutput;

typedef struct {
    FusionAhrs ahrs;
    FusionBias bias;
    Soft3D_MotionOutput output;
    float calibration_seconds;
    float calibration_mean[3];
    float calibration_m2[3];
    float calibration_accel[3];
    float previous_accel[3];
    uint32_t calibration_samples;
    bool previous_accel_valid;
} Soft3D_Motion;

/* Identity quaternion, zero bias, INITIALIZING; keep this state in one task. */
void Soft3D_MotionInit(Soft3D_Motion *motion);
/* Measured dt in seconds, nominal 0.01, accepted 0.005..0.02.
 * Invalid input holds the last finite attitude and returns false. A zero or
 * out-of-gravity-range accelerometer falls back to gyro-only integration.
 * Initial bias requires 2 seconds of quiet data; yaw has no absolute reference.
 */
bool Soft3D_MotionUpdate(Soft3D_Motion *motion, const float accel_g[3],
                        const float gyro_dps[3], float dt);
void Soft3D_MotionGetOutput(const Soft3D_Motion *motion, Soft3D_MotionOutput *output);

#endif
