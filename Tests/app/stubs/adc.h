#ifndef TEST_APP_ADC_H
#define TEST_APP_ADC_H
#include "main.h"
typedef struct { uint32_t unused; } ADC_HandleTypeDef;
extern ADC_HandleTypeDef hadc1;
#define ADC_CALIB_OFFSET 0U
#define ADC_SINGLE_ENDED 0U
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *, uint32_t);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *);
#endif
