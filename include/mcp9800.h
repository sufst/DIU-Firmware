/*
 * MCP9800 board temperature sensor (U8) on I2C.
 */
#ifndef MCP9800_H
#define MCP9800_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Read the board temperature in units of 0.1 degC (253 = 25.3 degC).
 * Returns false if the sensor is not answering; it is then set up again
 * every I2C_RETRY_PERIOD_MS. The first call sets the sensor up.
 */
bool mcp9800_read(uint32_t now_ms, int16_t *deci_celsius);

#endif
