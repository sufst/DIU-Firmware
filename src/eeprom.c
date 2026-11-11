#include "eeprom.h"
#include "config.h"
#include "i2c_bus.h"

/* The datasheet write time is 5 ms at most; allow double */
#define WRITE_TIMEOUT_MS    10u

bool eeprom_read(uint16_t address, uint8_t *data, uint16_t len)
{
    if ((uint32_t)address + len > EEPROM_SIZE_BYTES) {
        return false;
    }
    return i2c_bus_read(I2C_ADDR_EEPROM, address, I2C_REG_16BIT, data, len);
}

bool eeprom_write(uint16_t address, const uint8_t *data, uint16_t len)
{
    /* Refuse anything that would run past the end of its 64-byte page */
    if (len == 0u || (address % EEPROM_PAGE_BYTES) + len > EEPROM_PAGE_BYTES ||
        (uint32_t)address + len > EEPROM_SIZE_BYTES) {
        return false;
    }

    if (!i2c_bus_write(I2C_ADDR_EEPROM, address, I2C_REG_16BIT, data, len)) {
        return false;
    }

    /* While it is busy storing the bytes the chip does not answer at all,
     * so wait until it does again. */
    return i2c_bus_device_ready(I2C_ADDR_EEPROM, WRITE_TIMEOUT_MS);
}
