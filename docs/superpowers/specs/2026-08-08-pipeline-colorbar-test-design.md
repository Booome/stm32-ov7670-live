# 设计：OV7670 彩条模式输出到 LCD 的硬件在环测试

- 日期：2026-08-08
- 分支：`feat/pipeline-colorbar-test`
- 状态：已批准

## 1. 背景与目标

现有 `TEST_OV7670` 测试组仅做寄存器级验证（断言 `COM3` bit0 置 1/清零），
并未验证 OV7670 彩条测试图案真正经过完整 pipeline 显示到 LCD。

本测试利用 OV7670 内置 colorbar（彩条测试图案，`COM3` bit0 = 1）作为**确定性视频源**，
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
OV7670_EnableColorBar()       // COM3 |= 0x01，开启彩条
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

### 3.3 串口输出格式

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
| `Core/Src/test/test_runner.c` | +`#ifdef TEST_PIPELINE` 分支 |
| `CMakeLists.txt` | +`option(TEST_PIPELINE ...)` + 源文件/编译定义 |

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
- 不引入 Unity/断言（纯观测测试）。
- 不做彩条/实况自动切换（保持彩条持续显示）。
- 不逐像素回读校验（DMA 直通链路无回读路径）。
- 不在 `FRAME_START`/`FRAME_DONE` 等非 IDLE 状态下执行阻塞打印
  （FPS 打印延迟到 IDLE 窗口，避免破坏 1.3ms 读时序）。
