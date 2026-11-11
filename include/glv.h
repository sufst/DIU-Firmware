/*
 * GLV (low-voltage battery) supply monitor.
 *
 * The GLV rail reaches pin PA0 through a resistor divider, and is measured
 * with the microcontroller's own ADC. (This is separate from the ADS7038,
 * which reads the driver sensors.)
 */
#ifndef GLV_H
#define GLV_H

#include <stdbool.h>
#include <stdint.h>

/* Start the ADC measuring continuously in the background. */
bool glv_init(void);

/* Latest GLV supply voltage in millivolts (0 if the ADC failed to start). */
uint16_t glv_read_mv(void);

#endif
