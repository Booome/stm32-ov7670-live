# ST7735 LCD 单元测试设计

## 1. 概述

### 1.1 目标

针对新连接的 ST7735 LCD 屏幕，设计全面的单元测试，验证：

1. **连接验证**：SPI 通信、RDID 寄存器读取、引脚控制（CS/DC/BL/RESET）
2. **功能验证**：初始化序列、地址窗口、像素写入、显示图案
3. **边界条件**：地址窗口边界值、DMA 传输超时处理
4. **DMA 通路验证**：SPI DMA + LCD 配合，ping-pong 双缓冲

### 1.2 约束

- MCU: STM32F103C8T6 (20KB RAM, 64KB FLASH)
- 屏幕全帧 160x128x2 = 40960 字节，远超 RAM，必须逐行渲染
- 测试框架: Unity (ThirdParty/Unity)
- SPI2: 半双工 BIDIMODE 1-line, 18MHz

### 1.3 两个测试组

| 测试组 | CMake 选项 | SPI 传输方式 | 文件 |
|--------|-----------|-------------|------|
| TEST_LCD | `TEST_LCD` | 阻塞 `HAL_SPI_Transmit` | `test_lcd.c/h` |
| TEST_LCD_DMA | `TEST_LCD_DMA` | DMA `HAL_SPI_Transmit_DMA` + ping-pong | `test_lcd_dma.c/h` |

两个测试组相互独立，分别编译运行。TEST_LCD 验证基础连接和功能，TEST_LCD_DMA 验证 DMA 通路与 LCD 的配合。

## 2. 驱动新增函数

在 `st7735.c/h` 中新增两个 I/O 函数，不修改现有 API。

### 2.1 LCD_ReadRegMulti

```c
/**
 * @brief   Read multiple bytes from ST7735 register
 * @param   cmd   Read command (e.g. 0x0B for RDD_MADCTL, returns 2 bytes)
 * @param   data  Buffer to store received bytes
 * @param   len   Number of bytes to read
 * @note    RDD_MADCTL/RDD_COLMOD return [dummy, value]; RDID1/2/3 return [value]
 */
void LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len);
```

**实现逻辑**：与现有 `ReadReg` 相同的 CS/DC 控制序列，但调用 `HAL_SPI_Receive(&hspi2, data, len, HAL_MAX_DELAY)` 读取多字节。

**用途**：RDD_MADCTL (0x0B) 和 RDD_COLMOD (0x0C) 返回 2 字节（dummy + value），现有 `LCD_ReadReg` 只读 1 字节无法获取实际值。

### 2.2 LCD_WritePixels

```c
/**
 * @brief   Send pixel data to LCD via SPI (blocking)
 * @param   data  RGB565 pixel data
 * @param   len   Number of bytes to send
 * @note    Caller must call LCD_SetAddrWindow() first (leaves CS low, DC high).
 *          Caller raises CS after all pixel data is sent.
 */
void LCD_WritePixels(const uint8_t *data, uint16_t len);
```

**实现逻辑**：`HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY)` 的薄封装。

**CS/DC 管理约定**：保持现有模式不变 - `LCD_SetAddrWindow()` 负责 CS low + DC high，`LCD_WritePixels()` 不碰 CS/DC，调用者在完成后调 `LCD_CS_High()`。

## 3. TEST_LCD 测试组（阻塞 SPI）

### 3.1 测试流程

```
RunLcdTests()
  |
  |- 1. DWT_Init() + LCD_Init()
  |     (LCD_Init 内含 reset + RDID1 校验, 失败则 Error_Handler 不返回)
  |
  |- 2. Unity assert 测试
  |     |- test_lcd_rdid1
  |     |- test_lcd_rdid2_rdid3
  |     |- test_lcd_madctl
  |     |- test_lcd_colmod
  |     |- test_lcd_addr_window
  |
  |- 3. UNITY_END() (UART 输出结果摘要)
  |
  +- 4. 视觉测试循环 (永不返回)
        |- 图案 1-9, 每个持续 ~3 秒, 循环
```

### 3.2 Unity assert 测试用例

| # | 测试函数 | 验证内容 | Assert |
|---|---------|---------|--------|
| 1 | `test_lcd_rdid1` | SPI 通信基础验证 | `TEST_ASSERT_EQUAL(0x7C, LCD_ReadReg(0xDA))` |
| 2 | `test_lcd_rdid2_rdid3` | 额外 ID 寄存器 | `TEST_ASSERT_NOT_EQUAL(0x00, val)` + `TEST_ASSERT_NOT_EQUAL(0xFF, val)` |
| 3 | `test_lcd_madctl` | MADCTL 写入回读 | `LCD_ReadRegMulti(0x0B, buf, 2)` -> `TEST_ASSERT_EQUAL(0x28, buf[1])` |
| 4 | `test_lcd_colmod` | COLMOD 写入回读 | `LCD_ReadRegMulti(0x0C, buf, 2)` -> `TEST_ASSERT_EQUAL(0x05, buf[1])` |
| 5 | `test_lcd_addr_window` | 地址窗口边界 | 3 种边界各设窗口+写1像素+CS high, 全部完成则 `TEST_PASS()` |

**各用例设计理由**：

- **用例 1 (RDID1)**：0x7C 是 ST7735 制造商 ID，值错误说明接线/SPI 时序有问题。
- **用例 2 (RDID2/RDID3)**：值因模块而异，断言非 0x00（无响应）和非 0xFF（通信失败）即可验证通信有效。
- **用例 3 (MADCTL 回读)**：`LCD_Init()` 写入 MADCTL=0x28，通过 RDD_MADCTL (0x0B) 读回验证。ST7735 的 RDD 命令返回 2 字节 `[dummy, value]`。
- **用例 4 (COLMOD 回读)**：`LCD_Init()` 写入 COLMOD=0x05，通过 RDD_COLMOD (0x0C) 读回验证。
- **用例 5 (地址窗口边界)**：
  - 全屏 `(0, 0, 159, 127)` -> 写 1 像素 -> `LCD_CS_High()`
  - 单像素 `(0, 0, 0, 0)` -> 写 1 像素 -> `LCD_CS_High()`
  - 末像素 `(159, 127, 159, 127)` -> 写 1 像素 -> `LCD_CS_High()`

### 3.3 视觉测试图案

逐行渲染，320 字节行缓冲（160 像素 x 2 字节 RGB565）：

```
LCD_SetAddrWindow(0, 0, 159, 127)   // CS low, DC high
for y in 0..127:
    FillLine(line_buf, y, pattern)   // 填充图案
    OverlayDigit(line_buf, y, digit) // 叠加数字
    LCD_WritePixels(line_buf, 320)   // 阻塞发送一行
LCD_CS_High()
HAL_Delay(3000)                      // 持续 3 秒
```

**9 个图案**：

| # | 图案 | 内容 |
|---|------|------|
| 1 | 纯红 | 全屏 0xF800 |
| 2 | 纯绿 | 全屏 0x07E0 |
| 3 | 纯蓝 | 全屏 0x001F |
| 4 | 纯白 | 全屏 0xFFFF |
| 5 | 纯黑 | 全屏 0x0000 |
| 6 | 水平条纹 | 5 条横向色带（红/绿/蓝/白/黑），每带 ~26 行 |
| 7 | 垂直条纹 | 5 条纵向色带（红/绿/蓝/白/黑），每带 32 列 |
| 8 | 棋盘格 | 20x20 像素方块，红白交替 |
| 9 | RGB 渐变 | 三段横向渐变：上 1/3 红、中 1/3 绿、下 1/3 蓝（左->右 0->max） |

### 3.4 数字标号渲染

**字体**：5x7 点阵，仅含数字 0-9（70 字节 ROM）

**缩放**：4 倍放大 -> 屏幕上 20x28 像素

**位置**：居中，x_start = (160-20)/2 = 70, y_start = (128-28)/2 = 50

**可见性**：先在字体区域绘制黑色底框（2px 边距，x=[68,91] y=[48,79]），再用白色绘制数字。

**字体数据**：

```c
static const uint8_t kFont5x7[10][7] = {
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},  /* 0 */
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},  /* 1 */
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},  /* 2 */
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},  /* 3 */
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},  /* 4 */
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},  /* 5 */
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},  /* 6 */
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},  /* 7 */
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},  /* 8 */
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},  /* 9 */
};
```

每行渲染流程：

1. 用图案颜色填充整行 `line_buf[0..319]`
2. 若 y 在 `[48,79]`：`line_buf[68..91]` 填黑色（底框）
3. 若 y 在 `[50,77]`：`font_row = (y - 50) / 4`，对每个字体列 fc (0-4)，若 `kFont5x7[digit][font_row] & (0x10 >> fc)`，则 `line_buf[70+fc*4 .. 70+fc*4+7]` 填白色

## 4. TEST_LCD_DMA 测试组（DMA + ping-pong）

### 4.1 测试流程

```
RunLcdDmaTests()
  |
  |- 1. DWT_Init() + LCD_Init()
  |- 2. HAL_SPI_RegisterCallback() 注册 DMA 回调
  |
  |- 3. Unity assert 测试
  |     |- test_lcd_dma_basic
  |     |- test_lcd_dma_full_frame
  |
  |- 4. UNITY_END()
  |
  +- 5. DMA 视觉测试循环 (永不返回)
        |- 图案 1-9, DMA ping-pong 发送, 每个持续 ~3 秒, 循环
```

### 4.2 DMA 回调注册

`pipeline.c` 已覆盖 `HAL_SPI_TxCpltCallback`（设置 `s_spi_dma_busy = false`），且始终参与编译。为避免冲突，通过 `HAL_SPI_RegisterCallback` 注册自己的回调：

```c
static volatile bool s_dma_done;

static void DmaTxCpltCallback(SPI_HandleTypeDef *hspi)
{
  (void)hspi;
  s_dma_done = true;
}

/* 在 RunLcdDmaTests() 开头注册 */
HAL_SPI_RegisterCallback(&hspi2, HAL_SPI_TX_COMPLETE_CB_ID, DmaTxCpltCallback);
```

测试中 `Pipeline_Init()` 未调用，pipeline 状态为 `DISABLED`，其回调即使被触发也只是无害地写一个 static 变量。

### 4.3 Unity assert 测试用例

| # | 测试函数 | 验证内容 | Assert |
|---|---------|---------|--------|
| 1 | `test_lcd_dma_basic` | 单行 DMA 传输 (320B) | `TEST_ASSERT_EQUAL(HAL_OK, status)` + 超时内 `s_dma_done` 置位 |
| 2 | `test_lcd_dma_full_frame` | 128 行逐行 DMA 传输 | 每行断言 `HAL_OK` + 全帧无超时 |

**test_lcd_dma_basic 伪代码**：

```c
void test_lcd_dma_basic(void)
{
  static uint8_t dma_buf[320];
  /* fill with red */
  for (uint16_t i = 0; i < 160u; i++) {
    dma_buf[i * 2u]     = 0xF8u;
    dma_buf[i * 2u + 1u] = 0x00u;
  }

  LCD_SetAddrWindow(0u, 0u, 159u, 0u);  /* 1 line */

  s_dma_done = false;
  HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf, 320u);
  TEST_ASSERT_EQUAL(HAL_OK, status);

  uint32_t deadline = HAL_GetTick() + 100u;
  while (!s_dma_done) {
    if (HAL_GetTick() > deadline) {
      TEST_FAIL_MESSAGE("SPI DMA timeout");
      return;
    }
  }

  LCD_CS_High();
  TEST_PASS();
}
```

**test_lcd_dma_full_frame 伪代码**：

```c
void test_lcd_dma_full_frame(void)
{
  static uint8_t dma_buf[320];
  LCD_SetAddrWindow(0u, 0u, 159u, 127u);

  for (uint16_t y = 0; y < 128u; y++) {
    /* fill with green */
    for (uint16_t i = 0; i < 160u; i++) {
      dma_buf[i * 2u]     = 0x07u;
      dma_buf[i * 2u + 1u] = 0xE0u;
    }

    s_dma_done = false;
    HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf, 320u);
    TEST_ASSERT_EQUAL(HAL_OK, status);

    uint32_t deadline = HAL_GetTick() + 100u;
    while (!s_dma_done) {
      if (HAL_GetTick() > deadline) {
        TEST_FAIL_MESSAGE("SPI DMA full-frame timeout");
        return;
      }
    }
  }

  LCD_CS_High();
  TEST_PASS();
}
```

### 4.4 DMA ping-pong 视觉图案

双缓冲（各 320B），交替使用：

```
DMA ping-pong 流程 (填充与传输并行):
  1. 填充 buf_a (line 0)
  2. s_dma_done = false
  3. HAL_SPI_Transmit_DMA(buf_a, 320)   -- DMA 开始发送 buf_a
  4. 填充 buf_b (line 1)                 -- CPU 填充 buf_b (与 DMA 并行)
  5. while (!s_dma_done) {}             -- 等待 buf_a DMA 完成
  6. s_dma_done = false
  7. HAL_SPI_Transmit_DMA(buf_b, 320)   -- DMA 开始发送 buf_b
  8. 填充 buf_a (line 2)                 -- CPU 填充 buf_a (与 DMA 并行)
  9. while (!s_dma_done) {}             -- 等待 buf_b DMA 完成
  10. 交替 step 2-9 直到 128 行完成
  11. LCD_CS_High()
```

关键：step 4/8 的 CPU 填充与 step 3/7 的 DMA 传输并行执行，这才是 ping-pong 的核心价值。虽然行填充仅 ~10us（远小于 DMA 传输 ~142us），但验证了异步 DMA 通路正确工作。

图案与 TEST_LCD 相同（9 个图案，每个 3 秒，中央显示数字标号）。区别仅在于像素数据通过 DMA 发送而非阻塞 SPI。

## 5. 文件结构

```
Core/Inc/bsp/
  st7735.h              # 新增 LCD_ReadRegMulti, LCD_WritePixels 声明
Core/Src/bsp/
  st7735.c              # 新增 LCD_ReadRegMulti, LCD_WritePixels 实现

Core/Inc/test/
  test_lcd.h            # TEST_LCD 组声明
  test_lcd_dma.h        # TEST_LCD_DMA 组声明
Core/Src/test/
  test_lcd.c            # TEST_LCD 组实现 (阻塞 SPI)
  test_lcd_dma.c        # TEST_LCD_DMA 组实现 (DMA ping-pong)

Core/Inc/test/test_runner.h   # 无修改 (已有桩)
Core/Src/test/test_runner.c   # 新增 TEST_LCD_DMA 调度
CMakeLists.txt                # 新增 TEST_LCD_DMA option + sources
```

## 6. CMake 集成

### 6.1 新增 option

```cmake
option(TEST_LCD_DMA  "Enable LCD DMA hardware tests"  OFF)
```

### 6.2 条件判断更新

```cmake
if(TEST_TEST_LED OR TEST_LOGIC OR TEST_SCCB OR TEST_OV7670 OR TEST_LCD OR TEST_LCD_DMA)
    set(_unit_tests ON)
endif()
```

### 6.3 TEST_LCD_DMA source 注册

```cmake
if(TEST_LCD_DMA)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd_dma.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LCD_DMA)
endif()
```

## 7. test_runner.c 新增调度

```c
#ifdef TEST_LCD_DMA
#include "test_lcd_dma.h"
#endif
// ...
#ifdef TEST_LCD_DMA
  RunLcdDmaTests();
#endif
```

## 8. RAM/Flash 预算

| 资源 | 大小 | 说明 |
|------|------|------|
| 行缓冲 `s_line_buf[320]` | 320B | TEST_LCD 单缓冲 |
| DMA 双缓冲 `s_dma_buf_a[320]` + `s_dma_buf_b[320]` | 640B | TEST_LCD_DMA ping-pong |
| 字体 ROM `kFont5x7` | 70B (flash) | 共享于两个测试组 |
| 局部变量 | ~20B | 循环计数等 |
| **合计 RAM (TEST_LCD)** | **~390B** | 远低于 20KB |
| **合计 RAM (TEST_LCD_DMA)** | **~710B** | 远低于 20KB |

## 9. 构建命令

```bash
# TEST_LCD (阻塞 SPI)
cmake --preset Debug -DTEST_LCD=ON && cmake --build --preset Debug

# TEST_LCD_DMA (DMA + ping-pong)
cmake --preset Debug -DTEST_LCD_DMA=ON && cmake --build --preset Debug
```
