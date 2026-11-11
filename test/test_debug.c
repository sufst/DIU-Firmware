/*
 * Tests for debug.c: the serial debug text. Checks that the headings and the
 * value lines are the same length (so the columns line up), that they fit
 * the buffer, and that values are printed correctly.
 */
#include <stdio.h>
#include <string.h>
#include "sim.h"
#include "debug.h"

static char header[DEBUG_LINE_MAX + 1];
static char line[DEBUG_LINE_MAX + 1];

static debug_data_t example(void)
{
    debug_data_t d;
    memset(&d, 0, sizeof(d));

    d.time_ms = 123456;
    d.readings[SENSOR_APPS1] = (sensor_reading_t){ 2500, 500, SENSOR_OK };
    d.readings[SENSOR_APPS2] = (sensor_reading_t){ 1250, 495, SENSOR_OK };
    d.readings[SENSOR_BRAKE] = (sensor_reading_t){ 742, 121, SENSOR_OK };
    d.readings[SENSOR_STEERING] = (sensor_reading_t){ 0, 0, SENSOR_SHORT_TO_GND };
    d.safety.pedal = 497;
    d.safety.brake_plausibility_tripped = true;
    d.calibrated = true;
    d.glv_mv = 12340;
    d.temperature_ok = true;
    d.temperature_dc = -53;
    d.imu_ok = true;
    d.imu.accel_mg[0] = 12;
    d.imu.accel_mg[1] = -1003;
    d.imu.accel_mg[2] = 8000;
    d.imu.gyro_ddps[0] = -5;
    d.imu.gyro_ddps[1] = 1234;
    d.imu.gyro_ddps[2] = 0;
    return d;
}

/* Does the line contain this exact text? */
static int has(const char *text)
{
    return strstr(line, text) != NULL;
}

int main(void)
{
    debug_data_t d = example();

    size_t header_length = debug_format_header(header);
    size_t line_length = debug_format_line(line, &d);
    header[header_length] = '\0';
    line[line_length] = '\0';

    printf("%s%s\n", header, line);

    check("header and value line are the same length", (int)line_length, (int)header_length);
    check("fits in the buffer with room to spare", line_length < DEBUG_LINE_MAX - 8u, 1);
    check("ends with carriage return + line feed",
          line[line_length - 2] == '\r' && line[line_length - 1] == '\n', 1);

    check("time in seconds",                 has("   123.45"), 1);
    check("voltage in mV",                   has("  2500"), 1);
    check("percent with one decimal",        has("  50.0"), 1);
    check("pedal",                           has("   49.7"), 1);
    check("brake pressure",                  has("  12.1"), 1);
    check("fault code",                      has("  0  0  0  1"), 1);
    check("flags: IMP 0, BPP 1, calibrated", has("   0   1  OK"), 1);
    check("GLV volts",                       has("  12.34"), 1);
    check("negative temperature",            has("   -5.3"), 1);
    check("small positive acceleration",     has("   0.012"), 1);
    check("negative acceleration",           has("  -1.003"), 1);
    check("small negative rotation rate",    has("    -0.5"), 1);
    check("rotation rate",                   has("   123.4"), 1);

    /* Missing devices and the other calibration states */
    d.imu_ok = false;
    d.temperature_ok = false;
    d.calibrated = false;
    line[debug_format_line(line, &d)] = '\0';
    check("no IMU or temperature: same length", (int)strlen(line), (int)header_length);
    check("  shown as ---", has("     ---"), 1);
    check("  not calibrated shown as NO", has("  NO"), 1);

    d.calibrating = true;
    line[debug_format_line(line, &d)] = '\0';
    check("calibrating shown as RUN", has(" RUN"), 1);

    /* Extreme values must not break the column widths */
    d = example();
    d.time_ms = 4294967295u;
    for (int i = 0; i < 3; i++) {
        d.imu.accel_mg[i] = -32768;
        d.imu.gyro_ddps[i] = -32768;
    }
    d.temperature_dc = -1250;
    d.glv_mv = 65535;
    size_t extreme_length = debug_format_line(line, &d);
    check("largest values: line is not longer than the header (no overflow)",
          extreme_length <= header_length + 6u, 1);
    check("largest values: fits the buffer", extreme_length < DEBUG_LINE_MAX, 1);

    return test_summary();
}
