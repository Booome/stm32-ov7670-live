# 设计：OV7670 彩条模式输出到 LCD 的硬件在环测试

- 日期：2026-08-08
- 分支：`feat/pipeline-colorbar-test`
- 状态：已批准

## 1. 背景与目标

现有 `TEST_OV7670` 测试组仅做寄存器级验证（断言 `COM7` bit1 置 1/清零），
并未验证 OV7670 彩条测试图案真正经过完整 pipeline 显示到 LCD。

本测试利用 OV7670 内置 colorbar（彩条测试图案，`COM7` bit1 = 1 + test_pattern = "10" 8-bar）作为**确定性视频源**，
走完整实时链路：

```
OV7670 colorbar -> AL422B FIFO -> Camera DMA (Circular) -> SPI DMA (Normal) -> ST7735 LCD
```

无需镜头或真实景物即可目视验证全链路是否打通，并以串口帧率统计作为链路稳定性的量化依据。

目标：
- 在 LCD 上持续显示稳定的彩条图案（目视检查）
- 串口每 1 秒打印一次实测 FPS，确认帧率稳定（预期约 30fps）
- 自动证明链路至少完整跑通了一帧（帧计数递增）

## 2. 组织形态

新增独立测试组 `TEST_PIPELINE`，由现有 `TestRunner_Run()` 调度，无限循环。

- 开关宏：`TEST_PIPELINE`（CMake option）
- 入口函数：`RunPipelineTests()`（`test_pipeline.c` / `test_pipeline.h`）
- 调度位置：`test_runner.c` 中 `TEST_LCD_DMA` 分支之后

> 不引入 Unity：本测试组为纯视觉 + 串口观测测试，不使用 UNITY_BEGIN/END，
> 也不调用 RUN_TEST。`test_pipeline.c` 不 include `unity.h`。

## 3. 设计

### 3.1 pipeline 最小可观测性扩展

`pipeline.c` 增加帧计数器（唯一对生产代码的改动，纯可观测性增强，不改变任何行为）：

- `static volatile uint32_t s_frame_count;`（初值 0）
- `FrameDone()` 末尾执行 `s_frame_count++;`
- `pipeline.h` 新增只读接口：`uint32_t Pipeline_GetFrameCount(void);`

### 3.2 测试组 `test_pipeline.c`

初始化顺序（测试模式下 main.c 不调用 `Pipeline_EnableTimDma()`，
且正常应用分支被 `#else` 跳过，故测试组必须自包含完整初始化）：

```
DWT_Init()
OV7670_Init()                 // 失败则打印 FAILED 并 Error_Handler()
OV7670_EnableColorBar()       // COM7 bit1 + COM17 bit3 + test_pattern 8-bar
LCD_Init()
Pipeline_Init()
Pipeline_EnableTimDma()       // 使能 TIM3 CC4 DMA 请求
Pipeline_ClearVsyncPending()
Pipeline_EnableVsyncIrq()
```

初始化后进入无限循环：

```
uint32_t last_tick = HAL_GetTick();
bool print_pending = false;
uint8_t last_fps = 0;

for (;;) {
  Pipeline_Poll();

  /* 1-second FPS window (SysTick based) */
  if (HAL_GetTick() - last_tick >= 1000u) {
    uint32_t frames_now = Pipeline_GetFrameCount();
    last_fps = (uint8_t)(frames_now - last_frames);
    last_frames = frames_now;
    print_pending = true;
    last_tick = HAL_GetTick();
  }

  /* Deferred print: only when pipeline is IDLE (no timing impact) */
  if (print_pending && Pipeline_GetState() == PIPELINE_STATE_IDLE) {
    debug_printf("[TEST_PIPELINE] fps=%u state=IDLE frames=%lu\n",
                 last_fps, (unsigned long)Pipeline_GetFrameCount());
    print_pending = false;
  }
}
```

- 帧计数差值即本 1 秒窗口内完成的帧数，即 FPS。
- **打印时序规避**：`debug_printf` 是阻塞式 UART（115200 baud，每行约 3.5ms），
  若在 `FRAME_START` 状态打印会延迟 `ReadStart()` 导致该帧错位。
  因此 FPS 只统计、不立即打印；置 `print_pending` 标志，待 `Pipeline_GetState()`
  回到 `IDLE`（`Poll()` 刚处理完一帧的间隙）才实际输出，保证对 DMA 时序零阻塞。
- 计时用 SysTick `HAL_GetTick()`（1ms 分辨率），不用 DWT 非阻塞延时
  （`DWT_DelayStart` 的 `UsToTicks` 对 1,000,000us 会饱和在 ~59ms）。
- 彩条目视 + FPS 稳定（≈30）即 PASS；FPS 持续为 0 说明链路未打通。

### 3.3 FIFO 手动读取数据验证（`TEST_OV7670` 扩展，2026-08-08 追加）

目视彩条异常时，无法判断是"彩条配置错误"还是"pipeline/LCD 时序问题"。
本测试绕过 pipeline/DMA，按 AL422B FIFO 协议**手动逐行读取**数据并校验，
直接验证"FIFO 内存中的像素数据是否为正确的 8-bar 彩条"，从而把链路问题一分为二。

#### 3.3.1 前置维度一致性检查

读取校验前必须先断言分辨率/维度与 LCD 完全一致，避免"读了错误尺寸"的误判：

```
_Static_assert(PIPELINE_WIDTH == LCD_TEST_WIDTH, ...)    // 160 == 160
_Static_assert(PIPELINE_HEIGHT == LCD_TEST_HEIGHT, ...)  // 128 == 128
_Static_assert(PIPELINE_HALF_SIZE == LCD_TEST_LINE_SIZE, ...)  // 320 == 320 (RGB565 每行字节)
```

- 每行 = 160px × 2B = 320B（`LCD_TEST_LINE_SIZE`），与 pipeline 半缓冲大小一致。
- 8-bar 结构：160px / 8 = 每 bar 20px = 40B。

#### 3.3.2 手动读取流程（不依赖 pipeline/DMA）

```
1. OV7670_EnableColorBar()          // 已有（COM7 bit1 + COM17 bit3 + 8-bar）
2. OV7670_FIFO_WR_High()            // 使能 FIFO 写入（摄像头持续写入彩条）
3. DWT_DelayMs(100)                 // 跳过前 3 帧（~100ms），避开彩条切换不稳定帧
4. 临时把 RCK(PB1) 从 TIM3_CH4 AF 切为 GPIO 输出（手动翻转）
5. RRST low -> high                 // 复位读指针
6. OE low                           // 使能 FIFO 输出
7. 逐行读取并校验：
     for row in 0..N:
       for i in 0..319:             // 320B = 一行
         RCK high -> 采样 GPIOA->IDR & 0xFF -> RCK low
       ValidateLine(line)           // 8-bar 结构断言
8. OE high，恢复 RCK 为 AF 模式
```

#### 3.3.3 数据校验断言

每行 320B = 160 个 RGB565 像素，必须满足 8-bar 结构：

- **bar 内恒定**：每个 bar（20px = 40B）内像素 RGB565 值恒定
  （允许边缘 1px 过渡，取 bar 中间 18px 校验）。
- **8 bar 互异**：8 个 bar 的代表色两两不同。
- 打印 8 个 bar 的代表 RGB565 值（便于人工核对标准 8-bar：
  白 0xFFFF / 黄 0xFFE0 / 青 0x07FF / 绿 0x07E0 / 品红 0xF81F / 红 0xF800 / 蓝 0x001F / 黑 0x0000）。

> 校验不断言具体颜色值（彩条经色彩矩阵/饱和度调整后可能有偏差），
> 只断言"结构"：bar 内恒定 + bar 间互异。

#### 3.3.4 可行性依据

- AL422B 时序：读写周期 20ns、访问时间 15ns，远低于 GPIO 位带翻转周期，手动读完全可行。
- 手动读取不覆盖 FIFO 数据（只读不写），读到即有效数据。
- 逐行读校验：每行读完后校验该行，数据无破坏风险。

### 3.4 串口输出格式

```
[TEST_PIPELINE] OV7670 colorbar -> LCD live pipeline test
[TEST_PIPELINE] fps=29 state=IDLE frames=2350
[TEST_PIPELINE] fps=30 state=IDLE frames=2380
...
```

- 打印固定发生在 `state=IDLE` 时刻（非阻塞打印策略保证）。
- 仅在 `DEBUG` 宏定义时输出（`debug_printf`），Release 构建无此开销。

## 4. 文件变更清单

| 文件 | 变更 |
|------|------|
| `Core/Src/bsp/pipeline.c` | +`s_frame_count` 自增（FrameDone 末尾） |
| `Core/Inc/bsp/pipeline.h` | +`Pipeline_GetFrameCount()` 声明 |
| `Core/Src/test/test_pipeline.c` | 新增，测试组实现（不依赖 Unity） |
| `Core/Inc/test/test_pipeline.h` | 新增，声明 `RunPipelineTests()` |
| `Core/Src/test/test_ov7670_colorbar.c` | 新增，独立测试组 `TEST_OV7670_COLORBAR`（Unity） |
| `Core/Inc/test/test_ov7670_colorbar.h` | 新增，声明 `RunOv7670ColorbarTests()` |
| `Core/Src/test/test_runner.c` | +`#ifdef TEST_PIPELINE` 分支 |
| `CMakeLists.txt` | +`option(TEST_PIPELINE ...)` + 源文件/编译定义 |

### FIFO 数据验证测试接入

`test_ov7670_colorbar.c` 是**独立测试组** `TEST_OV7670_COLORBAR`（CMake option），
自包含 `DWT_Init/SCCB_Init/OV7670_Init` + 彩条 enable 全流程，
不依赖 `TEST_OV7670` 组的寄存器校验状态（避免其 enable/disable 彩条序列干扰数据读取）。

- 入口函数：`RunOv7670ColorbarTests()`（`test_ov7670_colorbar.c/.h`）
- 调度位置：`test_runner.c` 中 `TEST_OV7670` 分支之后
- 编译：仅 `-DTEST_OV7670_COLORBAR=ON` 时编译，其余开关显式 OFF

## 5. 错误处理

- `OV7670_Init()` 返回 false → 串口打印 `[TEST_PIPELINE] OV7670_Init FAILED`，进入 `Error_Handler()`。
- 帧计数不递增 → 串口 FPS=0，目视 LCD 无彩条，人工判定失败。

## 6. 验证方式

1. 构建：`cmake --preset Debug -DTEST_PIPELINE=ON`，`cmake --build --preset Debug`
2. `arm-none-eabi-objcopy -O binary build/Debug/stm32-ov7670-live.elf build/Debug/stm32-ov7670-live.bin`
3. 烧录：`st-flash --reset write build/Debug/stm32-ov7670-live.bin 0x08000000`
4. 串口（`/dev/ttyACM2` @115200）观察 FPS 输出稳定 ≈30
5. 目视 LCD 显示完整彩条图案，无撕裂/无偏色

## 7. 内存与资源

- `s_pipeline_buffer` 640B（已有）+ `s_frame_count` 4B，增量极小。
- 20KB RAM / 64KB FLASH 预算内。
- 不新增外设占用。

## 8. 不做的事（明确排除）

- 不修改 pipeline 的状态机/时序逻辑（只加帧计数器 `s_frame_count`）。
- 不引入 Unity/断言（纯观测测试；FIFO 数据验证测试例外，它用 Unity 断言）。
- 不做彩条/实况自动切换（保持彩条持续显示）。
- 不在 `FRAME_START`/`FRAME_DONE` 等非 IDLE 状态下执行阻塞打印
  （FPS 打印延迟到 IDLE 窗口，避免破坏 1.3ms 读时序）。
- **不修改 pipeline 的读路径**：FIFO 数据验证是独立的手动 GPIO 读取，
  与 Camera DMA/SPI DMA 无共享资源（RCK 由 TIM3 AF 驱动，手动读时临时切 GPIO，
  读毕恢复，互不干扰）。
