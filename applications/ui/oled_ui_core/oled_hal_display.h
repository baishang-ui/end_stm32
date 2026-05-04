#ifndef __OLED_DRIVER_H
#define __OLED_DRIVER_H

#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include "stdio.h"
#include "main.h"

#define SSD1306
#define OLED_UI_USE_HW_SPI

#ifdef OLED_UI_USE_HW_SPI
#define OLED_UI_SPI_USE_DMA

#define OLED_DC_Clr()  (HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_RESET))
#define OLED_DC_Set()  (HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET))
#define OLED_RES_Clr() (HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_RESET))
#define OLED_RES_Set() (HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET))
#define OLED_CS_Clr()  (HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_RESET))
#define OLED_CS_Set()  (HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET))
#else
#define OLED_SCL_Clr() (OLED_CLK_GPIO_Port->BRR = OLED_CLK_Pin)
#define OLED_SCL_Set() (OLED_CLK_GPIO_Port->BSRR = OLED_CLK_Pin)
#define OLED_SDA_Clr() (OLED_MOSI_GPIO_Port->BRR = OLED_MOSI_Pin)
#define OLED_SDA_Set() (OLED_MOSI_GPIO_Port->BSRR = OLED_MOSI_Pin)
#define OLED_RES_Clr() (OLED_RST_GPIO_Port->BRR = OLED_RST_Pin)
#define OLED_RES_Set() (OLED_RST_GPIO_Port->BSRR = OLED_RST_Pin)
#define OLED_DC_Clr()  (OLED_DC_GPIO_Port->BRR = OLED_DC_Pin)
#define OLED_DC_Set()  (OLED_DC_GPIO_Port->BSRR = OLED_DC_Pin)
#define OLED_CS_Clr()  (OLED_CS_GPIO_Port->BRR = OLED_CS_Pin)
#define OLED_CS_Set()  (OLED_CS_GPIO_Port->BSRR = OLED_CS_Pin)
#endif

#define OLED_CMD  0
#define OLED_DATA 1

/**
 * @brief 初始化 OLED 控制器、GPIO/SPI 和显存状态。
 * @param 无。
 * @return 无。
 */
void OLED_Init(void);

/**
 * @brief 将完整显存缓冲区刷新到 OLED 屏幕。
 * @param 无。
 * @return 无。
 */
void OLED_Update(void);

/**
 * @brief 将显存中的指定区域刷新到 OLED 屏幕。
 * @param X 区域左上角 X 坐标。
 * @param Y 区域左上角 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 * @return 无。
 */
void OLED_UpdateArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);

/**
 * @brief 设置 OLED 显示颜色模式。
 * @param colormode true 表示深色模式，false 表示浅色模式。
 * @return 无。
 */
void OLED_SetColorMode(bool colormode);

/**
 * @brief 设置 OLED 亮度。
 * @param Brightness 亮度值，具体范围由底层控制器决定。
 * @return 无。
 */
void OLED_Brightness(int16_t Brightness);

#endif


