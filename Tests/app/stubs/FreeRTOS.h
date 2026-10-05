#ifndef TEST_APP_FREERTOS_H
#define TEST_APP_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t UBaseType_t;
size_t xPortGetFreeHeapSize(void);
#endif
