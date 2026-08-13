# AGENTS.md - STM32 OV7670 Live

## Project Overview

- MCU: STM32F103C8T6 (72MHz, LQFP48, 20KB RAM, 64KB FLASH)
- Camera: OV7670 + AL422B FIFO module
- Display: ST7735 1.8" TFT LCD (160x128 RGB565)
- Goal: Real-time camera live view, 160x128 RGB565 @ 30fps
- Architecture: Dual-DMA ping-pong pipeline (Camera DMA Circular + SPI DMA Normal), zero-copy within frame
- Toolchain: C11, STM32 HAL, CMake + arm-none-eabi-gcc, CubeMX generated

## Hardware Facts (OV7670 FIFO module)

- FIFO write enable is HREF-gated on the module: `/WE = NAND(HREF, FIFO_WR)` (SN74LVC1G00).
  PCLK only reaches the AL422B WCLK while HREF is high, so **only active pixels are stored**.
- Consequence: each stored row is exactly `width*2` bytes (320B for 160x120 RGB565) with no
  blanking padding. FIFO row boundaries align to 320B; software must NOT filter HREF blanks.
- Frame = 120 rows x 320B = 38400B < AL422B capacity 393216B, no overflow.

## Build & Verify

- Build: `cmake --preset Debug && cmake --build --preset Debug`
- Toolchain: cmake/gcc-arm-none-eabi.cmake (--specs=nano.specs)
- Constraints: RAM 20KB, FLASH 64KB
- Debug build auto-defines `DEBUG` macro (cmake/stm32cubemx/CMakeLists.txt)
- Unit-test options (TEST_TEST_LED/TEST_LOGIC/TEST_SCCB/TEST_OV7670/TEST_LCD/TEST_LCD_DMA) are cached by CMake: once set via `-DTEST_*=ON`, later `cmake --preset Debug` without `-D` reuses the cached value. ALWAYS pass `-D` explicitly for every switch (e.g. `-DTEST_LCD=OFF` to return to the normal app). Check current state with `grep TEST_ build/Debug/CMakeCache.txt`. Do NOT assume "no `-D` means OFF".

## File Structure

- `Core/Inc/`, `Core/Src/`         - CubeMX generated code (only edit USER CODE regions)
- `Core/Inc/bsp/`, `Core/Src/bsp/` - User BSP driver modules
- `docs/`                          - Design docs and reference materials
- `cmake/stm32cubemx/`             - CubeMX generated, do NOT edit manually

### BSP Module Naming

| File | Module | Responsibility |
|------|--------|----------------|
| dwt_delay.c/h | DWT | DWT CYCCNT precise delay |
| ov7670_sccb.c/h | SCCB | GPIO-bitbang SCCB/I2C read/write |
| ov7670.c/h | OV7670 | Register configuration |
| st7735.c/h | LCD | ST7735 initialization and commands |
| pipeline.c/h | Pipeline | Dual-DMA frame capture pipeline (Camera -> FIFO -> LCD) |

### Adding BSP Sources

Add in root `CMakeLists.txt` (NOT in `cmake/stm32cubemx/`):

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/bsp/dwt_delay.c
    Core/Src/bsp/ov7670_sccb.c
    # ...
)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc/bsp
)
```

## Coding Standard (referencing STM32 SDK)

### Formatting

- Indent: 2 spaces, no tabs
- Braces: Allman style (opening brace on new line)
- Line ending: no trailing whitespace
- switch/case: case labels align with switch

### Naming Conventions

| Category | Rule | Example |
|----------|------|---------|
| BSP public functions (.h) | Module_Action() PascalCase | SCCB_WriteReg(), LCD_Init() |
| BSP static inline (.h) | Module_Action() PascalCase | Pipeline_EnableVsyncIrq() |
| BSP static functions (.c) | Action() PascalCase | WriteByte(), OnVsync() |
| Test functions (test_*.c) | Action() PascalCase | TestSccbReadPid(), TestOv7670Init() |
| Test group entry (test_*.c) | Run<Group>Tests() PascalCase | RunSccbTests(), RunOv7670Tests() |
| HAL/CubeMX functions | keep as-is | HAL_GPIO_WritePin(), MX_GPIO_Init() |
| Local variables | snake_case | reg_addr, byte_sent |
| Global handles | h + PascalCase | hspi2, htim3, huart1 |
| DMA handles | hdma_ + snake_case | hdma_spi2_tx, hdma_tim3_ch4_up |
| Macros / constants | UPPER_SNAKE_CASE | OV7670_D0_Pin, CAMERA_FRAME_SIZE |
| Typedefs | PascalCase + TypeDef | Camera_StateTypeDef |
| Enum values | UPPER_SNAKE_CASE | CAMERA_STATE_IDLE |

> **通用原则**：作用范围越广，命名描述需要越多限定。

### Comments

- Functions: Doxygen style `/** @brief @param @retval */`
- File header: Doxygen block comment (`@file @brief`)
- Inline: `/* comment */`
- No Chinese characters in code comments

### C Language Practices

- Numeric constants with `u` suffix: `0x14u`, `40960u`
- Interrupt-shared variables must be `volatile`
- Pointer `*` adjacent to variable name: `uint8_t *ptr`
- Parameter validation: `assert_param()`
- Empty loop bodies (busy-wait / spin) use `while (cond);`, not `while (cond) { }`

### LL Peripheral Access

- Do not hardcode CMSIS peripheral instances (`DMA1_ChannelN`, `TIMx`, `SPIx`,
  `EXTI`) or their channel-bound flags directly in business logic. Define
  readable macro aliases in `Core/Inc/bsp/periph_map.h` — the single header
  that aggregates all such mappings:

  ```c
  #define PIPELINE_CAM_DMA       DMA1            /* controller */
  #define PIPELINE_CAM_DMA_CHNL  DMA1_Channel3   /* channel */
  #define PIPELINE_CAM_TIM       TIM3
  ```

- CubeMX regeneration may re-map DMA channels / peripherals; updating the
  `periph_map.h` block is enough. Keep the aliases together with a "must match
  msp.c" note.

## CubeMX Rules

- Only write code within `/* USER CODE BEGIN */ ... /* USER CODE END */` regions
- After regeneration, verify USER CODE regions are preserved
- Do NOT modify files under `cmake/stm32cubemx/`

## Debug Output

### printf Redirection

`syscalls.c` provides `_write()` -> `__io_putchar()` (weak).
Implement a non-weak `__io_putchar()` that calls `HAL_UART_Transmit()` to USART1.

### debug_printf Macro

```c
#ifdef DEBUG
  #define debug_printf(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
  #define debug_printf(fmt, ...) ((void)0)
#endif
```

- `DEBUG` macro is auto-defined by CMake Debug build
- Do NOT use `debug_printf` in interrupt context (blocking UART)
- **Do NOT use `debug_printf` in timing-sensitive regions** (between signal
  assertions, inside VSYNC/HREF polling loops, between WR_High and WR_Low,
  etc.). UART is blocking: at 115200 baud, one character takes ~87us, a
  40-char line takes ~3.5ms -- enough to miss a VSYNC edge or skew a write
  window. Record data to variables inside the sensitive region, print after
  exiting it (e.g., after `WR_Low`).
- newlib-nano does not support `%f` float formatting by default

## Temporary Analysis Output

- All host-side analysis products (rendered images, decoded frame dumps,
  segment tables, logs copied from /tmp, etc.) go under the project-local
  `.temp/` directory (e.g. `.temp/tp10_160x128_be_segments.txt`), so they
  are reachable via SSH without touching the repo. Do NOT scatter analysis
  files in `/tmp` only.
- `.temp/` is gitignored scratch space; keep the repo itself clean.


## Language Preferences

- Communication: Chinese
- Code / comments / variable names: English only
- Design documents (docs/): Chinese
