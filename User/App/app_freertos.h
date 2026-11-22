#ifndef __APP_FREERTOS_H
#define __APP_FREERTOS_H

#include "stm32f1xx_hal.h"

/**
 * @brief  初始化所有 FreeRTOS IPC 对象与业务任务（在 MX_FREERTOS_Init 中调用）
 */
void App_FreeRTOS_Init(void);

/**
 * @brief  供 stm32f1xx_it / main 中断回调转发的钩子
 */
void App_OnKeyExtiISR(void);
void App_OnOledDmaDoneISR(void);

#endif /* __APP_FREERTOS_H */
