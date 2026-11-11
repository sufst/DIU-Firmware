/*
 * Tests for calibration.c: the button sequence, the LED patterns, what gets
 * saved, how the calibration changes the pedal reading, and every way a
 * calibration can be rejected.
 *
 * The button, sensor voltages and storage are all simulated; the main-loop
 * steps are copied in run() below.
 */
#include "sim.h"
#include "calibration.h"
#include "config.h"
#include "safety.h"
#include "sensors.h"

static uint32_t now_ms = 1000;
static sensor_reading_t readings[SENSOR_COUNT];
static safety_result_t safety;
static calibration_status_t cal;

static bool button_down;

/* LED activity during the most recent run() */
static int led_on_ms;
static int led_changes;
static bool led_last;

/* Run the same steps as the firmware's main loop for a number of milliseconds */
static void run(uint32_t ms)
{
    led_on_ms = 0;
    led_changes = 0;

    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        sensors_read(now_ms, readings);
        safety_update(now_ms, readings, &safety);

        bool error = safety.apps_implausible || safety.brake_plausibility_tripped;
        for (int k = 0; k < SENSOR_COUNT; k++) {
            if (readings[k].fault != SENSOR_OK) {
                error = true;
            }
        }

        cal = calibration_update(now_ms, button_down, readings, error);
        if (cal.active) {
            safety.pedal = 0;
        }

        led_on_ms += cal.led_on ? 1 : 0;
        if (cal.led_on != led_last) {
            led_changes++;
        }
        led_last = cal.led_on;
    }
}

/* A short press, and a 2.1 s hold */
static void press(void) { button_down = true; run(100);  button_down = false; run(100); }
static void hold(void)  { button_down = true; run(2100); button_down = false; run(100); }

/* Power-cycle the board (the stored calibration survives) */
static void reboot(void)
{
    now_ms += 10;
    button_down = false;
    sensors_init();
    safety_init(now_ms);
    calibration_init(now_ms);
}

/*
 * The example pedal used below: its mechanical stops put APPS1 at 0.80 V
 * released and 3.90 V pressed (APPS2 at half of that), so mid travel is
 * 2.35 V / 1.175 V.
 */
static void pedal_released(void) { sim_apps_mv(800, 400); }
static void pedal_pressed(void)  { sim_apps_mv(3900, 1950); }
static void pedal_mid(void)      { sim_apps_mv(2350, 1175); }

static void test_uncalibrated(void)
{
    reboot();
    sim_apps_mv(500, 250);
    run(200);
    check("never calibrated: 0.5 V -> 0 %", safety.pedal, 0);
    check("  'calibrated' is false", cal.calibrated, 0);
    sim_apps_mv(2500, 1250);
    run(5);
    check("never calibrated: 2.5 V -> 50 %", safety.pedal, 500);
    check("  LED steady on when healthy", led_on_ms, 5);
    check("  not in calibration mode", cal.active, 0);
}

static void test_successful_calibration(void)
{
    press();
    check("a short press does not start calibration", cal.active, 0);

    button_down = true;
    run(1900);
    check("held 1.9 s: not yet", cal.active, 0);
    run(200);
    check("held 2.1 s: calibration mode", cal.active, 1);
    check("  pedal sent as 0 % while calibrating", safety.pedal, 0);
    button_down = false;
    run(100);

    run(2000);
    check("  slow blink: on half the time", led_on_ms, 1000);
    check("  slow blink: 1 Hz (4 changes in 2 s)", led_changes, 4);

    pedal_released();
    press();
    check("0 % recorded: still calibrating", cal.active, 1);
    run(2000);
    check("  fast blink: on half the time", led_on_ms, 1000);
    check("  fast blink: 5 Hz (20 changes in 2 s)", led_changes, 20);

    pedal_pressed();
    press();
    check("100 % recorded: calibration finished", cal.active, 0);
    check("  'calibrated' is true", cal.calibrated, 1);
    check("  saved once", sim_storage_saves, 1);
    check_near("  stored APPS1 at 0 %", sim_stored_cal.apps1_zero_mv, 800, 1);
    check_near("  stored APPS1 at 100 %", sim_stored_cal.apps1_full_mv, 3900, 1);
    check_near("  stored APPS2 at 0 %", sim_stored_cal.apps2_zero_mv, 400, 1);
    check_near("  stored APPS2 at 100 %", sim_stored_cal.apps2_full_mv, 1950, 1);
    run(500);
    check("  LED steady on afterwards", led_on_ms, 500);

    check("calibrated: fully pressed -> 100 %", safety.pedal, 1000);
    pedal_released();
    run(5);
    check("calibrated: released -> 0 %", safety.pedal, 0);
    sim_apps_mv(840, 420);
    run(5);
    check("calibrated: 1.3 % of travel is inside the dead zone -> 0 %", safety.pedal, 0);
    sim_apps_mv(3860, 1930);
    run(5);
    check("calibrated: 98.7 % of travel -> 100 %", safety.pedal, 1000);
    pedal_mid();
    run(5);
    check("calibrated: mid travel -> 50 %", safety.pedal, 500);
    check("  no implausibility or fault codes",
          safety.apps_implausible | readings[SENSOR_APPS1].fault | readings[SENSOR_APPS2].fault, 0);

    reboot();
    pedal_mid();
    run(50);
    check("after a power cycle: mid travel still 50 %", safety.pedal, 500);
    check("  'calibrated' is true", cal.calibrated, 1);
}

/* After each rejected attempt the earlier calibration must still be in use */
static void check_old_calibration_kept(const char *what)
{
    run(2500);          /* let the failure flicker finish */
    pedal_mid();
    run(150);
    check(what, safety.pedal, 500);
    check("  nothing new saved", sim_storage_saves, 1);
    check("  still 'calibrated'", cal.calibrated, 1);
}

static void test_rejected_calibrations(void)
{
    /* Hardly any pedal travel */
    hold();
    pedal_released();
    press();
    sim_apps_mv(1000, 500);
    press();
    check("tiny travel -> rejected, calibration mode left", cal.active, 0);
    run(1000);
    check("  LED flickers", led_changes > 20, 1);
    check_old_calibration_kept("  old calibration kept (mid travel 50 %)");
    run(500);
    check("  LED steady again", led_on_ms, 500);

    /* Pressed first, then released */
    hold();
    pedal_pressed();
    press();
    pedal_released();
    press();
    check_old_calibration_kept("positions the wrong way round -> rejected, old kept");

    /* Sensor fault at either step */
    hold();
    sim_apps_mv(0, 400);
    press();
    check("sensor unplugged at the 0 % step -> rejected", cal.active, 0);
    check_old_calibration_kept("  old calibration kept");

    hold();
    pedal_released();
    press();
    sim_apps_mv(3900, 5000);
    press();
    check_old_calibration_kept("sensor shorted at the 100 % step -> rejected, old kept");

    /* Nobody presses the button */
    hold();
    check("calibration started", cal.active, 1);
    run(59000);
    check("  still waiting after 59 s", cal.active, 1);
    run(1500);
    check("  abandoned after 60 s", cal.active, 0);
    check_old_calibration_kept("  old calibration kept");

    /* Storage fails */
    sim_storage_broken = true;
    hold();
    sim_apps_mv(1000, 500);
    press();
    sim_apps_mv(4000, 2000);
    press();
    check_old_calibration_kept("saving fails -> old calibration put back");
    sim_storage_broken = false;
}

static void test_health_light(void)
{
    pedal_mid();
    run(500);
    check("healthy -> LED on", led_on_ms, 500);

    sim_apps_mv(0, 1175);
    run(500);
    check("APPS1 unplugged -> LED off", led_on_ms, 0);
    pedal_mid();
    run(500);
    check("recovered -> LED on", led_on_ms, 500);

    sim_brake_mv(0);
    run(500);
    check("brake sensor unplugged -> LED off", led_on_ms, 0);
    sim_brake_mv(500);
}

static void test_button_robustness(void)
{
    reboot();
    button_down = true;
    run(10000);
    check("button stuck down from power-up -> never calibrates", cal.active, 0);
    button_down = false;
    run(200);
    hold();
    check("  works normally once released and held again", cal.active, 1);

    reboot();
    for (int i = 0; i < 400; i++) {
        button_down = (i % 10) < 5;     /* down 5 ms, up 5 ms */
        run(1);
    }
    button_down = false;
    check("4 s of 5 ms contact bounce -> never counts as a hold", cal.active, 0);
}

int main(void)
{
    sim_brake_mv(500);
    sim_mv[ADS7038_CH_SPARE1] = 2500;

    test_uncalibrated();
    test_successful_calibration();
    test_rejected_calibrations();
    test_health_light();
    test_button_robustness();

    return test_summary();
}
