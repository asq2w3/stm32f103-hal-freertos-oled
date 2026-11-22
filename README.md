# STM32F103C8T6 HAL 库 + FreeRTOS + 0.96 寸 OLED 实战项目

> **硬件**：STM32F103C8T6 核心板 + 0.96 英寸 4 针 OLED（I2C / SSD1306）+ ST-Link V2  
> **工具链**：STM32CubeMX + CMake + CLion（Windows）

---

## 目录结构

```text
stm32f103-hal-freertos-oled/
├── .gitignore                        # 屏蔽 cmake-build-*、.idea/ 等编译与 IDE 产物
├── README.md                         # 项目说明与快速上手
├── stlink_f103.cfg                   # OpenOCD 烧录与调试配置（兼容原装与国产芯片）
├── docs/
│   └── stm32_hal_freertos_tutorial.md # 完整零基础实战教程
└── User/                             # 自定义驱动与业务代码（与 CubeMX 生成目录隔离）
    ├── Drivers/ssd1306/              # 0.96 寸 OLED 硬件 I2C + DMA 驱动
    └── App/                          # FreeRTOS 多任务监控终端业务代码
```

---

## 固定接线表

| 模块 | 模块引脚 | STM32F103C8T6 引脚 | 说明 |
| :--- | :--- | :--- | :--- |
| **ST-Link V2** | `SWCLK` / `SWDIO` / `GND` / `3.3V` | `SWCLK` / `SWDIO` / `GND` / `3.3V` | 供电与 SWD 调试（接了 3.3V 就不要同时插 USB 口） |
| **0.96 寸 OLED** | `VCC` / `GND` | `3.3V` / `GND` | **插线前务必核对背面丝印，严禁接反** |
| | `SCL` / `SDA` | `PB6` / `PB7` | 硬件 `I2C1` |
| **模拟按键** | 杜邦线一端 | `PA0` | 另一端轻碰 `GND` 触发下降沿外部中断 |

---

## 快速接入 CubeMX（Windows + CLion）

1. 克隆本仓库到本地无中文、无空格路径（例如 `D:\Embedded\stm32f103-hal-freertos-oled`）。
2. 打开 **STM32CubeMX**，选择 `STM32F103C8T6`，`Toolchain / IDE` 选择 **`CMake`**，按 [完整教程](docs/stm32_hal_freertos_tutorial.md) 配置外设与 FreeRTOS 后生成代码到本目录。
3. 在根目录 `CMakeLists.txt` 中追加 `User/` 源码与头文件路径：
   ```cmake
   target_sources(${CMAKE_PROJECT_NAME} PRIVATE
       User/Drivers/ssd1306/ssd1306.c
       User/App/app_freertos.c
   )

   target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
       User/Drivers/ssd1306
       User/App
   )
   ```
4. 用 **CLion** 打开工程，配置 `OpenOCD Download & Run` 选择 `stlink_f103.cfg`，开启 `RTOS Integration (FreeRTOS)` 即可编译烧录与调试。
