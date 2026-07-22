# SPI 1-line ST7735 寄存器读取 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 LCD_Init 中读取 ST7735 RDID1 寄存器验证 SPI 通信，利用 SPI BIDIMODE 单线双向模式。

**Architecture:** SPI2 已由 CubeMX 改为 Half-Duplex（SPI_DIRECTION_1LINE）。在 st7735.c 中新增 ReadReg 静态函数通过 HAL_SPI_Transmit/Receive 实现半双工读取，LCD_Init 中调用读取 RDID1（0xDA，期望 0x7C）验证，失败则 Error_Handler。

**Tech Stack:** STM32 HAL, C11, CMake + arm-none-eabi-gcc

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `Core/Src/bsp/st7735.c` | 修改 | 添加 ReadReg 静态函数、RDID1 常量、LCD_Init 验证逻辑 |
| `Core/Inc/bsp/st7735.h` | 修改 | 声明 LCD_ReadReg 公开 API |

> CubeMX 生成的 `Core/Src/main.c`（SPI_DIRECTION_1LINE）和 `.ioc` 已由用户完成，不在本计划范围内。

---

### Task 1: st7735.c 添加 ReadReg 函数和 RDID1 验证

**Files:**
- Modify: `Core/Src/bsp/st7735.c`

- [ ] **Step 1: 添加 includes 和 RDID1 命令常量**

在 `st7735.c` 文件顶部 includes 区添加 `debug.h`：

```c
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "stm32f1xx_hal.h"
```

在现有命令常量区块（`ST7735_CMD_RAMWR` 之后）添加：

```c
#define ST7735_CMD_RDID1   0xDAu
#define ST7735_RDID1_EXPECTED  0x7Cu
```

- [ ] **Step 2: 添加 ReadReg 静态函数**

在 `WriteData` 函数之后、`LCD_Init` 函数之前添加：

```c
/**
  * @brief   Read an 8-bit register from ST7735 via SPI half-duplex
  * @param   cmd  Register read command (e.g. 0xDA for RDID1)
  * @retval  Register value
  *
  *          Sequence: CS low -> DC low -> send cmd (BIDIOE=1) ->
  *          SPI switches to receive (BIDIOE=0) -> read 1 byte -> CS high.
  *          Requires SPI_DIRECTION_1LINE in MX_SPI2_Init.
  */
static uint8_t ReadReg(uint8_t cmd)
{
  uint8_t value = 0u;

  LCD_CS_Low();
  LCD_DC_Low();

  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi2, &value, 1u, HAL_MAX_DELAY);

  LCD_CS_High();
  return value;
}
```

- [ ] **Step 3: 在 LCD_Init 中添加 RDID1 验证**

在 `LCD_Init` 函数中，硬件复位延时之后、`LCD_CS_Low()` 初始化序列之前，插入 RDID1 读取验证：

```c
  /* Verify SPI communication by reading RDID1 */
  uint8_t id = ReadReg(ST7735_CMD_RDID1);
  debug_printf("LCD RDID1=0x%02X (expect 0x%02X)\n", id, ST7735_RDID1_EXPECTED);
  if (id != ST7735_RDID1_EXPECTED)
  {
    Error_Handler();
  }
```

插入位置：在 `DWT_DelayMs(120u);`（复位后延时）之后，`LCD_CS_Low();`（初始化序列开始）之前。

> `debug_printf` 在 Debug 构建中输出实际读取的 ID 值，便于调试。Release 构建中编译为空操作。LCD_Init 不在中断上下文中，可安全使用阻塞 UART 输出。

- [ ] **Step 4: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功，0 errors, 0 warnings。RAM ~15%，FLASH ~30%。

- [ ] **Step 5: 提交**

```bash
git add Core/Src/bsp/st7735.c
git commit -m "feat: add ST7735 RDID1 readback verification in LCD_Init

Add ReadReg() static function using SPI half-duplex (BIDIMODE)
to read ST7735 registers via MOSI bidirectional line.
LCD_Init() now reads RDID1 (0xDA) and verifies expected value
0x7C before proceeding with initialization."
```

---

### Task 2: st7735.h 声明公开 API

**Files:**
- Modify: `Core/Inc/bsp/st7735.h`

- [ ] **Step 1: 添加 LCD_ReadReg 公开函数声明**

在 `st7735.h` 的 `LCD_SetAddrWindow` 声明之后、`#endif` 之前添加：

```c
/** @brief  Read an 8-bit register from ST7735
  * @param  cmd  Register read command (e.g. 0xDA for RDID1)
  * @retval Register value
  *
  *         Uses SPI half-duplex (BIDIMODE) to read via MOSI.
  *         Requires SPI_DIRECTION_1LINE configuration.
  */
uint8_t  LCD_ReadReg(uint8_t cmd);
```

- [ ] **Step 2: 在 st7735.c 中添加 LCD_ReadReg 包装函数**

在 `ReadReg` 函数之后添加公开包装函数：

```c
uint8_t LCD_ReadReg(uint8_t cmd)
{
  return ReadReg(cmd);
}
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功，0 errors, 0 warnings。

- [ ] **Step 4: 提交**

```bash
git add Core/Inc/bsp/st7735.h Core/Src/bsp/st7735.c
git commit -m "feat: expose LCD_ReadReg public API for ST7735 register readback"
```

---

### Task 3: 更新头文件注释

**Files:**
- Modify: `Core/Inc/bsp/st7735.h`

- [ ] **Step 1: 更新文件头注释**

将头文件注释中的 "SPI2 18MHz 4-line serial interface." 更新为：

```c
 *          SPI2 18MHz 4-line serial interface (Half-Duplex BIDIMODE).
```

- [ ] **Step 2: 编译验证**

Run: `cmake --build --preset Debug`
Expected: 编译成功。

- [ ] **Step 3: 提交**

```bash
git add Core/Inc/bsp/st7735.h
git commit -m "docs: update st7735.h header to reflect Half-Duplex BIDIMODE"
```
