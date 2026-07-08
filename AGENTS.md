# AGENTS.md - STM32 OV7670 Live

## Project Overview

- MCU: STM32F103C8T6 (72MHz, LQFP48, 20KB RAM, 64KB FLASH)
- Camera: OV7670 + AL422B FIFO module
- Display: ST7735 1.8" TFT LCD (160x128 RGB565)
- Goal: Real-time camera live view, 160x128 RGB565 @ 30fps
- Architecture: Dual-DMA ping-pong pipeline (Camera DMA Circular + SPI DMA Normal), zero-copy within frame
- Toolchain: C11, STM32 HAL, CMake + arm-none-eabi-gcc, CubeMX generated

## Build & Verify

- Build: `cmake --preset Debug && cmake --build --preset Debug`
- Toolchain: cmake/gcc-arm-none-eabi.cmake (--specs=nano.specs)
- Constraints: RAM 20KB, FLASH 64KB
- Debug build auto-defines `DEBUG` macro (cmake/stm32cubemx/CMakeLists.txt)

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
| camera.c/h | Camera | Frame capture state machine and DMA callbacks |

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
| BSP functions | Module_Action() PascalCase | SCCB_WriteReg(), LCD_Init() |
| HAL/CubeMX functions | keep as-is | HAL_GPIO_WritePin(), MX_GPIO_Init() |
| Local variables | snake_case | reg_addr, byte_sent |
| Global handles | h + PascalCase | hspi2, htim3, huart1 |
| DMA handles | hdma_ + snake_case | hdma_spi2_tx, hdma_tim3_ch4_up |
| Macros / constants | UPPER_SNAKE_CASE | OV7670_D0_Pin, CAMERA_FRAME_SIZE |
| Typedefs | PascalCase + TypeDef | Camera_StateTypeDef |
| Enum values | UPPER_SNAKE_CASE | CAMERA_STATE_IDLE |

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
- newlib-nano does not support `%f` float formatting by default

## Language Preferences

- Communication: Chinese
- Code / comments / variable names: English only
- Design documents (docs/): Chinese
