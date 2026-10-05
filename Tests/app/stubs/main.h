#ifndef TEST_APP_MAIN_H
#define TEST_APP_MAIN_H
#include <stdint.h>
typedef struct { volatile uint32_t DEMCR; } CoreDebug_Type;
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
extern CoreDebug_Type test_core_debug;
extern DWT_Type test_dwt;
extern uint32_t SystemCoreClock;
#define CoreDebug (&test_core_debug)
#define DWT (&test_dwt)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
uint32_t HAL_GetTick(void);
void Error_Handler(void);
#endif
