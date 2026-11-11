/*
 * Driver Input Unit - project configuration.
 *
 * Every tunable number lives here. The per-sensor voltage ranges are in the
 * sensor table at the top of sensors.c.
 *
 * Unless stated otherwise, pedal/steering values are in units of 0.1 %
 * (so 250 means 25.0 %) and pressures are in units of 0.1 bar.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ---- CAN ---------------------------------------------------------------- */

#define CAN_BITRATE                 500000u   /* bit/s */

/* 11-bit IDs and send periods of the frames (layouts are in main.c) */
#define CAN_ID_SENSORS              0x100u    /* pedal, brake, steering, flags: every sample */
#define CAN_ID_ACCEL                0x101u    /* IMU acceleration   */
#define CAN_ID_GYRO                 0x102u    /* IMU rotation rate  */
#define CAN_ID_STATUS               0x103u    /* supply voltage, board temperature */

#define IMU_PERIOD_MS               10u       /* 100 Hz */
#define STATUS_PERIOD_MS            100u      /* 10 Hz  */

/* ---- Sampling ----------------------------------------------------------- */

/* The sensors are sampled and the CAN frame is sent once per period. */
#define SAMPLE_PERIOD_MS            1u

/* The ADS7038 uses its AVDD pin as the reference, which is the +5V rail. */
#define ADC_VREF_MV                 5000u

/*
 * Oversampling done inside the ADC: it takes 2^N conversions and returns
 * their average. 4 -> 16 samples, which takes about 17 us per channel.
 * Range 0 (off) to 7 (128 samples, about 136 us per channel).
 */
#define ADC_OVERSAMPLING_LOG2       4u

/* If the ADC stops answering, try to set it up again this often. */
#define ADC_RETRY_PERIOD_MS         100u

/* ---- APPS plausibility (APPS1 against APPS2) ----------------------------- */

/* The two pedal sensors may differ by up to this much (0.1 %): 10.0 %. */
#define APPS_MAX_DIFFERENCE         100

/*
 * The two signals never sit at the same voltage in normal use (APPS2 runs at
 * half the voltage of APPS1, so they are at least 250 mV apart). If they are
 * closer than this, the two signal wires are shorted together.
 */
#define APPS_SHORT_THRESHOLD_MV     100

/* The implausibility flag is set once the problem has lasted longer than this. */
#define APPS_IMPLAUSIBLE_TIME_MS    100u

/* ---- APPS / brake pedal plausibility (EV6.3) ------------------------------ */

/* Brake pressure above which the brakes count as "actuated" (0.1 bar): 5.0 bar. */
#define BRAKE_ACTUATED_THRESHOLD    50

/* Trips when the brakes are actuated and the pedal is above this (0.1 %): 25.0 % ... */
#define BPP_APPS_TRIP_THRESHOLD     250

/* ... at the same time, for longer than this. */
#define BPP_TRIP_TIME_MS            500u

/* Stays tripped until the pedal drops below this (0.1 %): 5.0 %. */
#define BPP_APPS_RESET_THRESHOLD    50

/* ---- Watchdog -------------------------------------------------------------- */

/*
 * If the main loop stops running for this long the microcontroller is reset.
 * Must be comfortably longer than the slowest thing the loop ever does
 * (setting up the ADC, ~10 ms; saving the calibration, ~10 ms).
 */
#define WATCHDOG_TIMEOUT_MS         250u

/* ---- GLV supply monitor ---------------------------------------------------- */

/* Resistor divider from the GLV rail to the microcontroller pin: R2 and R3. */
#define GLV_DIVIDER_TOP_OHMS        56000u
#define GLV_DIVIDER_BOTTOM_OHMS     10000u

/* The microcontroller's ADC measures against its 3.3 V supply. */
#define MCU_VREF_MV                 3300u

/* ---- I2C devices ----------------------------------------------------------- */

/* 7-bit addresses, as marked on the schematic */
#define I2C_ADDR_TEMP_SENSOR        0x4Du     /* MCP9800A5  */
#define I2C_ADDR_EEPROM             0x50u     /* M24256E    */
#define I2C_ADDR_IMU                0x6Au     /* LSM6DSO32  */

/* A device that stops answering is set up again this often. */
#define I2C_RETRY_PERIOD_MS         1000u

/* ---- Serial debug output --------------------------------------------------- */

/* 8 data bits, no parity, 1 stop bit. PuTTY: connection type Serial, this speed. */
#define DEBUG_BAUD                  921600u

/* One line of values is printed this often: 20 ms = 50 lines per second. */
#define DEBUG_PERIOD_MS             20u

/* The column headings are printed again after this many lines. */
#define DEBUG_HEADER_EVERY          40u

/* ---- Pedal calibration (case button and LED) ------------------------------- */

/* The button must read steady for this long before a change is believed. */
#define BUTTON_DEBOUNCE_MS          30u

/* Hold the button this long to enter calibration mode. */
#define CAL_HOLD_TIME_MS            2000u

/* Calibration is abandoned if no button press arrives within this time. */
#define CAL_TIMEOUT_MS              60000u

/* LED blink periods (on for half, off for half). */
#define CAL_LED_ZERO_PERIOD_MS      1000u   /* slow: waiting for the 0 % position    */
#define CAL_LED_FULL_PERIOD_MS      200u    /* fast: waiting for the 100 % position  */
#define CAL_LED_FAIL_PERIOD_MS      60u     /* flicker: calibration was rejected     */
#define CAL_FAIL_INDICATION_MS      2000u   /* how long the flicker lasts            */

/*
 * Dead zone at each end of the calibrated pedal travel (0.1 %): 2.0 %.
 * Without it, noise would stop a released pedal reading exactly 0 %, and a
 * pedal pressed a hair less hard than during calibration would never reach
 * 100 %.
 */
#define APPS_CAL_DEADZONE           20

#endif
