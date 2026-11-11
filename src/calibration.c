#include "calibration.h"
#include "cal_storage.h"
#include "config.h"

typedef enum {
    STATE_NORMAL,       /* not calibrating: LED is the health light         */
    STATE_WAIT_ZERO,    /* waiting for a press with the pedal released      */
    STATE_WAIT_FULL,    /* waiting for a press with the pedal fully pressed */
    STATE_FAILED        /* calibration rejected: flicker the LED for a bit  */
} state_t;

static state_t s_state;
static uint32_t s_state_since_ms;       /* when the current state was entered */
static apps_calibration_t s_pending;    /* values recorded so far this run    */
static bool s_calibrated;               /* a saved calibration is in use      */

/* Button debouncing */
static bool s_button_raw;               /* last raw reading                        */
static uint32_t s_button_change_ms;     /* when the raw reading last changed       */
static bool s_button_down;              /* debounced state                         */
static uint32_t s_button_down_since_ms; /* when the debounced press started        */
static bool s_button_armed;             /* seen released since start-up            */
static bool s_hold_used;                /* this press has already counted as a hold */

/* -------------------------------------------------------------------------
 * Applying a calibration to the sensor scaling
 * ------------------------------------------------------------------------- */

/*
 * Pull both ends of a recorded range inwards by the dead zone, so the pedal
 * reliably reads 0 % when released and 100 % when fully pressed.
 */
static void with_dead_zone(uint16_t zero_mv, uint16_t full_mv, uint16_t *mv_min, uint16_t *mv_max)
{
    int32_t span = (int32_t)full_mv - (int32_t)zero_mv;
    int32_t dead = (span * APPS_CAL_DEADZONE) / 1000;

    *mv_min = (uint16_t)(zero_mv + dead);
    *mv_max = (uint16_t)(full_mv - dead);
}

/* Use a calibration for scaling. Returns false, changing nothing, if it is not sensible. */
static bool apply(const apps_calibration_t *cal)
{
    uint16_t min1, max1, min2, max2;

    /* 100 % must be a higher voltage than 0 % on both sensors */
    if (cal->apps1_full_mv <= cal->apps1_zero_mv || cal->apps2_full_mv <= cal->apps2_zero_mv) {
        return false;
    }

    with_dead_zone(cal->apps1_zero_mv, cal->apps1_full_mv, &min1, &max1);
    with_dead_zone(cal->apps2_zero_mv, cal->apps2_full_mv, &min2, &max2);

    /* Check both before changing either, so they can never be half updated */
    if (!sensors_range_valid(SENSOR_APPS1, min1, max1) ||
        !sensors_range_valid(SENSOR_APPS2, min2, max2)) {
        return false;
    }

    sensors_set_range(SENSOR_APPS1, min1, max1);
    sensors_set_range(SENSOR_APPS2, min2, max2);
    return true;
}

/* -------------------------------------------------------------------------
 * Button
 * ------------------------------------------------------------------------- */

typedef struct {
    bool pressed;   /* the button has just gone down                      */
    bool held;      /* the button has just been down for CAL_HOLD_TIME_MS */
} button_events_t;

static button_events_t update_button(uint32_t now_ms, bool raw)
{
    button_events_t events = { false, false };

    if (raw != s_button_raw) {
        /* Still bouncing: restart the settling timer */
        s_button_raw = raw;
        s_button_change_ms = now_ms;
    } else if ((now_ms - s_button_change_ms) >= BUTTON_DEBOUNCE_MS) {
        /* The reading has been steady long enough to believe */
        if (!raw) {
            /*
             * Presses only count once the button has been seen released. A
             * button that is stuck down (or wired the wrong way round) at
             * power-up therefore can never start a calibration.
             */
            s_button_armed = true;
            s_button_down = false;
        } else if (!s_button_down && s_button_armed) {
            s_button_down = true;
            s_button_down_since_ms = now_ms;
            s_hold_used = false;
            events.pressed = true;
        }
    }

    if (s_button_down && !s_hold_used && (now_ms - s_button_down_since_ms) >= CAL_HOLD_TIME_MS) {
        s_hold_used = true;
        events.held = true;
    }

    return events;
}

/* -------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */

static void enter(state_t state, uint32_t now_ms)
{
    s_state = state;
    s_state_since_ms = now_ms;
}

/* true for the first half of every period: a 50 % blink */
static bool blink(uint32_t now_ms, uint32_t period_ms)
{
    return ((now_ms - s_state_since_ms) % period_ms) < (period_ms / 2u);
}

void calibration_init(uint32_t now_ms)
{
    apps_calibration_t saved;

    s_button_raw = false;
    s_button_change_ms = now_ms;
    s_button_down = false;
    s_button_armed = false;
    s_hold_used = false;
    enter(STATE_NORMAL, now_ms);

    /* If nothing valid is saved, the defaults from the sensor table stay in use */
    s_calibrated = cal_storage_load(&saved) && apply(&saved);
}

calibration_status_t calibration_update(uint32_t now_ms, bool button_down,
                                        const sensor_reading_t readings[SENSOR_COUNT],
                                        bool error)
{
    calibration_status_t status = { false, false, false };
    button_events_t button = update_button(now_ms, button_down);

    const sensor_reading_t *apps1 = &readings[SENSOR_APPS1];
    const sensor_reading_t *apps2 = &readings[SENSOR_APPS2];
    bool sensors_ok = apps1->fault == SENSOR_OK && apps2->fault == SENSOR_OK;

    switch (s_state) {
    case STATE_NORMAL:
        status.led_on = !error;
        if (button.held) {
            enter(STATE_WAIT_ZERO, now_ms);
        }
        break;

    case STATE_WAIT_ZERO:
        status.active = true;
        status.led_on = blink(now_ms, CAL_LED_ZERO_PERIOD_MS);

        if (button.pressed) {
            if (sensors_ok) {
                s_pending.apps1_zero_mv = apps1->millivolts;
                s_pending.apps2_zero_mv = apps2->millivolts;
                enter(STATE_WAIT_FULL, now_ms);
            } else {
                enter(STATE_FAILED, now_ms);
            }
        } else if ((now_ms - s_state_since_ms) >= CAL_TIMEOUT_MS) {
            enter(STATE_FAILED, now_ms);
        }
        break;

    case STATE_WAIT_FULL:
        status.active = true;
        status.led_on = blink(now_ms, CAL_LED_FULL_PERIOD_MS);

        if (button.pressed) {
            s_pending.apps1_full_mv = apps1->millivolts;
            s_pending.apps2_full_mv = apps2->millivolts;

            /*
             * Only keep it if the sensors are healthy, the values make sense
             * and they were saved. Otherwise the old calibration stays.
             * (Saving to the EEPROM blocks for about 10 ms - acceptable
             * here, the car is stationary.)
             */
            if (sensors_ok && apply(&s_pending) && cal_storage_save(&s_pending)) {
                s_calibrated = true;
                enter(STATE_NORMAL, now_ms);
            } else {
                /* Put back what was in use before: the saved calibration, or
                 * the defaults from the sensor table if there is none. */
                apps_calibration_t saved;
                sensors_init();
                s_calibrated = cal_storage_load(&saved) && apply(&saved);
                enter(STATE_FAILED, now_ms);
            }
        } else if ((now_ms - s_state_since_ms) >= CAL_TIMEOUT_MS) {
            enter(STATE_FAILED, now_ms);
        }
        break;

    case STATE_FAILED:
    default:
        status.led_on = blink(now_ms, CAL_LED_FAIL_PERIOD_MS);
        if ((now_ms - s_state_since_ms) >= CAL_FAIL_INDICATION_MS) {
            enter(STATE_NORMAL, now_ms);
        }
        break;
    }

    status.calibrated = s_calibrated;
    return status;
}
