#ifndef TEST_LCD_MAIN_H
#define TEST_LCD_MAIN_H

#include <stdint.h>

typedef enum {GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1} GPIO_PinState;
typedef enum {HAL_OK = 0, HAL_ERROR = 1} HAL_StatusTypeDef;
typedef struct {uint32_t CR;} DMA_Stream_TypeDef;
typedef struct {void *Instance;} DMA_HandleTypeDef;
typedef struct {DMA_HandleTypeDef *hdmatx;} SPI_HandleTypeDef;
typedef struct {uint32_t CCR;} SCB_Type;
extern SCB_Type test_scb;

#define SCB (&test_scb)
#define __DCACHE_PRESENT 1U
#define SCB_CCR_DC_Msk 0x10000U
#define DMA_SxCR_EN 1U
#define LCD_CS_GPIO_Port ((void *)1)
#define LCD_BLK_GPIO_Port ((void *)2)
#define LCD_RES_GPIO_Port ((void *)3)
#define LCD_DC_GPIO_Port ((void *)4)
#define LCD_CS_Pin 1U
#define LCD_BLK_Pin 2U
#define LCD_RES_Pin 3U
#define LCD_DC_Pin 4U
#define SPI1_IRQn 1
#define DMA1_Stream0_IRQn 2
#define __DSB() Test_DSB()
#define __HAL_SPI_DISABLE(spi) Test_SPI_Disable(spi)
#define __HAL_RCC_DMA1_FORCE_RESET() Test_DMA_Reset(1)
#define __HAL_RCC_DMA1_RELEASE_RESET() Test_DMA_Reset(0)

void HAL_GPIO_WritePin(void *port, uint16_t pin, GPIO_PinState state);
void HAL_Delay(uint32_t milliseconds);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *data,
                                    uint16_t length, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *spi, uint8_t *data, uint16_t length);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_EnableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
void SCB_CleanDCache_by_Addr(uint32_t *address, int32_t size);
void Test_DSB(void);
void Test_SPI_Disable(SPI_HandleTypeDef *spi);
void Test_DMA_Reset(int asserted);
void Error_Handler(void);
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *spi);
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi);

#endif
