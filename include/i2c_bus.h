/*
 * The I2C bus (I2C2, 400 kHz) shared by the temperature sensor, the IMU and
 * the EEPROM. Addresses are the plain 7-bit ones, as on the schematic.
 */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>

/* How many bytes a device uses for its register / memory address */
typedef enum {
    I2C_REG_8BIT = 1,       /* sensors      */
    I2C_REG_16BIT = 2       /* the EEPROM   */
} i2c_reg_size_t;

bool i2c_bus_init(void);

/* Read or write len bytes starting at a register. Return false on no answer. */
bool i2c_bus_read(uint8_t address, uint16_t reg, i2c_reg_size_t reg_size,
                  uint8_t *data, uint16_t len);
bool i2c_bus_write(uint8_t address, uint16_t reg, i2c_reg_size_t reg_size,
                   const uint8_t *data, uint16_t len);

/* True if the device answers to its address. Waits up to timeout_ms for it. */
bool i2c_bus_device_ready(uint8_t address, uint32_t timeout_ms);

#endif
