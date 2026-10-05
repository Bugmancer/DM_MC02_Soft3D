#include "soft3d_imu.h"
#include "main.h"
#include "spi.h"
#include "cmsis_os2.h"

#include <stddef.h>
#include <string.h>

#define IMU_TIMEOUT_MS 2U
#define ACC_ID 0x1EU
#define GYRO_ID 0x0FU

static Soft3D_IMUStatus s_status = SOFT3D_IMU_NOT_INITIALIZED;

static void imu_delay(uint32_t milliseconds)
{
    uint32_t ticks = (uint32_t)(((uint64_t)milliseconds * osKernelGetTickFreq() + 999U) / 1000U);
    /* osDelay may start just before a tick; keep the datasheet minimum delay. */
    (void)osDelay(ticks + 1U);
}

static bool imu_transfer(bool accelerometer, uint8_t *tx, uint8_t *rx, uint16_t length)
{
    HAL_StatusTypeDef result;
    HAL_GPIO_WritePin(accelerometer ? ACC_CS_GPIO_Port : GYRO_CS_GPIO_Port,
                      accelerometer ? ACC_CS_Pin : GYRO_CS_Pin, GPIO_PIN_RESET);
    result = HAL_SPI_TransmitReceive(&hspi2, tx, rx, length, IMU_TIMEOUT_MS);
    HAL_GPIO_WritePin(accelerometer ? ACC_CS_GPIO_Port : GYRO_CS_GPIO_Port,
                      accelerometer ? ACC_CS_Pin : GYRO_CS_Pin, GPIO_PIN_SET);
    if (result != HAL_OK) {
        s_status = SOFT3D_IMU_SPI_ERROR;
        return false;
    }
    return true;
}

static bool imu_read(bool accelerometer, uint8_t address, uint8_t *data, uint16_t length)
{
    uint8_t tx[10] = {0U};
    uint8_t rx[10];
    uint16_t prefix = accelerometer ? 2U : 1U;
    if (length > sizeof(tx) - prefix) return false;
    tx[0] = (uint8_t)(address | 0x80U);
    if (!imu_transfer(accelerometer, tx, rx, (uint16_t)(length + prefix))) return false;
    /* Acceleration SPI has one dummy byte after the address; gyro has none. */
    memcpy(data, rx + prefix, length);
    return true;
}

static bool imu_write(bool accelerometer, uint8_t address, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];
    tx[0] = (uint8_t)(address & 0x7FU);
    tx[1] = value;
    return imu_transfer(accelerometer, tx, rx, sizeof(tx));
}

static bool imu_configure(bool accelerometer, uint8_t address, uint8_t value,
                           uint8_t mask, uint32_t settling_ms)
{
    uint8_t actual;
    if (!imu_write(accelerometer, address, value)) return false;
    imu_delay(settling_ms);
    if (!imu_read(accelerometer, address, &actual, 1U)) return false;
    if ((actual & mask) != (value & mask)) {
        s_status = SOFT3D_IMU_CONFIG_ERROR;
        return false;
    }
    return true;
}

static bool imu_check_id(bool accelerometer)
{
    uint8_t id;
    if (!imu_read(accelerometer, 0x00U, &id, 1U)) return false;
    if (id != (accelerometer ? ACC_ID : GYRO_ID)) {
        s_status = accelerometer ? SOFT3D_IMU_NO_ACCEL : SOFT3D_IMU_NO_GYRO;
        return false;
    }
    return true;
}

bool Soft3D_IMU_Init(void)
{
    uint8_t ignored;
    s_status = SOFT3D_IMU_NOT_INITIALIZED;
    HAL_GPIO_WritePin(ACC_CS_GPIO_Port, ACC_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GYRO_CS_GPIO_Port, GYRO_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(IMU_HEATER_GPIO_Port, IMU_HEATER_Pin, GPIO_PIN_RESET);
    if (osThreadGetId() == NULL || osKernelGetTickFreq() == 0U) return false;
    imu_delay(50U);

    /* The first accelerometer transaction after power/reset selects SPI mode. */
    if (!imu_read(true, 0x00U, &ignored, 1U)) return false;
    imu_delay(1U);
    if (!imu_check_id(true) || !imu_check_id(false)) return false;
    if (!imu_write(true, 0x7EU, 0xB6U)) return false;
    imu_delay(50U);
    if (!imu_read(true, 0x00U, &ignored, 1U)) return false;
    imu_delay(1U);
    if (!imu_check_id(true)) return false;
    if (!imu_write(false, 0x14U, 0xB6U)) return false;
    imu_delay(50U);
    if (!imu_check_id(false)) return false;

    /* Normal filtering, 200 Hz on both dies; fixed ranges +/-6 g and +/-2000 dps. */
    if (!imu_configure(true, 0x7DU, 0x04U, 0x04U, 50U) ||
        !imu_configure(true, 0x7CU, 0x00U, 0x03U, 5U) ||
        !imu_configure(true, 0x40U, 0xA9U, 0xFFU, 2U) ||
        !imu_configure(true, 0x41U, 0x01U, 0x03U, 2U) ||
        !imu_configure(false, 0x0FU, 0x00U, 0x07U, 2U) ||
        !imu_configure(false, 0x10U, 0x84U, 0x07U, 2U) ||
        !imu_configure(false, 0x11U, 0x00U, 0xA0U, 50U)) {
        return false;
    }
    s_status = SOFT3D_IMU_READY;
    return true;
}

static int32_t imu_raw(const uint8_t *bytes)
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8);
    return value >= 32768U ? (int32_t)value - 65536 : (int32_t)value;
}

bool Soft3D_IMU_Read(Soft3D_IMUSample *sample)
{
    uint8_t accel[6];
    uint8_t gyro[8];
    uint32_t i;
    if (sample == NULL) return false;
    memset(sample, 0, sizeof(*sample));
    if (s_status != SOFT3D_IMU_READY) return false;

    /* Checking both IDs on every sample also detects a floating/disconnected bus. */
    if (!imu_check_id(true) || !imu_read(true, 0x12U, accel, sizeof(accel)) ||
        !imu_read(false, 0x00U, gyro, sizeof(gyro))) return false;
    if (gyro[0] != GYRO_ID) {
        s_status = SOFT3D_IMU_NO_GYRO;
        return false;
    }
    for (i = 0U; i < 3U; ++i) {
        int32_t acc_value = imu_raw(accel + i * 2U);
        int32_t gyro_value = imu_raw(gyro + 2U + i * 2U);
        sample->accel_g[i] = (float)acc_value * (6.0f / 32768.0f);
        sample->gyro_dps[i] = (float)gyro_value * (2000.0f / 32768.0f);
        if (acc_value >= 32760 || acc_value <= -32760 ||
            gyro_value >= 32760 || gyro_value <= -32760) {
            sample->flags |= SOFT3D_IMU_SATURATED;
        }
    }
    sample->flags |= SOFT3D_IMU_ACCEL_VALID | SOFT3D_IMU_GYRO_VALID;
    return true;
}

uint32_t Soft3D_IMU_Status(void)
{
    return (uint32_t)s_status;
}
