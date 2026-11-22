#include "app_freertos.h"
#include "ssd1306.h"
#include "adc.h"
#include "i2c.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>

/* ==================== 数据结构与句柄 ==================== */
typedef struct {
    int16_t  temp_x10;     /* 定点数温度 (单位 0.1 C, 避免链接庞大的浮点 printf) */
    uint32_t uptime_sec;   /* 系统运行秒数 */
    uint32_t free_heap;    /* FreeRTOS 当前空闲堆字节数 */
    uint16_t oled_stk_min; /* OLED 任务栈最小剩余安全水位 (Words) */
} SysMetrics_t;

static TaskHandle_t  s_task_input  = NULL;
static TaskHandle_t  s_task_oled   = NULL;
static TaskHandle_t  s_task_sensor = NULL;
static QueueHandle_t s_q_metrics   = NULL; /* 长度为 1 的邮箱队列 (xQueueOverwrite) */

static volatile uint8_t s_current_page = 0; /* 0: 系统监控, 1: 秒表, 2: DMA 动画 */

/* ==================== 内部辅助：读取片内温度传感器 ==================== */
static int16_t read_mcu_temp_x10(void)
{
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 20) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return 0;
    }
    uint32_t raw = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    /* RM0008 公式: T = (V25 - Vsense) / Avg_Slope + 25
     * V25 = 1.43V, Avg_Slope = 4.3mV/C, Vref = 3.3V, 12-bit = 4095
     * 全程使用整数运算避免软件浮点拖慢 M3 核并节省 Flash */
    int32_t v_sense_mv = (int32_t)((raw * 3300UL) / 4095UL);
    int32_t temp_x10   = ((1430L - v_sense_mv) * 100L) / 43L + 250L;
    return (int16_t)temp_x10;
}

/* ==================== 任务 1：按键输入（最高优先级，事件驱动） ==================== */
static void Task_Input(void *arg)
{
    (void)arg;
    for (;;)
    {
        /* 永久阻塞等待 PA0 EXTI 中断发来的任务通知，平时 0% CPU 占用 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* 非阻塞消抖：等待 40ms 后确认 PA0 确实处于低电平 */
        vTaskDelay(pdMS_TO_TICKS(40));
        if (HAL_GPIO_ReadPin(KEY0_GPIO_Port, KEY0_Pin) == GPIO_PIN_RESET)
        {
            s_current_page = (uint8_t)((s_current_page + 1U) % 3U);
            /* 消抖保护间隔，避免杜邦线弹跳一次切两页 */
            vTaskDelay(pdMS_TO_TICKS(180));
        }
        /* 清除防抖期间可能积攒的多余通知 */
        ulTaskNotifyTake(pdTRUE, 0);
    }
}

/* ==================== 任务 2：传感器与系统指标采样（500ms 周期） ==================== */
static void Task_Sensor(void *arg)
{
    (void)arg;
    HAL_ADCEx_Calibration_Start(&hadc1); /* F103 上电做一次 ADC 自校准 */

    TickType_t last_wake = xTaskGetTickCount();
    SysMetrics_t m = {0};

    for (;;)
    {
        m.temp_x10   = read_mcu_temp_x10();
        m.uptime_sec = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        m.free_heap  = (uint32_t)xPortGetFreeHeapSize();
        m.oled_stk_min = (s_task_oled != NULL)
                         ? (uint16_t)uxTaskGetStackHighWaterMark(s_task_oled) : 0U;

        /* 长度为 1 的队列用 Overwrite，永远只保留最新一帧采样数据，永不阻塞生产者 */
        xQueueOverwrite(s_q_metrics, &m);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

/* ==================== 任务 3：OLED 屏幕看门人（独占 I2C1，DMA 非阻塞刷屏） ==================== */
static void Task_OLED(void *arg)
{
    (void)arg;
    SSD1306_Init(&hi2c1);

    TickType_t last_wake = xTaskGetTickCount();
    SysMetrics_t m = {0};
    char buf[24];
    uint8_t anim = 0;
    uint32_t sw_ticks = 0;

    for (;;)
    {
        /* 非阻塞偷看最新指标数据（不消费，保留供下次使用） */
        xQueuePeek(s_q_metrics, &m, 0);

        SSD1306_Clear();
        uint8_t page = s_current_page;

        if (page == 0U)
        {
            SSD1306_DrawString(0, 0, " [1/3] SYS MONITOR  ", true);
            int t_int = m.temp_x10 / 10;
            int t_dec = abs(m.temp_x10 % 10);
            snprintf(buf, sizeof(buf), "MCU Temp : %d.%d C", t_int, t_dec);
            SSD1306_DrawString(0, 2, buf, false);

            snprintf(buf, sizeof(buf), "Uptime   : %lus", (unsigned long)m.uptime_sec);
            SSD1306_DrawString(0, 3, buf, false);

            snprintf(buf, sizeof(buf), "FreeHeap : %lu B", (unsigned long)m.free_heap);
            SSD1306_DrawString(0, 4, buf, false);

            snprintf(buf, sizeof(buf), "OLED Stk : %u W", (unsigned)m.oled_stk_min);
            SSD1306_DrawString(0, 5, buf, false);

            SSD1306_DrawString(0, 7, "PA0->GND: Next Page", false);
        }
        else if (page == 1U)
        {
            sw_ticks += 50U; /* 本任务严格 50ms 周期运行 */
            uint32_t sec = sw_ticks / 1000U;
            uint32_t ms  = (sw_ticks % 1000U) / 10U;
            SSD1306_DrawString(0, 0, " [2/3] STOPWATCH    ", true);
            snprintf(buf, sizeof(buf), "TIME: %02lu:%02lu.%02lu",
                     (unsigned long)(sec / 60U), (unsigned long)(sec % 60U), (unsigned long)ms);
            SSD1306_DrawString(6, 3, buf, false);
            SSD1306_DrawString(0, 6, "Driven by DelayUntil", false);
        }
        else
        {
            SSD1306_DrawString(0, 0, " [3/3] I2C DMA DEMO ", true);
            snprintf(buf, sizeof(buf), "Progress : %3u%%", anim);
            SSD1306_DrawString(0, 2, buf, false);
            SSD1306_DrawProgressBar(0, 32, 128, 14, anim);
            SSD1306_DrawString(0, 7, "20 FPS Non-Blocking", false);
            anim = (uint8_t)((anim + 3U) % 101U);
        }

        /* 发起 DMA 刷屏后主动休眠等 DMA 完成通知（期间约 23ms CPU 完全让给其他任务！） */
        if (SSD1306_UpdateScreen_DMA() == HAL_OK)
        {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        }

        /* 精准锁定 20 FPS (每帧 50ms) */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));
    }
}

/* ==================== 任务 4：心跳 LED（最低优先级，证明系统无卡死） ==================== */
static void Task_HeartBeat(void *arg)
{
    (void)arg;
    for (;;)
    {
        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ==================== 异常防御钩子（爆栈 / 内存耗尽立即捕获） ==================== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask; (void)pcTaskName;
    __disable_irq();
    while (1) { /* 在此打断点，看 pcTaskName 即可秒定位哪个任务爆栈 */ }
}

void vApplicationMallocFailedHook(void)
{
    __disable_irq();
    while (1) { /* 堆内存不足，调大 TOTAL_HEAP_SIZE 或压缩任务栈 */ }
}

/* ==================== 中断转发入口 & 初始化 ==================== */
void App_OnKeyExtiISR(void)
{
    BaseType_t woken = pdFALSE;
    if (s_task_input != NULL)
    {
        vTaskNotifyGiveFromISR(s_task_input, &woken);
        portYIELD_FROM_ISR(woken); /* 若被唤醒任务优先级更高，退出中断时立即切换 */
    }
}

void App_OnOledDmaDoneISR(void)
{
    SSD1306_OnDmaDone();
    BaseType_t woken = pdFALSE;
    if (s_task_oled != NULL)
    {
        vTaskNotifyGiveFromISR(s_task_oled, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void App_FreeRTOS_Init(void)
{
    s_q_metrics = xQueueCreate(1, sizeof(SysMetrics_t));

    /* 注意：栈大小单位为 Word (4 Bytes)！
     * Task_OLED 用到 snprintf，给 256 Words (1024 Bytes) 留足安全余量 */
    xTaskCreate(Task_Input,     "Input",  128, NULL, 3, &s_task_input);
    xTaskCreate(Task_Sensor,    "Sensor", 192, NULL, 2, &s_task_sensor);
    xTaskCreate(Task_OLED,      "OLED",   256, NULL, 2, &s_task_oled);
    xTaskCreate(Task_HeartBeat, "LED",     96, NULL, 1, NULL);
}
