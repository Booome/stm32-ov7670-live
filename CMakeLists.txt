cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32-ov7670-live)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/bsp/dwt_delay.c
    Core/Src/bsp/ov7670_sccb.c
    Core/Src/bsp/ov7670.c
    Core/Src/bsp/st7735.c
    Core/Src/bsp/pipeline.c
    Core/Src/bsp/debug.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Inc/bsp
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# ---- Unit test options (any TEST_* enabled => run tests instead of main app) ----
option(TEST_TEST_LED "Enable TEST_LED smoke test (1Hz blink)" OFF)
option(TEST_LOGIC    "Enable pure-logic unit tests"            OFF)
option(TEST_SCCB     "Enable SCCB hardware-in-loop tests"      OFF)
option(TEST_OV7670   "Enable OV7670 register tests"            OFF)
option(TEST_LCD      "Enable LCD hardware tests"               OFF)
option(TEST_LCD_DMA  "Enable LCD DMA hardware tests"           OFF)

if(TEST_TEST_LED OR TEST_LOGIC OR TEST_SCCB OR TEST_OV7670 OR TEST_LCD OR TEST_LCD_DMA)
    set(_unit_tests ON)
endif()

if(_unit_tests)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ThirdParty/Unity/unity.c
        Core/Src/test/test_runner.c
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ThirdParty/Unity
        Core/Inc/test
    )
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE UNIT_TESTS_ENABLED)

    if(TEST_TEST_LED)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_test_led.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_TEST_LED)
    endif()
    if(TEST_LOGIC)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_logic.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LOGIC)
    endif()
    if(TEST_SCCB)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_sccb.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_SCCB)
    endif()
    if(TEST_OV7670)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_ov7670.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_OV7670)
    endif()
    if(TEST_LCD)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LCD)
    endif()
    if(TEST_LCD OR TEST_LCD_DMA)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd_common.c)
    endif()
    if(TEST_LCD_DMA)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/test/test_lcd_dma.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TEST_LCD_DMA)
    endif()
endif()
