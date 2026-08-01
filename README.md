# STM32 OV7670 Live

STM32F103C8T6 + OV7670 (AL422B FIFO) + ST7735 TFT LCD 实时摄像头取景器。

160×128 RGB565 @ 30fps，双 DMA 零拷贝流水线。

## 硬件

| 组件 | 型号 | 参数 |
|------|------|------|
| MCU | STM32F103C8T6 | 72MHz, LQFP48, 20KB RAM, 64KB FLASH |
| 摄像头 | OV7670 + AL422B FIFO 模块 | VGA, SCCB 配置, 8-bit 并行 |
| 显示屏 | ST7735 1.8" TFT | 160×128 RGB565, SPI 4-line |

### 引脚分配

| 信号 | 引脚 | 功能 |
|------|------|------|
| OV7670_D0~D7 | PA0~PA7 | 8-bit 数据总线（FIFO DO） |
| FIFO_RCK | PB1 (TIM3_CH4) | 1.44MHz PWM 读时钟 |
| FIFO_OE | PA8 | 输出使能（低有效） |
| FIFO_WR | PB5 | 写使能（NAND HREF 选通） |
| FIFO_WRST | PB7 | 写指针复位 |
| FIFO_RRST | PB6 | 读指针复位 |
| VSYNC | PA11 (EXTI11) | 帧同步（下降沿中断） |
| HREF | PA12 | 行有效 |
| SCL | PB10 | SCCB 时钟（OD） |
| SDA | PB11 | SCCB 数据（OD） |
| PWDN | PB3 | 掉电控制 |
| RESET | PB4 | 复位 |
| LCD_SCL | PB13 (SPI2_SCK) | SPI 时钟 18MHz |
| LCD_SDA | PB15 (SPI2_MOSI) | SPI 主出 |
| LCD_CS | PB12 | 片选 |
| LCD_DC | PB8 | 数据/命令 |
| LCD_RES | PB14 | 硬件复位 |
| LCD_BLK | PB9 | 背光 |
| UART_TX | PA9 | 调试串口 115200 |
| TEST_LED | PA15 | 测试指示 LED |

## 架构

```
OV7670 -> AL422B FIFO -> GPIOA->IDR -> DMA1_Ch3(Circular) -> PipelineBuffer[640]
                                                              HT/TC 乒乓
                                          DMA1_Ch5(Normal) <- PipelineBuffer[320]
                                               |
                                          SPI2-DR -> ST7735 LCD
```

- **Camera DMA**：TIM3_CH4 PWM 每周期触发 1 字节搬运，Circular 模式填满 640B 缓冲区
- **SPI DMA**：HT 中断发送前 320B，TC 中断发送后 320B，与 Camera DMA 乒乓交替
- **零拷贝**：OV7670 输出 RGB565 与 ST7735 像素格式一致，SPI DMA 直送显存

### 关键时序

| 参数 | 值 |
|------|-----|
| RCK 频率 | 1.44MHz (TIM3 ARR=49) |
| SPI 频率 | 18MHz (APB1 /2) |
| 帧大小 | 160×128×2 = 40960 字节 |
| 半区大小 | 320 字节 (128 次半区传输) |
| 帧读时间 | 28.4ms |
| VSYNC 延时 | 1.3ms (后肩 1.11ms + 190μs 余量) |
| 理论帧率 | 30fps |

## 构建

### 依赖

- CMake 3.22+
- arm-none-eabi-gcc (STM32CubeCLT)

### 编译

```bash
cmake --preset Debug
cmake --build --preset Debug
```

输出：`build/Debug/stm32-ov7670-live.elf`

### 资源占用

| 区域 | 使用 | 容量 | 占比 |
|------|------|------|------|
| RAM | 3 KB | 20 KB | 15% |
| FLASH | 19.7 KB | 64 KB | 30% |

## 项目结构

```
Core/
  Inc/bsp/          BSP 驱动头文件
  Src/bsp/          BSP 驱动实现
    dwt_delay.c     DWT CYCCNT 精确延时
    ov7670_sccb.c   SCCB/I2C bit-bang 驱动
    ov7670.c        OV7670 寄存器配置 (158 条)
    st7735.c        ST7735 LCD 驱动
    pipeline.c      双 DMA 帧采集流水线
    debug.c         printf -> USART1 重定向
  Src/main.c        主程序 + CubeMX 外设初始化
  Src/stm32f1xx_it.c 中断服务函数
Drivers/             STM32 HAL + CMSIS
cmake/               CMake 工具链 + CubeMX 生成
docs/                设计文档
ThirdParty/          第三方库（Unity 测试框架）
```

## 模块说明

### Pipeline 状态机

```
DISABLED (上电默认)
  -> Pipeline_Init() -> IDLE
  -> VSYNC ↓ -> FRAME_START (DWT 1.3ms 非阻塞延时)
  -> 延时到期 -> FRAME_CAPTURING (PWM + Camera DMA + SPI DMA 乒乓)
  -> 40960B 传输完成 -> FRAME_DONE
  -> 清理 (停 PWM, OE 高, CS 高) -> IDLE
```

- 忙时丢帧：VSYNC 在非 IDLE 态直接丢弃
- DISABLED 守卫：初始化完成前 VSYNC 中断不会触发采集

### OV7670 配置

158 条寄存器，基于 Linux 内核 ov7670.c + bayernfan CameraInit 交叉验证：

- VGA 640×480 -> DCW by2 -> 320×240 -> Digital Zoom -> 160×128
- RGB565 色彩矩阵（与 YUV 默认值不同）
- 完整 Gamma 曲线、AGC/AEC、白平衡参数
- OV 片内 mux 触发序列

## 调试

Debug 构建自动定义 `DEBUG` 宏，`debug_printf` 通过 USART1 (115200 baud) 输出：

```
=== STM32 OV7670 Live ===
DWT init OK
OV7670 init OK
LCD init OK
Pipeline init OK, enabling VSYNC...
```

> `debug_printf` 为阻塞 UART，不可在中断上下文使用。

## 单元测试

工程内置设备端单元测试框架（[Unity](https://github.com/ThrowTheSwitch/Unity)），通过编译期 CMake 开关切换：任一测试组被使能时，固件入口 `main` 分支到测试运行器；未使能任何组时，正常运行主程序，测试代码不参与编译。

### 开关体系

| CMake Option | 测试组 | 类型 | 状态 | 说明 |
|---|---|---|---|---|
| `TEST_TEST_LED` | TEST_LED 烟雾测试 | 硬件 | 已实现 | TEST_LED 1Hz 闪烁，验证框架基础设施（编译/烧录/运行/分支切换） |
| `TEST_LOGIC` | 纯逻辑 | 纯逻辑 | 待实现 | 不依赖硬件：UsToTicks 边界、OV7670 寄存器表完整性等 |
| `TEST_SCCB` | SCCB | 硬件在环 | 待实现 | 回读 OV7670 PID/MID 校验 SCCB 总线与接线 |
| `TEST_OV7670` | OV7670 | 硬件在环 | 待实现 | 初始化后回读关键寄存器校验配置已写入 |
| `TEST_LCD` | LCD | 硬件在环 | 待实现 | 初始化 + 纯色填充验证 SPI 通路 |

- 任一 `TEST_*` 开启即编译测试基础设施，并由 CMake 派生 `UNIT_TESTS_ENABLED` 宏，`main` 据此切换分支
- 各组可任意组合，例如 `-DTEST_LOGIC=ON -DTEST_SCCB=ON`
- 未使能时测试代码完全不编译，主程序零开销

### 目录结构

```
ThirdParty/Unity/         Unity 测试框架（unity.h / unity_internals.h / unity.c）
Core/Inc/test/            测试头文件
  test_runner.h           调度入口
  test_test_led.h         TEST_LED 烟雾测试
Core/Src/test/            测试实现
  test_runner.c           调度 + setUp/tearDown + LED 反馈
  test_test_led.c         1Hz 闪烁烟雾测试
```

### 触发机制

`main.c` 的 USER CODE 区域通过宏切换：

```c
#ifdef UNIT_TESTS_ENABLED
  TestRunner_Run();   /* 跑选中的测试组，LED 反馈，不返回 */
#else
  /* 正常 pipeline 初始化 + 主循环 */
#endif
```

- 测试模式下 CubeMX 生成的外设初始化（GPIO/DMA/TIM3/SPI2/USART1）仍执行，但 USER CODE 内的 BSP 初始化（OV7670/LCD/Pipeline）被跳过，避免无硬件时 `OV7670_Init()` SCCB NACK 卡死
- 各测试组入口按需初始化对应硬件

### 输出与反馈

- **串口**：Unity 默认经 `putchar` -> `_write` -> `__io_putchar` -> `HAL_UART_Transmit` 输出到 USART1，复用现有调试链路，零额外配置
- **TEST_LED (PA15)**：`TEST_TEST_LED` 组本身即 1Hz 闪烁（验证框架存活）；其他组跑完 Unity 后，全部通过常亮、有失败快闪，便于无串口时肉眼判定

### 构建示例

```bash
# 正常主程序（默认，无测试）
cmake --preset Debug && cmake --build --preset Debug

# TEST_LED 烟雾测试
cmake --preset Debug -DTEST_TEST_LED=ON && cmake --build --preset Debug

# 组合：纯逻辑 + SCCB
cmake --preset Debug -DTEST_LOGIC=ON -DTEST_SCCB=ON && cmake --build --preset Debug
```

烧录后通过 USART1 (115200) 查看 Unity 测试报告，TEST_LED 指示通过/失败状态。

### CMake 选项缓存陷阱

> **`-DTEST_*` 的值会被 CMake 缓存，之后不带 `-D` 重新 configure 仍沿用缓存值。** 切勿认为"没传 `-D` 就是 OFF"。

- 示例：执行过 `cmake --preset Debug -DTEST_LCD=ON` 后，再运行 `cmake --preset Debug`（不带 `-D`），`TEST_LCD` 仍是 `ON`，测试依旧开启。
- **规范做法**：每次切换测试状态都必须显式传 `-D` 并重新 configure：
  ```bash
  # 开启（可任意组合多个测试）
  cmake --preset Debug -DTEST_LCD=ON -DTEST_LCD_DMA=ON && cmake --build --preset Debug

  # 关闭，回到正常主程序
  cmake --preset Debug -DTEST_LCD=OFF && cmake --build --preset Debug
  ```
- **查看当前生效值**：
  ```bash
  grep TEST_ build/Debug/CMakeCache.txt
  ```
- **彻底重置**：删除 build 目录后重新 configure：
  ```bash
  rm -rf build/Debug && cmake --preset Debug
  ```

### 资源占用

| 构建配置 | RAM | FLASH |
|----------|-----|-------|
| 默认（无测试） | 3 KB (15%) | 23064 B (35.19%) |
| TEST_TEST_LED=ON | 3272 B (15.98%) | 18812 B (28.70%) |

> TEST_TEST_LED 模式 FLASH 反而更小：测试 main 跳过了 pipeline/OV7670/LCD 初始化，未调用的 BSP 函数被 `--gc-sections` 丢弃。

TEST_TEST_LED 烟雾测试已在硬件上验证通过（LED 1Hz 闪烁）。
