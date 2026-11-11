/*
 * M24256E 256 kbit (32 KB) I2C EEPROM (IC8): storage that survives
 * power-off and re-flashing the firmware.
 */
#ifndef EEPROM_H
#define EEPROM_H

#include <stdbool.h>
#include <stdint.h>

#define EEPROM_SIZE_BYTES   32768u
#define EEPROM_PAGE_BYTES   64u

bool eeprom_read(uint16_t address, uint8_t *data, uint16_t len);

/*
 * Write up to one page. The bytes must not cross a 64-byte page boundary
 * (the chip would wrap round within the page). Blocks for about 5 ms while
 * the chip stores the data.
 */
bool eeprom_write(uint16_t address, const uint8_t *data, uint16_t len);

#endif
