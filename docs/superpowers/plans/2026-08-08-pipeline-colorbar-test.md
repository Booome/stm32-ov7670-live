# OV7670 彩条输出到 LCD 的 Pipeline 测试实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增 `TEST_PIPELINE` 测试组，启用 OV7670 colorbar 彩条测试图案，跑通完整 VSYNC→FIFO→Camera DMA→SPI DMA→LCD 链路，在 LCD 持续显示彩条，串口每秒输出一次 FPS。

**Architecture:** 复用现有 dual-DMA pipeline，仅给 `pipeline.c` 增加一个只读帧计数器作为可观测性扩展；新建不依赖 Unity 的 `test_pipeline.c`，自包含完成全部初始化后进入无限循环：`Pipeline_Poll()` + SysTick 秒窗统计 FPS，打印延迟到 pipeline `IDLE` 状态以避免阻塞打印破坏 1.3ms 读时序。

**Tech Stack:** C11, STM32F103 HAL, CMake, Unity（测试组本身不依赖，仅 test_runner 骨架需要）, arm-none-eabi-gcc

## Global Constraints

- 只在 USER CODE 区域编辑 CubeMX 生成文件；不修改 `cmake/stm32cubemx/` 下的文件
- 代码/注释/变量名全英文；设计文档 `docs/` 下用中文
- 命名规范：BSP 公共函数 `Module_Action()` PascalCase；测试组入口 `Run<Group>Tests()` PascalCase
- 数字常量带 `u` 后缀；中断共享变量加 `volatile`
- CMake TEST_* 选项会缓存，构建时必须显式传所有 `-DTEST_*=...`
- `.bin` 不自动生成，烧录前必须 `arm-none-eabi-objcopy -O binary ...`
- 不修改 pipeline 状态机/时序逻辑（只加计数器）；不引入 Unity 断言到本测试组

---

### Task 1: pipeline 帧计数器扩展

**Files:**
- Modify: `Core/Src/bsp/pipeline.c`（静态变量区、`FrameDone()`、`Pipeline_Init()`、新增 `Pipeline_GetFrameCount()`）
- Modify: `Core/Inc/bsp/pipeline.h`（声明）

**Interfaces:**
- Produces: `uint32_t Pipeline_GetFrameCount(void)` —— 返回自初始化以来完成的帧数（供 Task 3 FPS 统计使用）

- [ ] **Step 1: 在 `pipeline.c` 静态变量区加帧计数器**

`Core/Src/bsp/pipeline.c` 第 23-26 行模块状态区，追加一行：

```c
static volatile uint32_t s_frame_count;
```

- [ ] **Step 2: `FrameDone()` 中递增计数**

`FrameDone()`（`pipeline.c:79-98`）末尾 `s_state = PIPELINE_STATE_IDLE;` 之前插入：

```c
  s_frame_count++;
```

- [ ] **Step 3: `Pipeline_Init()` 中清零计数**

`Pipeline_Init()`（`pipeline.c:100-105`）内追加：

```c
  s_frame_count = 0u;
```

- [ ] **Step 4: 新增 getter 实现**

`Pipeline_GetState()`（`pipeline.c:107-110`）之后追加：

```c
uint32_t Pipeline_GetFrameCount(void)
{
  return s_frame_count;
}
```

- [ ] **Step 5: `pipeline.h` 声明接口**

`Core/Inc/bsp/pipeline.h` 公共 API 区（`Pipeline_GetState` 声明附近，约 78 行）追加：

```c
/** @brief  Get total frames captured since Pipeline_Init */
uint32_t Pipeline_GetFrameCount(void);
```

- [ ] **Step 6: 正常应用构建验证**

```bash
cmake --preset Debug -DTEST_TEST_LED=OFF -DTEST_LOGIC=OFF -DTEST_SCCB=OFF -DTEST_OV7670=OFF -DTEST_LCD=OFF -DTEST_LCD_DMA=OFF
cmake --build --preset Debug
```

Expected: 编译链接成功，无 warning。

- [ ] **Step 7: Commit**

```bash
git add Core/Src/bsp/pipeline.c Core/Inc/bsp/pipeline.h
git commit -m "feat: add frame counter to pipeline for observability"
```

---

### Task 2: CMake 新增 TEST_PIPELINE 选项

**Files:**
- Modify: `CMakeLists.txt`（选项定义 + `_unit_tests` 条件 + source/compile definition 块）

**Interfaces:**
- Produces: CMake option `TEST_PIPELINE`，启用时定义宏 `TEST_PIPELINE` 并加入 `Core/Src/test/test_pipeline.c`（Task 3 创建）

- [ ] **Step 1: 加 option 定义**

`CMakeLists.txt` 第 81 行（`TEST_LCD_DMA` 选项）之后追加：

```cmake
option(TEST_PIPELINE "Enable OV7670 colorbar->LCD pipeline test" OFF)
```

- [ ] **Step 2: 加入 `_unit_tests` 条件**

第 83 行 `if(TEST_TEST_LED OR ... OR TEST_LCD_DMA)` 追加 ` OR TEST_PIPELINE`。

- [ ] **Step 3: 加入 source + compile definition 块**

第 126-129 行 `TEST_LCD_DMA` 块之后追加：

```cmake
    if(TEST_PIPELINE)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_pipeline.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_PIPELINE)
    endif()
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add TEST_PIPELINE option"
```

---

### Task 3: test_pipeline 测试组 + test_runner 调度

**Files:**
- Create: `Core/Inc/test/test_pipeline.h`
- Create: `Core/Src/test/test_pipeline.c`
- Modify: `Core/Src/test/test_runner.c`（include + 分支）

**Interfaces:**
- Consumes: `Pipeline_GetFrameCount()`（Task 1）、`Pipeline_Init/Poll/GetState/EnableTimDma/ClearVsyncPending/EnableVsyncIrq`、`OV7670_Init/EnableColorBar`、`LCD_Init`、`DWT_Init`
- Produces: `void RunPipelineTests(void)` —— 被 `test_runner.c` 调用，无限循环不返回

- [ ] **Step 1: 创建 `Core/Inc/test/test_pipeline.h`**

```c
/**
  * @file    test_pipeline.h
  * @brief   OV7670 colorbar -> LCD live pipeline test group
  */
#ifndef TEST_PIPELINE_H
#define TEST_PIPELINE_H

void RunPipelineTests(void);

#endif /* TEST_PIPELINE_H */
```

- [ ] **Step 2: 创建 `Core/Src/test/test_pipeline.c`**

```c
/**
  * @file    test_pipeline.c
  * @brief   TEST_PIPELINE group - OV7670 colorbar -> LCD live pipeline
  *
  *          Enables OV7670 colorbar test pattern as a deterministic video
  *          source and runs the full VSYNC->FIFO->Camera DMA->SPI DMA->LCD
  *          pipeline. Prints FPS once per second, deferred to the pipeline
  *          IDLE window so the blocking UART never delays frame read start.
  *
  *          No Unity dependency: pure visual + serial observation test.
  */
#include "test_pipeline.h"
#include "pipeline.h"
#include "ov7670.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#define TEST_PIPELINE_FPS_WINDOW_MS  1000u

void RunPipelineTests(void)
{
  DWT_Init();

  if (!OV7670_Init())
  {
    debug_printf("[TEST_PIPELINE] OV7670_Init FAILED\n");
    Error_Handler();
  }
  debug_printf("[TEST_PIPELINE] OV7670 colorbar -> LCD live pipeline test\n");

  OV7670_EnableColorBar();
  debug_printf("[TEST_PIPELINE] colorbar enabled (COM7 bit1, 8-bar)\n");

  LCD_Init();
  debug_printf("[TEST_PIPELINE] LCD init OK\n");

  Pipeline_Init();
  Pipeline_EnableTimDma();
  Pipeline_ClearVsyncPending();
  Pipeline_EnableVsyncIrq();
  debug_printf("[TEST_PIPELINE] VSYNC IRQ enabled, streaming...\n");

  uint32_t last_frames = Pipeline_GetFrameCount();
  uint32_t last_tick   = HAL_GetTick();
  bool     print_pending = false;
  uint8_t  last_fps      = 0u;

  for (;;)
  {
    Pipeline_Poll();

    /* 1-second FPS window (SysTick based) */
    if (HAL_GetTick() - last_tick >= TEST_PIPELINE_FPS_WINDOW_MS)
    {
      uint32_t frames_now = Pipeline_GetFrameCount();
      last_fps     = (uint8_t)(frames_now - last_frames);
      last_frames  = frames_now;
      print_pending = true;
      last_tick    = HAL_GetTick();
    }

    /* Deferred print: only when pipeline is IDLE (no timing impact) */
    if (print_pending && Pipeline_GetState() == PIPELINE_STATE_IDLE)
    {
      debug_printf("[TEST_PIPELINE] fps=%u state=IDLE frames=%lu\n",
                   last_fps, (unsigned long)Pipeline_GetFrameCount());
      print_pending = false;
    }
  }
}
```

- [ ] **Step 3: `test_runner.c` 加 include**

`Core/Src/test/test_runner.c` 第 26-28 行（`TEST_LCD_DMA` include 之后）追加：

```c
#ifdef TEST_PIPELINE
#include "test_pipeline.h"
#endif
```

- [ ] **Step 4: `test_runner.c` 加调度分支**

第 66-68 行（`TEST_LCD_DMA` 分支之后）追加：

```c
#ifdef TEST_PIPELINE
  RunPipelineTests();   /* never returns: colorbar -> LCD streaming */
#endif
```

- [ ] **Step 5: 构建验证（TEST_PIPELINE=ON）**

```bash
cmake --preset Debug -DTEST_TEST_LED=OFF -DTEST_LOGIC=OFF -DTEST_SCCB=OFF -DTEST_OV7670=OFF -DTEST_LCD=OFF -DTEST_LCD_DMA=OFF -DTEST_PIPELINE=ON
cmake --build --preset Debug
```

Expected: 编译链接成功，无 warning。

- [ ] **Step 6: Commit**

```bash
git add Core/Inc/test/test_pipeline.h Core/Src/test/test_pipeline.c Core/Src/test/test_runner.c
git commit -m "feat: add TEST_PIPELINE colorbar-to-LCD streaming test group"
```

---

### Task 4: 烧录 + 硬件验证

**Files:** 无（仅验证）

- [ ] **Step 1: 生成 .bin**

```bash
arm-none-eabi-objcopy -O binary build/Debug/stm32-ov7670-live.elf build/Debug/stm32-ov7670-live.bin
```

- [ ] **Step 2: 烧录**

```bash
st-flash --reset write build/Debug/stm32-ov7670-live.bin 0x08000000
```

- [ ] **Step 3: 串口观察**

```bash
python3 /tmp/opencode/serial_capture.py
```

Expected:
```
[TEST_PIPELINE] OV7670 colorbar -> LCD live pipeline test
[TEST_PIPELINE] colorbar enabled (COM7 bit1, 8-bar)
[TEST_PIPELINE] LCD init OK
[TEST_PIPELINE] VSYNC IRQ enabled, streaming...
[TEST_PIPELINE] fps=30 state=IDLE frames=...
[TEST_PIPELINE] fps=30 state=IDLE frames=...
```

- 若 FPS 持续为 0：链路未打通，记录串口输出排查
- 若打印错过 IDLE 一直不输出：检查 `Pipeline_Poll()` 是否在循环内被调用、VSYNC 是否触发

- [ ] **Step 4: 目视验证**

Expected: LCD 显示稳定完整彩条（多色竖条），无撕裂、无错位、无偏色。

- [ ] **Step 5: 记录结果**

将串口输出与目视结论整理，作为合并依据。

---

## Self-Review 记录

- **Spec 覆盖**：帧计数器（Task 1）、CMake 选项（Task 2）、测试组实现含 IDLE 延迟打印与 SysTick 计时（Task 3）、验证（Task 4）——全部覆盖，无遗漏。
- **占位符扫描**：无 TBD/TODO；所有代码块为完整可编译内容。
- **类型一致性**：`Pipeline_GetFrameCount()` 返回 `uint32_t`，FPS 差值强转 `uint8_t`，`%lu` 对应 `(unsigned long)` 实参，一致。
