/*
 * Turns the two accelerator sensors into one trustworthy pedal position, and
 * runs the safety checks on it. The results are sent on CAN; it is up to the
 * VCU to act on the flags (this board does not control torque).
 *
 * 1. APPS implausibility
 *    The two accelerator sensors must agree. The flag is set when, for longer
 *    than APPS_IMPLAUSIBLE_TIME_MS, any of these is true:
 *      - they differ by more than APPS_MAX_DIFFERENCE (10 % of pedal travel)
 *      - their signal wires are shorted together
 *      - either sensor has a fault
 *    It clears as soon as the sensors agree again.
 *
 * 2. Pedal position
 *      - sensors agree:                  the average of the two
 *      - problem seen, flag not yet set: the lower of the two (never
 *                                        over-reports during the 100 ms wait)
 *      - APPS implausibility flag set:   0 %
 *
 * 3. APPS / brake pedal plausibility (Formula Student rule EV6.3)
 *    The flag is set when the brakes are actuated and the accelerator is past
 *    25 % at the same time for longer than 500 ms. Once set it stays set -
 *    even if the brakes are released - until the accelerator drops below 5 %.
 *    The pedal position is still reported truthfully while this is tripped.
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>
#include <stdint.h>
#include "sensors.h"

typedef struct {
    int16_t pedal;                      /* accelerator pedal position, 0.1 % */
    bool    apps_implausible;
    bool    brake_plausibility_tripped;
} safety_result_t;

/* Call once at start-up. */
void safety_init(uint32_t now_ms);

/* Call once per sample with the latest readings; fills in result. */
void safety_update(uint32_t now_ms, const sensor_reading_t readings[SENSOR_COUNT],
                   safety_result_t *result);

#endif
