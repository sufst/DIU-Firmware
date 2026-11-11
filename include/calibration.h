/*
 * Accelerator pedal calibration, using the case button and case LED.
 *
 * The pedal has its own mechanical end stops, so the sensor voltages at 0 %
 * and 100 % differ from pedal to pedal. Calibration records them:
 *
 *   1. Hold the button for 2 s.          LED blinks slowly.
 *   2. Release the pedal fully, press.   0 % recorded. LED blinks fast.
 *   3. Press the pedal fully, press.     100 % recorded and saved.
 *                                        LED goes back to steady.
 *
 * If the calibration is rejected (a sensor fault, too little pedal travel,
 * or it could not be saved) or nothing is pressed for 60 s, the LED flickers
 * for 2 s and the previous calibration is kept.
 *
 * Outside calibration the LED is the health light: on = no errors, off = an
 * error is present.
 *
 * The calibration is kept in the EEPROM (cal_storage.c) and loaded at start-up.
 * A board that has never been calibrated uses the nominal voltages from the
 * sensor table in sensors.c.
 */
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>
#include "sensors.h"

/* The four recorded voltages, in millivolts */
typedef struct {
    uint16_t apps1_zero_mv;     /* APPS1 with the pedal released      */
    uint16_t apps1_full_mv;     /* APPS1 with the pedal fully pressed */
    uint16_t apps2_zero_mv;
    uint16_t apps2_full_mv;
} apps_calibration_t;

typedef struct {
    bool active;        /* calibration mode is in progress: do not trust the pedal */
    bool calibrated;    /* a saved calibration is in use (false = nominal values)  */
    bool led_on;        /* what the case LED should be showing right now           */
} calibration_status_t;

/* Call once at start-up, after sensors_init(). Loads the saved calibration. */
void calibration_init(uint32_t now_ms);

/*
 * Call once per sample.
 *   button_down  raw state of the case button (debounced in here)
 *   readings     latest sensor readings
 *   error        true if any error is present (for the health light)
 */
calibration_status_t calibration_update(uint32_t now_ms, bool button_down,
                                        const sensor_reading_t readings[SENSOR_COUNT],
                                        bool error);

#endif
