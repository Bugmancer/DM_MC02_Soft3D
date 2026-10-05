#ifndef TEST_IMU_MAIN_H
#define TEST_IMU_MAIN_H

#include <stdint.h>

typedef enum {GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1} GPIO_PinState;
typedef enum {HAL_OK = 0, HAL_ERROR = 1} HAL_StatusTypeDef;
typedef struct {unsigned identifier;} SPI_HandleTypeDef;
#define ACC_CS_GPIO_Port ((void *)1)
#define GYRO_CS_GPIO_Port ((void *)1)
#define IMU_HEATER_GPIO_Port ((void *)2)
#define ACC_CS_Pin 1U
#define GYRO_CS_Pin 2U
#define IMU_HEATER_Pin 3U

void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                         uint8_t *rx, uint16_t length, uint32_t timeout);
#endif
