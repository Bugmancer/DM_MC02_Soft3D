#include "soft3d_imu.h"
#include "main.h"
#include "spi.h"
#include "cmsis_os2.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

SPI_HandleTypeDef hspi2 = {2U};
static uint8_t s_accel[128];
static uint8_t s_gyro[128];
static bool s_accel_present;
static bool s_gyro_present;
static bool s_accel_spi;
static GPIO_PinState s_accel_cs = GPIO_PIN_SET;
static GPIO_PinState s_gyro_cs = GPIO_PIN_SET;
static GPIO_PinState s_heater;
static uint32_t s_ticks;
static uint32_t s_accel_reset_at;
static uint32_t s_gyro_reset_at;
static uint32_t s_tick_frequency = 1000U;
static unsigned s_transfers;
static unsigned s_fail_transfer;
static unsigned s_activations;
static bool s_bad_configuration;
static osThreadId_t s_thread = (void *)1;

static void registers_reset(bool accel)
{
    if (accel) {
        memset(s_accel, 0, sizeof(s_accel));
        s_accel[0] = 0x1EU;
        s_accel_spi = false;
        s_accel_reset_at = s_ticks;
    } else {
        memset(s_gyro, 0, sizeof(s_gyro));
        s_gyro[0] = 0x0FU;
        s_gyro_reset_at = s_ticks;
    }
}

static void bus_reset(void)
{
    s_ticks = 0U;
    s_transfers = 0U;
    s_fail_transfer = 0U;
    s_activations = 0U;
    s_accel_present = true;
    s_gyro_present = true;
    s_bad_configuration = false;
    s_accel_cs = GPIO_PIN_SET;
    s_gyro_cs = GPIO_PIN_SET;
    s_thread = (void *)1;
    s_tick_frequency = 1000U;
    registers_reset(true);
    registers_reset(false);
}

void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state)
{
    (void)port;
    if (pin == ACC_CS_Pin) {
        assert(state == GPIO_PIN_SET || s_gyro_cs == GPIO_PIN_SET);
        s_accel_cs = state;
    } else if (pin == GYRO_CS_Pin) {
        assert(state == GPIO_PIN_SET || s_accel_cs == GPIO_PIN_SET);
        s_gyro_cs = state;
    } else {
        assert(pin == IMU_HEATER_Pin && state == GPIO_PIN_RESET);
        s_heater = state;
    }
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                         uint8_t *rx, uint16_t length, uint32_t timeout)
{
    bool accel = s_accel_cs == GPIO_PIN_RESET;
    uint8_t *registers = accel ? s_accel : s_gyro;
    unsigned prefix = accel ? 2U : 1U;
    uint8_t address = tx[0] & 0x7FU;
    unsigned i;
    assert(spi == &hspi2 && timeout > 0U && timeout <= 5U);
    assert((s_accel_cs == GPIO_PIN_RESET) != (s_gyro_cs == GPIO_PIN_RESET));
    assert(length >= 2U && length <= 10U);
    ++s_transfers;
    if (s_transfers == s_fail_transfer) return HAL_ERROR;
    memset(rx, 0xFF, length);
    if (!(accel ? s_accel_present : s_gyro_present)) return HAL_OK;
    assert((s_ticks - (accel ? s_accel_reset_at : s_gyro_reset_at)) * 1000U / s_tick_frequency >= 1U);
    if (accel && !s_accel_spi) {
        assert((tx[0] & 0x80U) != 0U && length == 3U);
        s_accel_spi = true;
        ++s_activations;
        return HAL_OK;
    }
    if ((tx[0] & 0x80U) != 0U) {
        assert(length > prefix);
        for (i = prefix; i < length; ++i) {
            rx[i] = registers[address + i - prefix];
        }
    } else {
        assert(length == 2U);
        if (tx[1] == 0xB6U && address == (accel ? 0x7EU : 0x14U)) {
            registers_reset(accel);
        } else if (!(s_bad_configuration && accel && address == 0x40U)) {
            registers[address] = tx[1];
        }
    }
    return HAL_OK;
}

uint32_t osKernelGetTickFreq(void) {return s_tick_frequency;}
osThreadId_t osThreadGetId(void) {return s_thread;}
osStatus_t osDelay(uint32_t ticks)
{
    assert(ticks > 0U);
    s_ticks += ticks;
    return 0;
}

static void pack(uint8_t *output, int value)
{
    uint16_t word = (uint16_t)value;
    output[0] = (uint8_t)word;
    output[1] = (uint8_t)(word >> 8);
}

static void check_empty(const Soft3D_IMUSample *sample)
{
    unsigned i;
    assert(sample->flags == 0U);
    for (i = 0U; i < 3U; ++i) assert(sample->accel_g[i] == 0.0f && sample->gyro_dps[i] == 0.0f);
}

static void check_deselected(void)
{
    assert(s_accel_cs == GPIO_PIN_SET && s_gyro_cs == GPIO_PIN_SET);
    assert(s_heater == GPIO_PIN_RESET);
}

static void test_initialization_and_samples(void)
{
    Soft3D_IMUSample sample;
    bus_reset();
    memset(&sample, 0xA5, sizeof(sample));
    assert(!Soft3D_IMU_Read(&sample));
    check_empty(&sample);
    assert(Soft3D_IMU_Init());
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_READY);
    assert(s_accel[0x7D] == 4U && s_accel[0x7C] == 0U);
    assert(s_accel[0x40] == 0xA9U && s_accel[0x41] == 1U);
    assert(s_gyro[0x0F] == 0U && (s_gyro[0x10] & 7U) == 4U && s_gyro[0x11] == 0U);
    assert(s_activations == 2U && s_ticks >= 250U);
    check_deselected();
    pack(s_accel + 0x12, 16384);
    pack(s_accel + 0x14, -8192);
    pack(s_accel + 0x16, 5461);
    pack(s_gyro + 2, 16384);
    pack(s_gyro + 4, -32768);
    pack(s_gyro + 6, 1);
    assert(Soft3D_IMU_Read(&sample));
    assert(sample.flags == (SOFT3D_IMU_ACCEL_VALID | SOFT3D_IMU_GYRO_VALID | SOFT3D_IMU_SATURATED));
    assert(sample.accel_g[0] == 3.0f && sample.accel_g[1] == -1.5f);
    assert(fabsf(sample.accel_g[2] - 1.0f) < 0.001f);
    assert(sample.gyro_dps[0] == 1000.0f && sample.gyro_dps[1] == -2000.0f);
    assert(fabsf(sample.gyro_dps[2] - 2000.0f / 32768.0f) < 0.00001f);
    pack(s_gyro + 4, -123);
    assert(Soft3D_IMU_Read(&sample));
    assert(sample.flags == (SOFT3D_IMU_ACCEL_VALID | SOFT3D_IMU_GYRO_VALID));
    check_deselected();
}

static void test_absence_and_configuration(void)
{
    bus_reset();
    s_accel_present = false;
    assert(!Soft3D_IMU_Init());
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_NO_ACCEL);
    check_deselected();
    bus_reset();
    s_gyro_present = false;
    assert(!Soft3D_IMU_Init());
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_NO_GYRO);
    check_deselected();
    bus_reset();
    s_bad_configuration = true;
    assert(!Soft3D_IMU_Init());
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_CONFIG_ERROR);
    check_deselected();
    bus_reset();
    s_thread = NULL;
    assert(!Soft3D_IMU_Init());
    assert(s_transfers == 0U);
    check_deselected();
    bus_reset();
    s_tick_frequency = 128U;
    assert(Soft3D_IMU_Init());
    assert(s_ticks * 1000U / s_tick_frequency >= 250U);
}

static void test_each_initialization_transfer_can_fail(void)
{
    unsigned count;
    unsigned i;
    bus_reset();
    assert(Soft3D_IMU_Init());
    count = s_transfers;
    for (i = 1U; i <= count; ++i) {
        Soft3D_IMUSample sample;
        bus_reset();
        s_fail_transfer = i;
        assert(!Soft3D_IMU_Init());
        assert(Soft3D_IMU_Status() == SOFT3D_IMU_SPI_ERROR);
        check_deselected();
        memset(&sample, 0xA5, sizeof(sample));
        assert(!Soft3D_IMU_Read(&sample));
        check_empty(&sample);
    }
}

static void test_sample_failures_and_recovery(void)
{
    unsigned i;
    Soft3D_IMUSample sample;
    for (i = 1U; i <= 3U; ++i) {
        bus_reset();
        assert(Soft3D_IMU_Init());
        s_fail_transfer = s_transfers + i;
        memset(&sample, 0xA5, sizeof(sample));
        assert(!Soft3D_IMU_Read(&sample));
        assert(Soft3D_IMU_Status() == SOFT3D_IMU_SPI_ERROR);
        check_empty(&sample);
        check_deselected();
        s_fail_transfer = 0U;
        assert(Soft3D_IMU_Init());
    }
    s_accel_present = false;
    assert(!Soft3D_IMU_Read(&sample));
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_NO_ACCEL);
    check_empty(&sample);
    bus_reset();
    assert(Soft3D_IMU_Init());
    s_gyro_present = false;
    assert(!Soft3D_IMU_Read(&sample));
    assert(Soft3D_IMU_Status() == SOFT3D_IMU_NO_GYRO);
    check_empty(&sample);
    check_deselected();
}

int main(void)
{
    test_initialization_and_samples();
    test_absence_and_configuration();
    test_each_initialization_transfer_can_fail();
    test_sample_failures_and_recovery();
    puts("bmi088: all tests passed");
    return 0;
}
