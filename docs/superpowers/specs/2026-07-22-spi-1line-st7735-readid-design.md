# SPI 单线双向模式（BIDIMODE）支持 ST7735 寄存器读取

> 日期：2026-07-22
> 分支：feature/spi-1line-readid

## 1. 背景与目标

### 1.1 问题

当前 SPI2 配置为 `SPI_DIRECTION_2LINES`（全双工），但 PB14（SPI2_MISO）被复用为 LCD_RESET GPIO。LCD 模块（ST7735 4-line serial）的 SDA 线在硬件层面是双向的，支持寄存器回读。当前设计只发不收，无法在初始化阶段通过读取 ST7735 ID 来验证 SPI 配置正确性。

### 1.2 目标

将 SPI2 切换为 `SPI_DIRECTION_1LINE`（BIDIMODE 单线双向），通过 MOSI（PB15）实现双向通信，在 `LCD_Init()` 中读取 ST7735 RDID1 寄存器（0xDA，期望值 0x7C）验证 SPI 配置。

## 2. 可行性论证

### 2.1 硬件兼容性

**STM32F103 SPI BIDIMODE：**

- `SPI_CR1.BIDIMODE`（Bit15）= 1 启用单线双向模式
- `SPI_CR1.BIDIOE`（Bit14）控制方向：1=输出（发送），0=输入（接收）
- BIDIMODE=1 时 MOSI 引脚作为双向数据线，MISO 引脚释放为 GPIO
- 主模式下 BIDIOE=0 时，SPI 启用后自动产生 SCL 时钟（HAL 驱动注释 line 66-70 确认）

**ST7735 4-line serial 接口（手册 Page 30 Section 9.3）：**

- "4-line serial interface use: CSX, D/CX, SCL and SDA (serial data input/output)"
- SDA 是双向数据线，支持读操作（RDID1/2/3、状态寄存器等）
- 读取时 ST7735 在 SCL 下降沿输出数据，MCU 在 SCL 上升沿采样

**引脚连接验证：**

| STM32 引脚 | SPI 功能 | 实际连接 | 兼容性 |
|-----------|---------|---------|--------|
| PB15 (MOSI) | BIDIMODE 下为双向数据线 | LCD SDA | 直接匹配 |
| PB14 (MISO) | BIDIMODE 下释放 | LCD RESET (GPIO) | 无冲突 |
| PB13 (SCK) | SPI 时钟 | LCD SCL | 不变 |
| PB12 (NSS) | 软件 CS | LCD CS | 不变 |

### 2.2 时序兼容性

ST7735 手册 Page 32 原文：
- "The driver samples the SDA (input data) at **rising edge** of SCL"
- "shifts SDA (output data) at **falling edge** of SCL"

STM32 SPI Mode 0（CPOL=0, CPHA=0，当前配置）：
- SCL 空闲低电平
- 接收时数据在 SCL **上升沿**采样

时序完全匹配：
- 写：MCU 在 SCL 下降沿更新 MOSI -> ST7735 在 SCL 上升沿采样
- 读：ST7735 在 SCL 下降沿输出 SDA -> MCU 在 SCL 上升沿采样

### 2.3 HAL 库支持验证

逐行审查 `stm32f1xx_hal_spi.c` 确认：

**发送（命令/数据/DMA）：**

| 函数 | 行号 | 1-line 行为 |
|------|------|------------|
| `HAL_SPI_Transmit()` | 835-839 | `SPI_1LINE_TX()` 设置 BIDIOE=1 |
| `HAL_SPI_Transmit_DMA()` | 1734-1738 | 同上，DMA 正常工作 |

**接收（读取寄存器）：**

| 函数 | 行号 | 1-line 行为 |
|------|------|------------|
| `HAL_SPI_Receive()` | 1016-1020 | `SPI_1LINE_RX()` 设置 BIDIOE=0，SPI 自动产生时钟 |
| `SPI_EndRxTransaction()` | 3637-3641 | 1-line 主模式自动禁用 SPI 停止时钟 |

**方向切换时序：**

```
1. HAL_SPI_Transmit 发送命令（BIDIOE=1, MOSI 输出）
   -> 最后一个 SCL 周期结束，BSY 清除，SCL 停止

2. HAL_SPI_Receive 开始：
   -> __HAL_SPI_DISABLE（SPE=0, MOSI 释放为 high-Z）
   -> SPI_1LINE_RX（BIDIOE=0）
   -> __HAL_SPI_ENABLE（SPE=1, 自动产生新 SCL 时钟）

3. ST7735 在新 SCL 下降沿输出数据 -> MCU 在 SCL 上升沿读取

4. SPI_EndRxTransaction：
   -> __HAL_SPI_DISABLE（停止时钟）
   -> 等待 BSY 清除
```

ST7735 要求 "SDA must be set to tri-state no later than at the falling edge of SCL of the last bit"。步骤 2 中 SPI 禁用瞬间 MOSI 即变为 high-Z，满足此要求。

### 2.4 Pipeline DMA 影响

Pipeline 使用 `HAL_SPI_Transmit_DMA()` 发送帧数据（`pipeline.c:156`）。在 1-line 模式下：
- DMA 发送时 BIDIOE=1（输出方向），行为与 2LINES 模式完全一致
- 不涉及方向切换，DMA 传输不受影响
- `HAL_SPI_TxCpltCallback` 回调正常触发

读取操作只在 `LCD_Init()` 初始化阶段执行，与 Pipeline 帧 DMA 不并发，无冲突。

### 2.5 HAL 驱动限制分析

HAL 注释（line 66-70）提到主模式 BIDIOE=0 下需要 DeInit/Init 来避免意外传输。此限制仅影响**连续多次接收**场景。对于单次读取（发送命令 -> 接收 1 字节 -> CS 拉高），`SPI_EndRxTransaction` 会在接收完成后禁用 SPI 停止时钟，不会出现意外传输。

## 3. 设计方案

### 3.1 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `Core/Src/main.c` | `MX_SPI2_Init()` 中 Direction 改为 `SPI_DIRECTION_1LINE` |
| `Core/Src/bsp/st7735.c` | 添加 `ReadReg()` 静态函数；`LCD_Init()` 中读取 RDID1 验证 |
| `Core/Inc/bsp/st7735.h` | 声明 `LCD_ReadReg()` 公开 API |

### 3.2 SPI 配置变更

```c
// Core/Src/main.c MX_SPI2_Init()
hspi2.Init.Direction = SPI_DIRECTION_1LINE;  // 原: SPI_DIRECTION_2LINES
```

其余参数（Mode/DataSize/CLKPolarity/CLKPhase/NSS/BaudRatePrescaler/FirstBit）不变。

### 3.3 ST7735 寄存器读取函数

新增静态函数 `ReadReg()`：

```c
/**
  * @brief  Read an 8-bit register from ST7735 via SPI half-duplex
  * @param  cmd  Register read command (e.g. 0xDA for RDID1)
  * @retval Register value
  *
  *         Sequence: CS low -> DC low -> send cmd -> SPI switches to
  *         receive mode (BIDIOE=0) -> read 1 byte -> CS high.
  *         Requires SPI_DIRECTION_1LINE in MX_SPI2_Init.
  */
static uint8_t ReadReg(uint8_t cmd)
{
  uint8_t value = 0u;

  LCD_CS_Low();
  LCD_DC_Low();            /* Command mode */

  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi2, &value, 1u, HAL_MAX_DELAY);

  LCD_CS_High();
  return value;
}
```

新增公开函数 `LCD_ReadReg()` 包装 `ReadReg()`，供外部调试调用。

### 3.4 LCD_Init() 中添加 ID 验证

在硬件复位后、初始化序列前读取 RDID1：

```c
void LCD_Init(void)
{
  /* Backlight on */
  LCD_BL_On();

  /* Hardware reset */
  LCD_RESET_Low();
  DWT_DelayUs(10u);
  LCD_RESET_High();
  DWT_DelayMs(120u);

  /* Verify SPI communication by reading RDID1 */
  uint8_t id = LCD_ReadReg(ST7735_CMD_RDID1);
  if (id != ST7735_RDID1_EXPECTED)
  {
    Error_Handler();
  }

  /* ... existing init sequence ... */
}
```

ST7735 RDID1（命令 0xDA）的期望值：`0x7C`（ST7735 手册 Page 71）。

### 3.5 错误处理

- RDID1 不匹配 -> `Error_Handler()` 终止（与 OV7670 SCCB 失败处理一致）
- `#ifdef DEBUG` 下通过 `debug_printf` 输出实际读取的 ID 值，便于调试

## 4. 验证计划

1. 编译通过（CMake Debug preset）
2. RAM/FLASH 占用无显著变化（读取函数仅初始化阶段使用，不增加运行时开销）
3. 实机测试：上电后 LCD_Init 读取 RDID1 = 0x7C，通过验证进入正常显示
4. Pipeline 帧传输不受影响（DMA 发送行为不变）
