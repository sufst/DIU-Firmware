#include "safety.h"
#include "config.h"

/* Last moment the two APPS were seen agreeing with each other */
static uint32_t s_apps_last_ok_ms;
static bool s_apps_implausible;

/* Last moment the "brakes on + pedal past 25 %" condition was NOT true */
static uint32_t s_bpp_last_clear_ms;
static bool s_bpp_tripped;

static int32_t abs_difference(int32_t a, int32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

void safety_init(uint32_t now_ms)
{
    s_apps_last_ok_ms = now_ms;
    s_apps_implausible = false;
    s_bpp_last_clear_ms = now_ms;
    s_bpp_tripped = false;
}

/* -------------------------------------------------------------------------
 * 1 + 2. APPS1 against APPS2, and the single pedal position
 * ------------------------------------------------------------------------- */
static int16_t update_pedal(uint32_t now_ms, const sensor_reading_t *apps1,
                            const sensor_reading_t *apps2)
{
    bool disagree = abs_difference(apps1->value, apps2->value) > APPS_MAX_DIFFERENCE;

    /*
     * APPS2 works at half the voltage of APPS1, so in normal use the two
     * signals are never at the same voltage. If they are, the wires are
     * shorted together.
     */
    bool shorted = abs_difference(apps1->millivolts, apps2->millivolts) < APPS_SHORT_THRESHOLD_MV;

    bool problem = disagree || shorted
                || apps1->fault != SENSOR_OK || apps2->fault != SENSOR_OK;

    if (!problem) {
        /* Healthy: report the average of the two sensors */
        s_apps_last_ok_ms = now_ms;
        s_apps_implausible = false;
        return (int16_t)((apps1->value + apps2->value) / 2);
    }

    if ((now_ms - s_apps_last_ok_ms) > APPS_IMPLAUSIBLE_TIME_MS) {
        s_apps_implausible = true;
    }

    if (s_apps_implausible) {
        /* The pedal position is unknown, so report "not pressed" */
        return 0;
    }

    /*
     * Something is wrong but it may only be a glitch (under 100 ms). Until we
     * know, believe the lower sensor so a fault can never add throttle.
     */
    return (apps1->value < apps2->value) ? apps1->value : apps2->value;
}

/* -------------------------------------------------------------------------
 * 3. Accelerator against brake (EV6.3)
 * ------------------------------------------------------------------------- */
static void update_brake_plausibility(uint32_t now_ms, const sensor_reading_t *apps1,
                                      const sensor_reading_t *apps2,
                                      const sensor_reading_t *brake)
{
    /* Use whichever pedal sensor reads higher: the cautious choice both for
     * tripping (trips sooner) and for resetting (resets later). */
    int16_t apps = (apps1->value > apps2->value) ? apps1->value : apps2->value;
    bool apps_healthy = apps1->fault == SENSOR_OK && apps2->fault == SENSOR_OK;

    if (s_bpp_tripped) {
        /*
         * EV6.3.12: stay tripped until the pedal is below 5 %, whether or not
         * the brakes are still on. A disconnected sensor reads 0 %, which must
         * not be mistaken for the driver lifting off, so both must be healthy.
         */
        if (apps < BPP_APPS_RESET_THRESHOLD && apps_healthy) {
            s_bpp_tripped = false;
            s_bpp_last_clear_ms = now_ms;
        }
        return;
    }

    /* EV6.3.11: brakes actuated and pedal past 25 % at the same time ... */
    bool brakes_on = brake->fault == SENSOR_OK && brake->value > BRAKE_ACTUATED_THRESHOLD;
    bool both_pressed = brakes_on && apps > BPP_APPS_TRIP_THRESHOLD;

    if (!both_pressed) {
        s_bpp_last_clear_ms = now_ms;
    } else if ((now_ms - s_bpp_last_clear_ms) > BPP_TRIP_TIME_MS) {
        /* ... for longer than 500 ms */
        s_bpp_tripped = true;
    }
}

void safety_update(uint32_t now_ms, const sensor_reading_t readings[SENSOR_COUNT],
                   safety_result_t *result)
{
    const sensor_reading_t *apps1 = &readings[SENSOR_APPS1];
    const sensor_reading_t *apps2 = &readings[SENSOR_APPS2];
    const sensor_reading_t *brake = &readings[SENSOR_BRAKE];

    result->pedal = update_pedal(now_ms, apps1, apps2);
    update_brake_plausibility(now_ms, apps1, apps2, brake);

    result->apps_implausible = s_apps_implausible;
    result->brake_plausibility_tripped = s_bpp_tripped;
}
