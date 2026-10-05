#ifndef SOFT3D_IMU_H
#define SOFT3D_IMU_H

#include <stdbool.h>
#include <stdint.h>

#define SOFT3D_IMU_ACCEL_VALID 1U
#define SOFT3D_IMU_GYRO_VALID 2U
#define SOFT3D_IMU_SATURATED 4U

typedef enum {
    SOFT3D_IMU_NOT_INITIALIZED = 0,
    SOFT3D_IMU_READY,
    SOFT3D_IMU_NO_ACCEL,
    SOFT3D_IMU_NO_GYRO,
    SOFT3D_IMU_SPI_ERROR,
    SOFT3D_IMU_CONFIG_ERROR
} Soft3D_IMUStatus;

typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    uint32_t flags;
} Soft3D_IMUSample;

/* One task owns SPI2. Call Init after the RTOS scheduler starts. Heater stays off. */
bool Soft3D_IMU_Init(void);
/* Both sensors run at 200 Hz; poll at 100 Hz. No DRDY interrupt is required.
 * False clears the complete sample, invalidates initialization, and requires retry.
 * The two dies are polled sequentially, not hardware-synchronised.
 */
bool Soft3D_IMU_Read(Soft3D_IMUSample *sample);
uint32_t Soft3D_IMU_Status(void);

#endif
