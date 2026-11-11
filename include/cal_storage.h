/*
 * Keeps the pedal calibration in the I2C EEPROM, so it survives power-off
 * and re-flashing the firmware.
 */
#ifndef CAL_STORAGE_H
#define CAL_STORAGE_H

#include <stdbool.h>
#include "calibration.h"

/* Read the saved calibration. Returns false if none has been saved (or it is corrupt). */
bool cal_storage_load(apps_calibration_t *cal);

/* Save a calibration. Returns false if it could not be written and read back. */
bool cal_storage_save(const apps_calibration_t *cal);

#endif
