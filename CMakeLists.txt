cmake_minimum_required(VERSION 3.20)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(driver-input-unit C ASM)

# ---- ST libraries, downloaded on the first configure -------------------------
# These repositories have no CMake files of their own, so SOURCE_SUBDIR points
# at a folder that does not exist to make CMake just download them.
include(FetchContent)
FetchContent_Declare(cmsis_core
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis-core.git
    GIT_TAG        v5.9.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)
FetchContent_Declare(cmsis_device_c0
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis-device-c0.git
    GIT_TAG        v1.4.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)
FetchContent_Declare(stm32c0xx_hal
    GIT_REPOSITORY https://github.com/STMicroelectronics/stm32c0xx-hal-driver.git
    GIT_TAG        v1.4.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)
FetchContent_MakeAvailable(cmsis_core cmsis_device_c0 stm32c0xx_hal)

set(DEVICE_DIR ${cmsis_device_c0_SOURCE_DIR})
set(HAL_DIR    ${stm32c0xx_hal_SOURCE_DIR})
set(LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/stm32c092xc.ld)

add_executable(diu.elf
    # Application
    src/main.c
    src/board.c
    src/ads7038.c
    src/can.c
    src/sensors.c
    src/safety.c
    src/calibration.c
    src/cal_storage.c
    src/glv.c
    src/i2c_bus.c
    src/eeprom.c
    src/mcp9800.c
    src/lsm6dso32.c
    src/serial.c
    src/debug.c

    # ST start-up code (vector table, reset handler, SystemInit)
    ${DEVICE_DIR}/Source/Templates/gcc/startup_stm32c092xx.s
    ${DEVICE_DIR}/Source/Templates/system_stm32c0xx.c

    # ST HAL drivers (only the ones enabled in include/stm32c0xx_hal_conf.h)
    ${HAL_DIR}/Src/stm32c0xx_hal.c
    ${HAL_DIR}/Src/stm32c0xx_hal_adc.c
    ${HAL_DIR}/Src/stm32c0xx_hal_adc_ex.c
    ${HAL_DIR}/Src/stm32c0xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32c0xx_hal_dma.c
    ${HAL_DIR}/Src/stm32c0xx_hal_fdcan.c
    ${HAL_DIR}/Src/stm32c0xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32c0xx_hal_i2c.c
    ${HAL_DIR}/Src/stm32c0xx_hal_i2c_ex.c
    ${HAL_DIR}/Src/stm32c0xx_hal_iwdg.c
    ${HAL_DIR}/Src/stm32c0xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32c0xx_hal_spi.c
    ${HAL_DIR}/Src/stm32c0xx_hal_spi_ex.c
    ${HAL_DIR}/Src/stm32c0xx_hal_uart.c
    ${HAL_DIR}/Src/stm32c0xx_hal_uart_ex.c)

target_include_directories(diu.elf PRIVATE include)
target_include_directories(diu.elf SYSTEM PRIVATE
    ${cmsis_core_SOURCE_DIR}/Include
    ${DEVICE_DIR}/Include
    ${HAL_DIR}/Inc)

target_compile_definitions(diu.elf PRIVATE
    STM32C092xx
    USE_HAL_DRIVER
    HSE_VALUE=25000000U)    # crystal Y1

set(CPU_FLAGS -mcpu=cortex-m0plus -mthumb)

target_compile_options(diu.elf PRIVATE
    ${CPU_FLAGS}
    -Os -g3
    -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>)

# Full warnings for our own code only, not for ST's
set_source_files_properties(
    src/main.c src/board.c src/ads7038.c src/can.c src/sensors.c src/safety.c
    src/calibration.c src/cal_storage.c src/glv.c src/i2c_bus.c src/eeprom.c
    src/mcp9800.c src/lsm6dso32.c src/serial.c src/debug.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

target_link_options(diu.elf PRIVATE
    ${CPU_FLAGS}
    -T${LINKER_SCRIPT}
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/diu.map
    -Wl,--print-memory-usage)

set_target_properties(diu.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET diu.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:diu.elf> ${CMAKE_CURRENT_BINARY_DIR}/diu.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:diu.elf> ${CMAKE_CURRENT_BINARY_DIR}/diu.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:diu.elf>)
