/*
 * Board support: clock, pin setup and timing helpers.
 *
 * Pin map (STM32C092FCP6, TSSOP20) - pins used by this firmware:
 *   PA4  SPI_CS    (GPIO output)   -> ADS7038 CS
 *   PA5  SPI_SCK   (SPI1, AF0)     -> ADS7038 SCLK
 *   PA6  SPI_MISO  (SPI1, AF0)     <- ADS7038 SDO
 *   PA7  SPI_MOSI  (SPI1, AF0)     -> ADS7038 SDI
 *   PB1  CAN_TX    (FDCAN1, AF3)   -> TCAN3404 TXD   (package pin 15)
 *   PA11 CAN_RX    (FDCAN1, AF4)   <- TCAN3404 RXD   (package pin 16)
 *   PC14/PC15      25 MHz crystal
 *   PA13/PA14      SWDIO/SWCLK
 *
 *   PA1  CASE_LED  (GPIO output)   -> 220R -> case connector P1 pin 3
 *   PA3  CASE_BTN  (GPIO input)    <- case button
 *   PA0  GLV_MONITOR (ADC1 IN0)    <- GLV rail through a 56k/10k divider
 *   PA2  SERIAL_TX (USART2, AF1)   -> test point TP5
 *   PB3  I2C_SCL   (I2C2, AF5)     package pin 20
 *   PA12 I2C_SDA   (I2C2, AF6)     package pin 17
 *
 * Not used yet (left in its reset state, analog/hi-Z):
 *   PB7 NEOPIXEL
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32c0xx_hal.h"

/* ADC chip select */
#define ADC_CS_PORT     GPIOA
#define ADC_CS_PIN      GPIO_PIN_4

/*
 * Case LED. The pin drives the LED through a 220R resistor and the other leg
 * of the LED goes to ground, so a high pin lights it.
 */
#define LED_PORT            GPIOA
#define LED_PIN             GPIO_PIN_1
#define LED_ON_LEVEL        GPIO_PIN_SET

/*
 * Case button, wired between the pin and +3.3V: pressed = high. The internal
 * pull-down of the microcontroller holds the pin low when released.
 * If the button is ever wired to ground instead, change BUTTON_PRESSED_LEVEL
 * to GPIO_PIN_RESET and BUTTON_PULL to GPIO_PULLUP.
 */
#define BUTTON_PORT             GPIOA
#define BUTTON_PIN              GPIO_PIN_3
#define BUTTON_PRESSED_LEVEL    GPIO_PIN_SET
#define BUTTON_PULL             GPIO_PULLDOWN

/* Start the HAL, the system clock, the 1 ms tick and the button/LED pins. */
void board_init(void);

/* Raw (not debounced) state of the case button. */
bool board_button_pressed(void);

void board_led_set(bool on);

/*
 * Watchdog. After board_watchdog_start(), board_watchdog_kick() must be
 * called at least every WATCHDOG_TIMEOUT_MS or the microcontroller resets.
 * Once started it cannot be stopped.
 */
void board_watchdog_start(void);
void board_watchdog_kick(void);

/* Busy-wait for a number of microseconds (accurate to a microsecond or two). */
void delay_us(uint32_t us);

/* Unrecoverable setup error: restart the microcontroller. */
void board_fatal(void);

#endif
