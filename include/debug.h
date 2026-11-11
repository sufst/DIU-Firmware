/*
 * Builds the text sent to the serial debug port: one line of values per
 * update, in fixed-width columns so they line up under the headings in a
 * terminal such as PuTTY.
 *
 * This file only produces text; serial.c sends it. It has no hardware
 * dependencies, so it is also built and checked by the PC tests.
 */
#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lsm6dso32.h"
#include "safety.h"
#include "sensors.h"

/* A buffer of this size holds any heading or value line */
#define DEBUG_LINE_MAX  256u

/* Everything that is printed */
typedef struct {
    uint32_t time_ms;
    sensor_reading_t readings[SENSOR_COUNT];
    safety_result_t safety;
    bool calibrating;
    bool calibrated;
    uint16_t glv_mv;
    bool temperature_ok;
    int16_t temperature_dc;     /* 0.1 degC */
    bool imu_ok;
    imu_data_t imu;
} debug_data_t;

/* Write the column headings into buf. Returns the number of characters. */
size_t debug_format_header(char buf[DEBUG_LINE_MAX]);

/* Write one line of values into buf. Returns the number of characters. */
size_t debug_format_line(char buf[DEBUG_LINE_MAX], const debug_data_t *data);

#endif
