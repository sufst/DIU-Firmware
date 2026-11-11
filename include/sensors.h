/*
 * The list of sensors, and conversion from ADC readings to real units.
 *
 * To add a sensor (e.g. the unused SPARE2 input):
 *   1. add an entry to sensor_id_t below (before SENSOR_COUNT)
 *   2. add its row to the table at the top of sensors.c
 *   3. add its position in the CAN frame to the table at the top of main.c
 * It is then sampled, fault-checked and transmitted automatically.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SENSOR_APPS1,       /* accelerator pedal position 1, 0.1 %   */
    SENSOR_APPS2,       /* accelerator pedal position 2, 0.1 %   */
    SENSOR_BRAKE,       /* brake pressure,               0.1 bar */
    SENSOR_STEERING,    /* steering angle (SPARE1 input), 0.1 %  */
    /* SENSOR_SPARE2, */
    SENSOR_COUNT
} sensor_id_t;

/*
 * Fault code for one sensor. These numbers are sent on CAN (3 bits each),
 * so do not renumber them without updating whatever decodes the frame.
 */
typedef enum {
    SENSOR_OK               = 0,
    SENSOR_SHORT_TO_GND     = 1,    /* reads ~0 V: shorted to ground, or disconnected
                                       (the board's pull-down makes these look the same) */
    SENSOR_SHORT_TO_VCC     = 2,    /* reads ~5 V: shorted to the supply */
    SENSOR_OUT_OF_RANGE     = 3,    /* outside the sensor's working range, but not a dead short */
    SENSOR_NO_DATA          = 4     /* the ADC is not responding, so nothing is known */
} sensor_fault_t;

typedef struct {
    uint16_t       millivolts;  /* voltage at the connector pin                     */
    int16_t        value;       /* scaled value in 0.1 units, clamped to its range  */
    sensor_fault_t fault;       /* SENSOR_OK (0) when healthy                       */
} sensor_reading_t;

/* Call once at start-up, before anything else in this file. */
void sensors_init(void);

/*
 * Change the voltages that mean "minimum" and "maximum" for one sensor (used
 * by the pedal calibration). The fault limits are not affected: they always
 * come from the sensor table.
 *
 * sensors_range_valid() says whether a range is acceptable: both ends inside
 * the working range of the sensor, the right way round, and at least a
 * quarter of its nominal span apart. sensors_set_range() refuses (returns
 * false, changes nothing) if it is not.
 */
bool sensors_range_valid(sensor_id_t id, uint16_t mv_min, uint16_t mv_max);
bool sensors_set_range(sensor_id_t id, uint16_t mv_min, uint16_t mv_max);

/*
 * Sample every sensor into readings[]. now_ms is the current time.
 *
 * If the ADC itself is not responding, every sensor is reported as
 * SENSOR_NO_DATA with a value of 0, and the ADC is set up again every
 * ADC_RETRY_PERIOD_MS.
 */
void sensors_read(uint32_t now_ms, sensor_reading_t readings[SENSOR_COUNT]);

#endif
