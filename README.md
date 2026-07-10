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
