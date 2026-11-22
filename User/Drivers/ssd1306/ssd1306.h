#ifndef __SSD1306_H
#define __SSD1306_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define SSD1306_WIDTH   128U
#define SSD1306_HEIGHT  64U
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8U)
#define SSD1306_BUF_LEN (SSD1306_WIDTH * SSD1306_PAGES) /* 1024 Bytes */

/* 7-bit 0x3C 左移一位 = 0x78；若你的模块跳线在 0x7A 则改此处 */
#define SSD1306_I2C_ADDR (0x3CU << 1)

/**
 * @brief  探测 I2C 总线上 SSD1306 是否在线
 * @param  hi2c HAL I2C 句柄指针
 * @retval true 探测成功，false 未应答（请检查接线或地址）
 */
bool SSD1306_IsReady(I2C_HandleTypeDef *hi2c);

/**
 * @brief  初始化 OLED（使用水平地址模式，适配整屏 DMA 刷新）
 */
HAL_StatusTypeDef SSD1306_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  清空内部显存（不立即发送，需调用 UpdateScreen）
 */
void SSD1306_Clear(void);

/**
 * @brief  在显存中画单个像素
 */
void SSD1306_DrawPixel(uint8_t x, uint8_t y, bool on);

/**
 * @brief  在指定像素位置绘制 5x7 ASCII 字符（步进 6x8）
 */
void SSD1306_DrawChar(uint8_t x, uint8_t y, char ch, bool invert);

/**
 * @brief  在指定行（line: 0~7，对应 y = line*8）与列像素 x 绘制字符串
 */
void SSD1306_DrawString(uint8_t x, uint8_t line, const char *str, bool invert);

/**
 * @brief  绘制水平进度条 (percent: 0~100)
 */
void SSD1306_DrawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t percent);

/**
 * @brief  阻塞方式将 1024 字节显存整屏刷入 OLED（约耗时 23ms @ 400kHz）
 */
HAL_StatusTypeDef SSD1306_UpdateScreen_Blocking(void);

/**
 * @brief  DMA 异步方式刷屏（立即返回，不阻塞 CPU）
 * @note   调用前请确认上一次 DMA 已完成（SSD1306_IsDmaBusy() == false）
 */
HAL_StatusTypeDef SSD1306_UpdateScreen_DMA(void);

/**
 * @brief  查询当前 DMA 刷屏是否仍在进行中
 */
bool SSD1306_IsDmaBusy(void);

/**
 * @brief  在 HAL_I2C_MemTxCpltCallback / HAL_I2C_ErrorCallback 中调用，释放 DMA 忙标志
 */
void SSD1306_OnDmaDone(void);

#endif /* __SSD1306_H */
