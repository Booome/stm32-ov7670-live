# ST7735 LCD Unit Tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement two LCD test groups (TEST_LCD blocking SPI + TEST_LCD_DMA ping-pong) with Unity asserts and visual pattern loops for the ST7735 display.

**Architecture:** Driver gets two new I/O functions (LCD_ReadRegMulti, LCD_WritePixels). Shared test utilities (font, pattern fill, digit overlay) live in test_lcd_common. Each test group has its own entry point and SPI transfer strategy.

**Tech Stack:** C11, STM32 HAL, Unity test framework, CMake

## Global Constraints

- MCU: STM32F103C8T6 (20KB RAM, 64KB FLASH)
- Indent: 2 spaces, no tabs
- Braces: Allman style (opening brace on new line)
- Numeric constants with `u` suffix: `0x14u`, `40960u`
- Pointer `*` adjacent to variable name: `uint8_t *ptr`
- BSP public functions: Module_Action() PascalCase
- BSP static functions: Action() PascalCase
- No Chinese characters in code or comments
- Doxygen style comments for functions
- Build: `cmake --preset Debug && cmake --build --preset Debug`
- Worktree: `.worktrees/lcd-unit-tests` on branch `feat/lcd-unit-tests`

---

### Task 1: Driver Additions (LCD_ReadRegMulti + LCD_WritePixels)

**Files:**
- Modify: `Core/Inc/bsp/st7735.h` (add 2 declarations after existing LCD_ReadReg)
- Modify: `Core/Src/bsp/st7735.c` (add 2 implementations after existing LCD_ReadReg)

**Interfaces:**
- Consumes: `hspi2` (extern SPI handle in st7735.c), `HAL_SPI_Receive`, `HAL_SPI_Transmit`
- Produces: `LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len)` and `LCD_WritePixels(const uint8_t *data, uint16_t len)` - used by Task 2 and Task 3

- [ ] **Step 1: Add declarations to st7735.h**

In `Core/Inc/bsp/st7735.h`, after the existing `LCD_ReadReg` declaration (before `#endif`), add:

```c
/** @brief  Read multiple bytes from ST7735 register
  * @param  cmd   Read command (e.g. 0x0B for RDD_MADCTL, returns 2 bytes)
  * @param  data  Buffer to store received bytes
  * @param  len   Number of bytes to read
  * @note   RDD_MADCTL/RDD_COLMOD return [dummy, value]; RDID1/2/3 return [value]
  */
void    LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len);

/** @brief  Send pixel data to LCD via SPI (blocking)
  * @param  data  RGB565 pixel data
  * @param  len   Number of bytes to send
  * @note   Caller must call LCD_SetAddrWindow() first (leaves CS low, DC high).
  *         Caller raises CS after all pixel data is sent.
  */
void    LCD_WritePixels(const uint8_t *data, uint16_t len);
```

- [ ] **Step 2: Add LCD_ReadRegMulti implementation to st7735.c**

In `Core/Src/bsp/st7735.c`, after the existing `LCD_ReadReg` function (after line 88), add:

```c
void LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len)
{
  LCD_CS_Low();
  LCD_DC_Low();

  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi2, data, len, HAL_MAX_DELAY);

  LCD_CS_High();
}
```

- [ ] **Step 3: Add LCD_WritePixels implementation to st7735.c**

After `LCD_ReadRegMulti`, add:

```c
void LCD_WritePixels(const uint8_t *data, uint16_t len)
{
  HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY);
}
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --preset Debug && cmake --build --preset Debug`
Expected: Build succeeds with no errors or warnings.

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/bsp/st7735.h Core/Src/bsp/st7735.c
git commit -m "feat: add LCD_ReadRegMulti and LCD_WritePixels to ST7735 driver"
```

---

### Task 2: TEST_LCD Test Group (Blocking SPI)

**Files:**
- Create: `Core/Inc/test/test_lcd_common.h` (shared font, colors, function declarations)
- Create: `Core/Src/test/test_lcd_common.c` (FillLine, OverlayDigit implementations)
- Create: `Core/Inc/test/test_lcd.h` (RunLcdTests declaration)
- Create: `Core/Src/test/test_lcd.c` (Unity asserts + blocking SPI visual loop)
- Modify: `CMakeLists.txt` (add test_lcd_common.c to TEST_LCD sources)

**Interfaces:**
- Consumes: `LCD_Init`, `LCD_ReadReg`, `LCD_ReadRegMulti`, `LCD_WritePixels`, `LCD_SetAddrWindow`, `LCD_CS_High`, `DWT_Init`, `hspi2`, Unity macros
- Produces: `RunLcdTests()` (called by test_runner.c), `LcdTest_FillLine()`, `LcdTest_OverlayDigit()` (shared with Task 3)

- [ ] **Step 1: Create test_lcd_common.h**

Create `Core/Inc/test/test_lcd_common.h`:

```c
/**
  * @file    test_lcd_common.h
  * @brief   Shared LCD test utilities (font, pattern fill, digit overlay)
  */
#ifndef TEST_LCD_COMMON_H
#define TEST_LCD_COMMON_H

#include <stdint.h>

#define LCD_TEST_RED     0xF800u
#define LCD_TEST_GREEN   0x07E0u
#define LCD_TEST_BLUE    0x001Fu
#define LCD_TEST_WHITE   0xFFFFu
#define LCD_TEST_BLACK   0x0000u

#define LCD_TEST_WIDTH      160u
#define LCD_TEST_HEIGHT     128u
#define LCD_TEST_LINE_SIZE  (LCD_TEST_WIDTH * 2u)
#define LCD_TEST_PATTERN_COUNT  9u

#define LCD_TEST_FONT_SCALE    4u
#define LCD_TEST_FONT_WIDTH    5u
#define LCD_TEST_FONT_HEIGHT   7u
#define LCD_TEST_FONT_X_START  70u
#define LCD_TEST_FONT_Y_START  50u

void LcdTest_FillLine(uint8_t *buf, uint16_t y, uint8_t pattern_id);
void LcdTest_OverlayDigit(uint8_t *buf, uint16_t y, uint8_t digit);

#endif /* TEST_LCD_COMMON_H */
```

- [ ] **Step 2: Create test_lcd_common.c**

Create `Core/Src/test/test_lcd_common.c` with font data, SetPixel, FillSolid helpers, LcdTest_FillLine (9 patterns: solid red/green/blue/white/black, horizontal stripes, vertical stripes, checkerboard, RGB gradient), and LcdTest_OverlayDigit (black bg box + white font). See spec section 3.3-3.4 for pattern and font details.

Key implementation points:
- `static const uint8_t kFont5x7[10][7]` - 5x7 bitmap font for digits 0-9
- `static void SetPixel(uint8_t *buf, uint16_t x, uint16_t color)` - writes RGB565 big-endian
- `static void FillSolid(uint8_t *buf, uint16_t color)` - fills 160 pixels
- `LcdTest_FillLine` - switch on pattern_id (0-8), each case fills the line
- `LcdTest_OverlayDigit` - draws black box at y=[48,79] x=[68,91], then white font pixels at y=[50,77] x=[70,89] scaled 4x

- [ ] **Step 3: Create test_lcd.h**

Create `Core/Inc/test/test_lcd.h`:

```c
/**
  * @file    test_lcd.h
  * @brief   LCD hardware test group (blocking SPI)
  */
#ifndef TEST_LCD_H
#define TEST_LCD_H

void RunLcdTests(void);

#endif /* TEST_LCD_H */
```

- [ ] **Step 4: Create test_lcd.c**

Create `Core/Src/test/test_lcd.c` with:
1. `RunLcdTests()` entry point
2. DWT_Init() + LCD_Init() (if LCD_Init fails, Error_Handler never returns)
3. 5 Unity test functions (see below)
4. `UNITY_BEGIN()` / `RUN_TEST()` x5 / `UNITY_END()`
5. Visual loop: 9 patterns, each 3 seconds, using blocking SPI

Unity test functions:

```c
void test_lcd_rdid1(void)
{
  TEST_ASSERT_EQUAL(0x7Cu, LCD_ReadReg(0xDAu));
}

void test_lcd_rdid2_rdid3(void)
{
  uint8_t rdid2 = LCD_ReadReg(0xDBu);
  uint8_t rdid3 = LCD_ReadReg(0xDCu);
  TEST_ASSERT_NOT_EQUAL(0x00u, rdid2);
  TEST_ASSERT_NOT_EQUAL(0xFFu, rdid2);
  TEST_ASSERT_NOT_EQUAL(0x00u, rdid3);
  TEST_ASSERT_NOT_EQUAL(0xFFu, rdid3);
}

void test_lcd_madctl(void)
{
  uint8_t buf[2];
  LCD_ReadRegMulti(0x0Bu, buf, 2u);
  TEST_ASSERT_EQUAL(0x28u, buf[1]);
}

void test_lcd_colmod(void)
{
  uint8_t buf[2];
  LCD_ReadRegMulti(0x0Cu, buf, 2u);
  TEST_ASSERT_EQUAL(0x05u, buf[1]);
}

void test_lcd_addr_window(void)
{
  uint8_t pixel[2] = {0xF8u, 0x00u};  /* red */

  LCD_SetAddrWindow(0u, 0u, 159u, 127u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  LCD_SetAddrWindow(0u, 0u, 0u, 0u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  LCD_SetAddrWindow(159u, 127u, 159u, 127u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  TEST_PASS();
}
```

Visual loop (after UNITY_END):

```c
static uint8_t s_line_buf[LCD_TEST_LINE_SIZE];

static void DrawFrameBlocking(uint8_t pattern_id)
{
  uint8_t digit = pattern_id + 1u;
  LCD_SetAddrWindow(0u, 0u, LCD_TEST_WIDTH - 1u, LCD_TEST_HEIGHT - 1u);
  for (uint16_t y = 0u; y < LCD_TEST_HEIGHT; y++)
  {
    LcdTest_FillLine(s_line_buf, y, pattern_id);
    LcdTest_OverlayDigit(s_line_buf, y, digit);
    LCD_WritePixels(s_line_buf, LCD_TEST_LINE_SIZE);
  }
  LCD_CS_High();
}

/* in RunLcdTests, after UNITY_END(): */
for (;;)
{
  for (uint8_t p = 0u; p < LCD_TEST_PATTERN_COUNT; p++)
  {
    DrawFrameBlocking(p);
    HAL_Delay(3000u);
  }
}
```

- [ ] **Step 5: Update CMakeLists.txt**

In the `if(TEST_LCD)` block, add test_lcd_common.c:

```cmake
    if(TEST_LCD)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            Core/Src/test/test_lcd.c
            Core/Src/test/test_lcd_common.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LCD)
    endif()
```

Note: test_runner.c already has `#ifdef TEST_LCD` dispatch stubs, no change needed.

- [ ] **Step 6: Build with TEST_LCD=ON**

Run: `cmake --preset Debug -DTEST_LCD=ON && cmake --build --preset Debug`
Expected: Build succeeds. Flash to hardware, verify Unity output on UART and visual patterns on LCD.

- [ ] **Step 7: Commit**

```bash
git add Core/Inc/test/test_lcd_common.h Core/Src/test/test_lcd_common.c \
        Core/Inc/test/test_lcd.h Core/Src/test/test_lcd.c CMakeLists.txt
git commit -m "feat: add TEST_LCD group with Unity asserts and visual patterns"
```

---

### Task 3: TEST_LCD_DMA Test Group (DMA + Ping-Pong)

**Files:**
- Create: `Core/Inc/test/test_lcd_dma.h` (RunLcdDmaTests declaration)
- Create: `Core/Src/test/test_lcd_dma.c` (DMA callback + Unity asserts + DMA ping-pong visual)
- Modify: `CMakeLists.txt` (add TEST_LCD_DMA option + condition + sources)
- Modify: `Core/Src/test/test_runner.c` (add TEST_LCD_DMA dispatch)

**Interfaces:**
- Consumes: `LCD_Init`, `LCD_ReadReg`, `LCD_SetAddrWindow`, `LCD_CS_High`, `DWT_Init`, `hspi2`, `HAL_SPI_Transmit_DMA`, `HAL_SPI_RegisterCallback`, `LcdTest_FillLine`, `LcdTest_OverlayDigit` (from Task 2), Unity macros
- Produces: `RunLcdDmaTests()` (called by test_runner.c)

- [ ] **Step 1: Create test_lcd_dma.h**

Create `Core/Inc/test/test_lcd_dma.h`:

```c
/**
  * @file    test_lcd_dma.h
  * @brief   LCD DMA hardware test group (SPI DMA + ping-pong)
  */
#ifndef TEST_LCD_DMA_H
#define TEST_LCD_DMA_H

void RunLcdDmaTests(void);

#endif /* TEST_LCD_DMA_H */
```

- [ ] **Step 2: Create test_lcd_dma.c**

Create `Core/Src/test/test_lcd_dma.c` with:

1. DMA callback and flag:

```c
#include "test_lcd_dma.h"
#include "test_lcd_common.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "unity.h"
#include "stm32f1xx_hal.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

static volatile bool s_dma_done;

static void DmaTxCpltCallback(SPI_HandleTypeDef *hspi)
{
  (void)hspi;
  s_dma_done = true;
}
```

2. Helper to wait for DMA with timeout:

```c
static bool WaitDmaDone(uint32_t timeout_ms)
{
  uint32_t deadline = HAL_GetTick() + timeout_ms;
  while (!s_dma_done)
  {
    if (HAL_GetTick() > deadline)
    {
      return false;
    }
  }
  return true;
}
```

3. Two Unity test functions:

```c
void test_lcd_dma_basic(void)
{
  static uint8_t dma_buf[LCD_TEST_LINE_SIZE];
  for (uint16_t i = 0u; i < LCD_TEST_WIDTH; i++)
  {
    dma_buf[i * 2u]     = 0xF8u;
    dma_buf[i * 2u + 1u] = 0x00u;
  }

  LCD_SetAddrWindow(0u, 0u, 159u, 0u);
  s_dma_done = false;
  HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf, LCD_TEST_LINE_SIZE);
  TEST_ASSERT_EQUAL(HAL_OK, status);
  TEST_ASSERT_TRUE(WaitDmaDone(100u));
  LCD_CS_High();
  TEST_PASS();
}

void test_lcd_dma_full_frame(void)
{
  static uint8_t dma_buf[LCD_TEST_LINE_SIZE];
  LCD_SetAddrWindow(0u, 0u, 159u, 127u);

  for (uint16_t y = 0u; y < LCD_TEST_HEIGHT; y++)
  {
    for (uint16_t i = 0u; i < LCD_TEST_WIDTH; i++)
    {
      dma_buf[i * 2u]     = 0x07u;
      dma_buf[i * 2u + 1u] = 0xE0u;
    }
    s_dma_done = false;
    HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf, LCD_TEST_LINE_SIZE);
    TEST_ASSERT_EQUAL(HAL_OK, status);
    TEST_ASSERT_TRUE(WaitDmaDone(100u));
  }

  LCD_CS_High();
  TEST_PASS();
}
```

4. DMA ping-pong visual loop:

```c
static uint8_t s_buf_a[LCD_TEST_LINE_SIZE];
static uint8_t s_buf_b[LCD_TEST_LINE_SIZE];

static void DrawFrameDma(uint8_t pattern_id)
{
  uint8_t digit = pattern_id + 1u;
  uint8_t *front = s_buf_a;
  uint8_t *back  = s_buf_b;

  LCD_SetAddrWindow(0u, 0u, LCD_TEST_WIDTH - 1u, LCD_TEST_HEIGHT - 1u);

  /* Line 0: fill front buffer, start DMA */
  LcdTest_FillLine(front, 0u, pattern_id);
  LcdTest_OverlayDigit(front, 0u, digit);
  s_dma_done = false;
  HAL_SPI_Transmit_DMA(&hspi2, front, LCD_TEST_LINE_SIZE);

  for (uint16_t y = 1u; y < LCD_TEST_HEIGHT; y++)
  {
    /* Fill back buffer while DMA sends front (overlap) */
    LcdTest_FillLine(back, y, pattern_id);
    LcdTest_OverlayDigit(back, y, digit);

    /* Wait for front DMA to complete */
    WaitDmaDone(100u);

    /* Swap: start DMA on back, fill front next iteration */
    uint8_t *tmp = front;
    front = back;
    back = tmp;

    s_dma_done = false;
    HAL_SPI_Transmit_DMA(&hspi2, front, LCD_TEST_LINE_SIZE);
  }

  /* Wait for last DMA */
  WaitDmaDone(100u);
  LCD_CS_High();
}
```

5. Entry point:

```c
void RunLcdDmaTests(void)
{
  DWT_Init();
  LCD_Init();

  HAL_SPI_RegisterCallback(&hspi2, HAL_SPI_TX_COMPLETE_CB_ID,
                           DmaTxCpltCallback);

  UNITY_BEGIN();
  RUN_TEST(test_lcd_dma_basic);
  RUN_TEST(test_lcd_dma_full_frame);
  UNITY_END();

  for (;;)
  {
    for (uint8_t p = 0u; p < LCD_TEST_PATTERN_COUNT; p++)
    {
      DrawFrameDma(p);
      HAL_Delay(3000u);
    }
  }
}
```

- [ ] **Step 3: Update CMakeLists.txt**

After the existing `option(TEST_LCD ...)` line, add:

```cmake
option(TEST_LCD_DMA  "Enable LCD DMA hardware tests"               OFF)
```

Update the `_unit_tests` condition:

```cmake
if(TEST_TEST_LED OR TEST_LOGIC OR TEST_SCCB OR TEST_OV7670 OR TEST_LCD OR TEST_LCD_DMA)
```

After the `if(TEST_LCD)` block, add shared source and TEST_LCD_DMA block:

```cmake
    if(TEST_LCD OR TEST_LCD_DMA)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd_common.c)
    endif()
    if(TEST_LCD_DMA)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd_dma.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LCD_DMA)
    endif()
```

Note: Move `test_lcd_common.c` out of the `if(TEST_LCD)` block into the shared `if(TEST_LCD OR TEST_LCD_DMA)` block.

- [ ] **Step 4: Update test_runner.c**

After the existing `#ifdef TEST_LCD` include block, add:

```c
#ifdef TEST_LCD_DMA
#include "test_lcd_dma.h"
#endif
```

After the existing `#ifdef TEST_LCD / RunLcdTests(); / #endif` block, add:

```c
#ifdef TEST_LCD_DMA
  RunLcdDmaTests();
#endif
```

- [ ] **Step 5: Build with TEST_LCD_DMA=ON**

Run: `cmake --preset Debug -DTEST_LCD_DMA=ON && cmake --build --preset Debug`
Expected: Build succeeds. Flash to hardware, verify DMA Unity asserts pass and DMA ping-pong visual patterns display correctly.

- [ ] **Step 6: Commit**

```bash
git add Core/Inc/test/test_lcd_dma.h Core/Src/test/test_lcd_dma.c \
        CMakeLists.txt Core/Src/test/test_runner.c
git commit -m "feat: add TEST_LCD_DMA group with DMA ping-pong visual patterns"
```
