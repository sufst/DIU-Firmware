/*
 * Driver Input Unit firmware.
 *
 * Once per SAMPLE_PERIOD_MS (1 ms) the main loop:
 *   1. reads every driver sensor through the ADS7038 ADC       (sensors.c)
 *   2. works out the pedal position and the safety flags       (safety.c)
 *   3. handles the case button and LED (pedal calibration)     (calibration.c)
 *   4. sends the sensor frame on CAN
 *
 * Less often it also reads the IMU, board temperature and GLV supply voltage,
 * sends them on CAN, and prints a line of values on the serial debug port.
 * The slower jobs are spread over different milliseconds so that no single
 * pass through the loop has to do everything.
 *
 * ---------------------------------------------------------------------------
 * CAN frames. All values are little-endian ("Intel" byte order in a DBC
 * file); bit 0 is the lowest bit of byte 0.
 *
 * CAN_ID_SENSORS (0x100), every 1 ms, 8 bytes:
 *   bits  0- 9   accelerator pedal position   0.1 % per bit     0-100 %
 *   bits 10-21   brake pressure               0.1 bar per bit   0-200 bar
 *   bits 22-33   steering angle               0.1 % per bit     0-100 %
 *   bits 34-37   message counter: goes up by one every frame, 15 wraps to 0
 *   bit  38      pedal not calibrated (nominal sensor voltages are in use)
 *   bits 39-45   (free)
 *   bit  46      APPS implausibility (APPS1 and APPS2 disagree for > 100 ms)
 *   bit  47      APPS / brake plausibility check tripped (EV6.3)
 *   bits 48-50   APPS1 fault code
 *   bits 51-53   APPS2 fault code
 *   bits 54-56   brake pressure sensor fault code
 *   bits 57-59   steering sensor fault code
 *   bits 60-62   (free)
 *   bit  63      pedal calibration in progress (pedal position is sent as 0 %)
 *
 *   Fault codes (sensor_fault_t in sensors.h):
 *     0 = OK                               3 = out of range
 *     1 = shorted to ground / disconnected 4 = no data (ADC not responding)
 *     2 = shorted to supply
 *
 * CAN_ID_ACCEL (0x101), every 10 ms, 6 bytes - only sent while the IMU works:
 *   bytes 0-1, 2-3, 4-5   acceleration X, Y, Z   signed, 1 mg per bit
 *
 * CAN_ID_GYRO (0x102), every 10 ms, 6 bytes - only sent while the IMU works:
 *   bytes 0-1, 2-3, 4-5   rotation rate X, Y, Z  signed, 0.1 deg/s per bit
 *
 * CAN_ID_STATUS (0x103), every 100 ms, 5 bytes:
 *   bytes 0-1   GLV supply voltage     unsigned, 1 mV per bit
 *   bytes 2-3   board temperature      signed, 0.1 degC per bit
 *   byte  4     bit 0: IMU working     bit 1: temperature sensor working
 */
#include "board.h"
#include "calibration.h"
#include "can.h"
#include "config.h"
#include "debug.h"
#include "glv.h"
#include "i2c_bus.h"
#include "lsm6dso32.h"
#include "mcp9800.h"
#include "safety.h"
#include "sensors.h"
#include "serial.h"

/* The temperature sensor only produces a new value every 240 ms */
#define TEMPERATURE_PERIOD_MS   500u

/* -------------------------------------------------------------------------
 * Sensor frame layout
 * ------------------------------------------------------------------------- */

#define NOT_SENT            0xFFu   /* this sensor's value is not put in the frame */
#define FAULT_CODE_BITS     3u

/* Where each sensor's value and fault code sit in the sensor frame */
typedef struct {
    uint8_t value_bit;      /* first bit of the value, or NOT_SENT */
    uint8_t value_length;   /* number of bits                      */
    uint8_t fault_bit;      /* first bit of the 3-bit fault code   */
} can_layout_t;

static const can_layout_t s_layout[SENSOR_COUNT] = {
    /* The two APPS are combined into the single pedal position, so only
     * their fault codes are sent, not their individual values. */
    [SENSOR_APPS1]    = { NOT_SENT,  0, 48 },
    [SENSOR_APPS2]    = { NOT_SENT,  0, 51 },
    [SENSOR_BRAKE]    = { 10,       12, 54 },
    [SENSOR_STEERING] = { 22,       12, 57 },
    /* [SENSOR_SPARE2] = { NOT_SENT, 0, 60 },   fault code fits here; its value
     *                                           would need a frame of its own */
};

#define PEDAL_BIT                   0u
#define PEDAL_LENGTH                10u
#define COUNTER_BIT                 34u
#define COUNTER_LENGTH              4u
#define FLAG_BIT_NOT_CALIBRATED     38u
#define FLAG_BIT_APPS_IMPLAUSIBLE   46u
#define FLAG_BIT_BRAKE_PLAUSIBILITY 47u
#define FLAG_BIT_CALIBRATING        63u

/* -------------------------------------------------------------------------
 * State shared between the jobs in the main loop
 * ------------------------------------------------------------------------- */

static sensor_reading_t s_readings[SENSOR_COUNT];
static safety_result_t s_safety;
static calibration_status_t s_cal;

static imu_data_t s_imu;
static bool s_imu_ok;
static int16_t s_temperature_dc;
static bool s_temperature_ok;
static uint16_t s_glv_mv;

static uint8_t s_frame_counter;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Write the lowest 'length' bits of value into the frame, starting at start_bit */
static void put_bits(uint8_t frame[8], uint32_t start_bit, uint32_t length, uint32_t value)
{
    for (uint32_t i = 0; i < length; i++) {
        uint32_t bit = start_bit + i;
        if ((value >> i) & 1u) {
            frame[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
        }
    }
}

/* Store a 16-bit value low byte first */
static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)(value >> 8);
}

/*
 * Returns true each time 'period_ms' has passed, for running a job at a
 * steady rate. *next_ms holds when it is next due.
 */
static bool due(uint32_t now_ms, uint32_t *next_ms, uint32_t period_ms)
{
    if ((int32_t)(now_ms - *next_ms) < 0) {
        return false;
    }
    *next_ms += period_ms;

    /* If we have fallen a long way behind, start again from now rather
     * than running the job many times in a row to catch up. */
    if ((int32_t)(now_ms - *next_ms) >= 0) {
        *next_ms = now_ms + period_ms;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * CAN frames
 * ------------------------------------------------------------------------- */

static void send_sensor_frame(void)
{
    uint8_t frame[8] = { 0 };

    put_bits(frame, PEDAL_BIT, PEDAL_LENGTH, (uint32_t)s_safety.pedal);
    put_bits(frame, COUNTER_BIT, COUNTER_LENGTH, s_frame_counter);
    put_bits(frame, FLAG_BIT_NOT_CALIBRATED, 1, !s_cal.calibrated);
    put_bits(frame, FLAG_BIT_APPS_IMPLAUSIBLE, 1, s_safety.apps_implausible);
    put_bits(frame, FLAG_BIT_BRAKE_PLAUSIBILITY, 1, s_safety.brake_plausibility_tripped);
    put_bits(frame, FLAG_BIT_CALIBRATING, 1, s_cal.active);

    for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
        if (s_layout[i].value_bit != NOT_SENT) {
            put_bits(frame, s_layout[i].value_bit, s_layout[i].value_length,
                     (uint32_t)s_readings[i].value);
        }
        put_bits(frame, s_layout[i].fault_bit, FAULT_CODE_BITS, (uint32_t)s_readings[i].fault);
    }

    can_send(CAN_ID_SENSORS, frame, sizeof(frame));

    /* The receiver can tell a frozen or repeated frame by the counter not moving */
    s_frame_counter = (uint8_t)((s_frame_counter + 1u) & 0x0Fu);
}

static void send_xyz_frame(uint16_t id, const int16_t xyz[3])
{
    uint8_t frame[6];

    for (uint32_t axis = 0; axis < 3u; axis++) {
        put_u16(&frame[2u * axis], (uint16_t)xyz[axis]);
    }
    can_send(id, frame, sizeof(frame));
}

static void send_status_frame(void)
{
    uint8_t frame[5];

    put_u16(&frame[0], s_glv_mv);
    put_u16(&frame[2], (uint16_t)s_temperature_dc);
    frame[4] = (uint8_t)((s_imu_ok ? 0x01u : 0u) | (s_temperature_ok ? 0x02u : 0u));

    can_send(CAN_ID_STATUS, frame, sizeof(frame));
}

/* -------------------------------------------------------------------------
 * Serial debug output
 * ------------------------------------------------------------------------- */

static void print_debug_line(uint32_t now_ms)
{
    /* Kept between calls: the serial port sends from this buffer in the background */
    static char text[2u * DEBUG_LINE_MAX];
    static uint32_t lines_printed;

    debug_data_t data;
    size_t length = 0;

    /* Still sending the previous line: skip this one rather than wait */
    if (serial_busy()) {
        return;
    }

    data.time_ms = now_ms;
    for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
        data.readings[i] = s_readings[i];
    }
    data.safety = s_safety;
    data.calibrating = s_cal.active;
    data.calibrated = s_cal.calibrated;
    data.glv_mv = s_glv_mv;
    data.temperature_ok = s_temperature_ok;
    data.temperature_dc = s_temperature_dc;
    data.imu_ok = s_imu_ok;
    data.imu = s_imu;

    /* Repeat the headings now and then so they stay on screen */
    if (lines_printed % DEBUG_HEADER_EVERY == 0u) {
        length = debug_format_header(text);
    }
    length += debug_format_line(&text[length], &data);
    lines_printed++;

    serial_send(text, (uint16_t)length);
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */

int main(void)
{
    board_init();
    if (!can_init(CAN_BITRATE)) {
        board_fatal();
    }

    /* None of these stop the board working if they fail: the pedal, brake
     * and steering signals do not depend on them. */
    i2c_bus_init();
    glv_init();
    serial_init();

    uint32_t now_ms = HAL_GetTick();
    sensors_init();
    safety_init(now_ms);
    calibration_init(now_ms);           /* loads the saved pedal calibration */

    /* From here on, a hang of more than WATCHDOG_TIMEOUT_MS resets the board */
    board_watchdog_start();

    /* When each slower job next runs. The different starting offsets keep
     * them from all landing in the same millisecond. */
    uint32_t next_imu_ms = now_ms + 3u;
    uint32_t next_gyro_frame_ms = now_ms + 8u;
    uint32_t next_status_ms = now_ms + 51u;
    uint32_t next_temperature_ms = now_ms + 76u;
    uint32_t next_debug_ms = now_ms + 13u;

    uint32_t last_run_ms = now_ms;

    for (;;) {
        /* Sleep until the next sample is due (the 1 ms tick wakes the CPU) */
        while ((HAL_GetTick() - last_run_ms) < SAMPLE_PERIOD_MS) {
            __WFI();
        }
        now_ms = HAL_GetTick();
        last_run_ms = now_ms;
        board_watchdog_kick();

        /* ---- Every sample: driver sensors, safety checks, sensor frame ---- */

        sensors_read(now_ms, s_readings);
        safety_update(now_ms, s_readings, &s_safety);

        /* The case LED is on only while nothing at all is wrong */
        bool error = s_safety.apps_implausible || s_safety.brake_plausibility_tripped;
        for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
            if (s_readings[i].fault != SENSOR_OK) {
                error = true;
            }
        }

        s_cal = calibration_update(now_ms, board_button_pressed(), s_readings, error);
        board_led_set(s_cal.led_on);

        /* The pedal is being moved on purpose during calibration: never
         * let that be taken as a throttle request. */
        if (s_cal.active) {
            s_safety.pedal = 0;
        }

        send_sensor_frame();

        /* ---- Slower jobs ---- */

        /* IMU: read it and send the acceleration; the rotation rates from
         * the same reading follow 5 ms later to spread the CAN traffic. */
        if (due(now_ms, &next_imu_ms, IMU_PERIOD_MS)) {
            s_imu_ok = lsm6dso32_read(now_ms, &s_imu);
            if (s_imu_ok) {
                send_xyz_frame(CAN_ID_ACCEL, s_imu.accel_mg);
            }
        }
        if (due(now_ms, &next_gyro_frame_ms, IMU_PERIOD_MS) && s_imu_ok) {
            send_xyz_frame(CAN_ID_GYRO, s_imu.gyro_ddps);
        }

        if (due(now_ms, &next_temperature_ms, TEMPERATURE_PERIOD_MS)) {
            s_temperature_ok = mcp9800_read(now_ms, &s_temperature_dc);
        }

        if (due(now_ms, &next_status_ms, STATUS_PERIOD_MS)) {
            s_glv_mv = glv_read_mv();
            send_status_frame();
        }

        if (due(now_ms, &next_debug_ms, DEBUG_PERIOD_MS)) {
            print_debug_line(now_ms);
        }
    }
}
