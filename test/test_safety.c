/*
 * Tests for sensors.c (scaling, fault codes) and safety.c (pedal position,
 * APPS implausibility, EV6.3 brake plausibility).
 *
 * Sensor voltages are set through the fake ADC and the real code is then
 * run for a number of simulated milliseconds.
 */
#include "sim.h"
#include "safety.h"
#include "sensors.h"

static uint32_t now_ms = 1000;
static sensor_reading_t readings[SENSOR_COUNT];
static safety_result_t safety;

#define APPS1 readings[SENSOR_APPS1]
#define APPS2 readings[SENSOR_APPS2]
#define BRAKE readings[SENSOR_BRAKE]
#define STEER readings[SENSOR_STEERING]

/* Run the firmware logic for a number of milliseconds */
static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        now_ms++;
        sensors_read(now_ms, readings);
        safety_update(now_ms, readings, &safety);
    }
}

/* Pedal released, brakes off, long enough for every timer to settle */
static void rest(void)
{
    sim_pedal_percent(0);
    sim_brake_mv(BAR_TO_MV(0));
    run(200);
}

static void test_scaling(void)
{
    rest();
    check("rest: pedal 0 %", safety.pedal, 0);
    check("rest: no fault codes", APPS1.fault | APPS2.fault | BRAKE.fault | STEER.fault, 0);
    check("rest: no flags", safety.apps_implausible | safety.brake_plausibility_tripped, 0);

    sim_pedal_percent(50);
    sim_brake_mv(BAR_TO_MV(100));
    run(1);
    check("pedal 50.0 %", safety.pedal, 500);
    check("brake 100.0 bar", BRAKE.value, 1000);
    check("steering 50.0 %", STEER.value, 500);

    sim_pedal_percent(100);
    run(1);
    check("pedal 100.0 %", safety.pedal, 1000);

    sim_apps_mv(500 + 40 * 50, 250 + 20 * 46);
    run(1);
    check("APPS 50 % and 46 % -> pedal is the average, 48 %", safety.pedal, 480);
}

static void test_fault_codes(void)
{
    rest();

    sim_apps_mv(0, 250);     run(1);
    check("APPS1 unplugged (0 V) -> code 1", APPS1.fault, SENSOR_SHORT_TO_GND);
    check("  APPS2 unaffected", APPS2.fault, SENSOR_OK);

    sim_apps_mv(5000, 250);  run(1);
    check("APPS1 at 5 V -> code 2", APPS1.fault, SENSOR_SHORT_TO_VCC);
    sim_apps_mv(300, 250);   run(1);
    check("APPS1 at 0.30 V -> code 3", APPS1.fault, SENSOR_OUT_OF_RANGE);
    sim_apps_mv(4700, 250);  run(1);
    check("APPS1 at 4.70 V -> code 3", APPS1.fault, SENSOR_OUT_OF_RANGE);
    sim_apps_mv(430, 250);   run(1);
    check("APPS1 at 0.43 V (inside margin) -> OK", APPS1.fault, SENSOR_OK);
    sim_apps_mv(4570, 2250); run(1);
    check("APPS1 at 4.57 V (inside margin) -> OK", APPS1.fault, SENSOR_OK);

    sim_apps_mv(500, 0);     run(1);
    check("APPS2 unplugged -> code 1", APPS2.fault, SENSOR_SHORT_TO_GND);
    sim_apps_mv(500, 5000);  run(1);
    check("APPS2 at 5 V -> code 2", APPS2.fault, SENSOR_SHORT_TO_VCC);
    sim_apps_mv(500, 2500);  run(1);
    check("APPS2 at 2.5 V -> code 3", APPS2.fault, SENSOR_OUT_OF_RANGE);
    sim_apps_mv(500, 150);   run(1);
    check("APPS2 at 0.15 V -> code 3", APPS2.fault, SENSOR_OUT_OF_RANGE);

    sim_apps_mv(500, 250);
    sim_brake_mv(0);         run(1);
    check("brake unplugged -> code 1", BRAKE.fault, SENSOR_SHORT_TO_GND);
    sim_brake_mv(5000);      run(1);
    check("brake at 5 V -> code 2", BRAKE.fault, SENSOR_SHORT_TO_VCC);
    sim_brake_mv(300);       run(1);
    check("brake at 0.3 V -> code 3", BRAKE.fault, SENSOR_OUT_OF_RANGE);

    sim_mv[ADS7038_CH_SPARE1] = 0;
    run(1);
    check("steering unplugged -> code 1", STEER.fault, SENSOR_SHORT_TO_GND);
    sim_mv[ADS7038_CH_SPARE1] = 2500;

    rest();
    check("all healthy again", APPS1.fault | APPS2.fault | BRAKE.fault | STEER.fault, 0);
}

static void test_apps_implausibility(void)
{
    /* One sensor fails high while driving at 60 % */
    sim_pedal_percent(60);
    run(200);
    sim_apps_mv(5000, 250 + 20 * 60);
    run(50);
    check("APPS1 shorts high at 60 %: pedal holds 60 % (lower sensor)", safety.pedal, 600);
    check("  not flagged at 50 ms", safety.apps_implausible, 0);
    run(52);
    check("  flagged after more than 100 ms", safety.apps_implausible, 1);
    check("  pedal forced to 0 %", safety.pedal, 0);
    sim_pedal_percent(60);
    run(1);
    check("  recovers at once: flag clear", safety.apps_implausible, 0);
    check("  recovers at once: pedal 60 %", safety.pedal, 600);

    /* One sensor unplugged */
    sim_apps_mv(0, 250 + 20 * 60);
    run(1);
    check("APPS1 unplugged at 60 %: pedal drops to 0 % straight away", safety.pedal, 0);

    /* Sensors disagree */
    sim_pedal_percent(60);
    run(200);
    sim_apps_mv(500 + 40 * 60, 250 + 20 * 45);
    run(50);
    check("15 % apart: pedal is the lower one, 45 %", safety.pedal, 450);
    run(52);
    check("  then 0 % once flagged", safety.pedal, 0);

    sim_pedal_percent(50);
    run(5);
    sim_apps_mv(500 + 40 * 50, 250 + 20 * 41);
    run(300);
    check("9 % apart for 300 ms -> plausible", safety.apps_implausible, 0);

    /* Short glitches must not add up */
    for (int i = 0; i < 2; i++) {
        sim_apps_mv(500 + 40 * 50, 250 + 20 * 39);
        run(60);
        sim_pedal_percent(50);
        run(5);
    }
    check("two separate 60 ms glitches -> not flagged", safety.apps_implausible, 0);

    /* Signal wires shorted together: both sensors read the same voltage */
    rest();
    sim_apps_mv(450, 450);
    run(102);
    check("shorted together at 0.45 V -> implausible", safety.apps_implausible, 1);
    check("  pedal 0 %", safety.pedal, 0);

    rest();
    sim_apps_mv(1500, 1500);
    run(50);
    check("shorted together at 1.5 V: pedal is the lower reading (25 %)", safety.pedal, 250);
    run(52);
    check("  -> implausible", safety.apps_implausible, 1);
    check("  -> pedal 0 %", safety.pedal, 0);

    rest();
    sim_apps_mv(4000, 4000);
    run(102);
    check("shorted together at 4.0 V -> implausible", safety.apps_implausible, 1);

    rest();
    sim_pedal_percent(100);
    run(300);
    check("healthy at full throttle -> plausible", safety.apps_implausible, 0);
}

static void test_brake_plausibility(void)
{
    rest();

    sim_pedal_percent(30);
    sim_brake_mv(BAR_TO_MV(50));
    run(500);
    check("30 % + brakes for 500 ms -> not yet", safety.brake_plausibility_tripped, 0);
    run(2);
    check("30 % + brakes for more than 500 ms -> tripped", safety.brake_plausibility_tripped, 1);
    check("  pedal still reported (30 %)", safety.pedal, 300);

    sim_brake_mv(BAR_TO_MV(0));
    run(500);
    check("  brakes released, pedal 30 % -> stays tripped", safety.brake_plausibility_tripped, 1);
    sim_pedal_percent(6);
    run(500);
    check("  pedal 6 % -> stays tripped", safety.brake_plausibility_tripped, 1);
    sim_pedal_percent(4);
    sim_brake_mv(BAR_TO_MV(50));
    run(1);
    check("  pedal 4 % with brakes on -> resets", safety.brake_plausibility_tripped, 0);

    sim_pedal_percent(24);
    run(2000);
    check("24 % + brakes -> never trips", safety.brake_plausibility_tripped, 0);

    sim_pedal_percent(30);
    sim_brake_mv(BAR_TO_MV(4));
    run(2000);
    check("30 % + 4 bar (below the 5 bar threshold) -> never trips", safety.brake_plausibility_tripped, 0);

    sim_brake_mv(BAR_TO_MV(6));
    run(502);
    check("30 % + 6 bar -> trips", safety.brake_plausibility_tripped, 1);

    sim_apps_mv(0, 0);
    run(300);
    check("  both APPS unplugged (read 0 %) -> does NOT reset", safety.brake_plausibility_tripped, 1);
    rest();
    check("  healthy and released -> resets", safety.brake_plausibility_tripped, 0);

    /* Short overlaps must not add up */
    for (int i = 0; i < 2; i++) {
        sim_pedal_percent(30);
        sim_brake_mv(BAR_TO_MV(50));
        run(400);
        sim_brake_mv(BAR_TO_MV(0));
        run(5);
    }
    check("two separate 400 ms overlaps -> not tripped", safety.brake_plausibility_tripped, 0);
}

static void test_adc_failure(void)
{
    rest();
    sim_pedal_percent(20);
    run(200);

    int inits_before = sim_adc_inits;
    sim_adc_dead = true;
    run(1);
    check("ADC dead -> every sensor code 4",
          APPS1.fault == SENSOR_NO_DATA && APPS2.fault == SENSOR_NO_DATA &&
          BRAKE.fault == SENSOR_NO_DATA && STEER.fault == SENSOR_NO_DATA, 1);
    check("  pedal 0 %", safety.pedal, 0);
    run(101);
    check("  APPS implausible after 100 ms", safety.apps_implausible, 1);
    run(400);
    check("  set-up retried: once at once, then every 100 ms (6 in 502 ms)",
          sim_adc_inits - inits_before, 6);

    sim_adc_dead = false;
    run(101);
    check("ADC back -> faults clear", APPS1.fault | APPS2.fault | safety.apps_implausible, 0);
    check("  pedal 20 %", safety.pedal, 200);
}

int main(void)
{
    sim_mv[ADS7038_CH_SPARE1] = 2500;   /* steering sensor mid-way */
    sensors_init();
    safety_init(now_ms);

    test_scaling();
    test_fault_codes();
    test_apps_implausibility();
    test_brake_plausibility();
    test_adc_failure();

    return test_summary();
}
