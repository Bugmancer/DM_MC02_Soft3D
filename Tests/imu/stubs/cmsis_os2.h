#ifndef TEST_IMU_CMSIS_OS2_H
#define TEST_IMU_CMSIS_OS2_H
#include <stdint.h>
typedef void *osThreadId_t;
typedef int osStatus_t;
uint32_t osKernelGetTickFreq(void);
osThreadId_t osThreadGetId(void);
osStatus_t osDelay(uint32_t ticks);
#endif
