/*
 * STM32 HAL configuration: selects which HAL drivers are built and gives the
 * HAL the oscillator frequencies. The HAL includes this file by name.
 */
#ifndef STM32C0xx_HAL_CONF_H
#define STM32C0xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- HAL drivers used by this project ----------------------------------- */
#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED      /* GLV supply voltage monitor        */
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED      /* serial debug output               */
#define HAL_FDCAN_MODULE_ENABLED    /* CAN                               */
#define HAL_FLASH_MODULE_ENABLED    /* (headers only, needed by the RCC driver) */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED      /* temperature sensor, IMU, EEPROM   */
#define HAL_IWDG_MODULE_ENABLED     /* watchdog                          */
#define HAL_RCC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED      /* ADS7038 sensor ADC                */
#define HAL_UART_MODULE_ENABLED     /* serial debug output               */

#define USE_HAL_ADC_REGISTER_CALLBACKS      0U
#define USE_HAL_FDCAN_REGISTER_CALLBACKS    0U
#define USE_HAL_I2C_REGISTER_CALLBACKS      0U
#define USE_HAL_IWDG_REGISTER_CALLBACKS     0U
#define USE_HAL_SPI_REGISTER_CALLBACKS      0U
#define USE_HAL_UART_REGISTER_CALLBACKS     0U
#define USE_SPI_CRC                         0U

/* ---- Oscillators --------------------------------------------------------- */
#if !defined(HSE_VALUE)
#define HSE_VALUE               (25000000U) /* crystal Y1, Hz (also set in CMakeLists.txt) */
#endif
#define HSE_STARTUP_TIMEOUT     (100UL)     /* ms */
#if !defined(HSI_VALUE)
#define HSI_VALUE               (48000000U) /* internal oscillator, Hz */
#endif
#define HSI48_VALUE             48000000U
#define LSI_VALUE               (32000UL)
#define LSI_STARTUP_TIME        130UL
#define LSE_VALUE               (32768UL)
#define LSE_STARTUP_TIMEOUT     (5000UL)
#define EXTERNAL_I2S1_CLOCK_VALUE (48000UL)

/* ---- System -------------------------------------------------------------- */
#define VDD_VALUE                   (3300UL)                        /* mV */
#define TICK_INT_PRIORITY           ((1UL << __NVIC_PRIO_BITS) - 1UL) /* lowest */
#define USE_RTOS                    0U
#define PREFETCH_ENABLE             1U
#define INSTRUCTION_CACHE_ENABLE    1U

/* ---- Driver headers ------------------------------------------------------ */
#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32c0xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32c0xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32c0xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32c0xx_hal_cortex.h"
#endif
#ifdef HAL_ADC_MODULE_ENABLED
#include "stm32c0xx_hal_adc.h"
#include "stm32c0xx_hal_adc_ex.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
#include "stm32c0xx_hal_i2c.h"
#endif
#ifdef HAL_IWDG_MODULE_ENABLED
#include "stm32c0xx_hal_iwdg.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32c0xx_hal_uart.h"
#endif
#ifdef HAL_FDCAN_MODULE_ENABLED
#include "stm32c0xx_hal_fdcan.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32c0xx_hal_flash.h"
#endif
#ifdef HAL_SPI_MODULE_ENABLED
#include "stm32c0xx_hal_spi.h"
#endif

/* Parameter checking inside the HAL is switched off to save flash. */
#define assert_param(expr) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif
