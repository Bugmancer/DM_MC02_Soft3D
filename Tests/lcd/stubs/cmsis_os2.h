#ifndef TEST_LCD_CMSIS_OS2_H
#define TEST_LCD_CMSIS_OS2_H
#include <stdint.h>
typedef void *osThreadId_t;
typedef int osStatus_t;
#define osWaitForever 0xFFFFFFFFU
#define osFlagsWaitAny 0U
uint32_t osKernelGetTickFreq(void);
osThreadId_t osThreadGetId(void);
osStatus_t osDelay(uint32_t ticks);
uint32_t osThreadFlagsClear(uint32_t flags);
uint32_t osThreadFlagsWait(uint32_t flags, uint32_t options, uint32_t timeout);
uint32_t osThreadFlagsSet(osThreadId_t thread, uint32_t flags);
#endif
