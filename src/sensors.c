#include "sensors.h"
#include "ads7038.h"
#include "config.h"

typedef struct {
    uint8_t  adc_channel;   /* which ADS7038 input the sensor is wired to */

    /* Scaling: a straight line between two points, clamped outside them */
    uint16_t mv_min;        /* sensor voltage (mV) that means out_min. These two are */
    uint16_t mv_max;        /* sensor voltage (mV) that means out_max. the defaults: */
                            /* the APPS pair is replaced by the pedal calibration.   */
    int16_t  out_min;       /* output in 0.1 units */
    int16_t  out_max;

    /* Fault detection (see the diagram below) */
    uint16_t range_margin_mv;   /* how far outside mv_min..mv_max is still accepted */
    uint16_t short_gnd_mv;      /* below this = shorted to ground or disconnected   */
    uint16_t short_vcc_mv;      /* above this = shorted to the supply               */
} sensor_cfg_t;

/*
 * The sensor table.
 *
 * Outputs are x10: 1000 = 100.0 % and 2000 = 200.0 bar.
 *
 * How a voltage is classified, using APPS1 as the example:
 *
 *   0 V      0.1 V       0.4 V  0.5 V          4.5 V  4.6 V       4.75 V    5 V
 *    |---------|-----------|------|=== normal ===|------|-----------|---------|
 *    SHORT_TO_   OUT_OF_    margin                margin  OUT_OF_    SHORT_TO_
 *    GND         RANGE      (OK)                  (OK)    RANGE      VCC
 *
 * Every input has a 100k pull-down on the board, so a disconnected sensor
 * reads about 0 V and is reported as SHORT_TO_GND.
 *
 * The margin allows for sensor tolerance and noise at the ends of travel, so
 * a pedal resting at exactly 0 % does not flicker in and out of OUT_OF_RANGE.
 */
static const sensor_cfg_t s_sensors[SENSOR_COUNT] = {
    /*                                                                       range   short   short
     *                    ADC input          mV min  mV max  out min  out max  margin  to GND  to VCC */
    [SENSOR_APPS1]    = { ADS7038_CH_APPS1,     500,   4500,       0,    1000,    100,    100,   4750 },  /* 0-100 %   */
    [SENSOR_APPS2]    = { ADS7038_CH_APPS2,     250,   2250,       0,    1000,     50,    100,   4750 },  /* 0-100 %   */
    [SENSOR_BRAKE]    = { ADS7038_CH_BPS,       500,   4500,       0,    2000,    100,    100,   4750 },  /* 0-200 bar */
    /* Steering angle sensor on SPARE1. The 0.5-4.5 V span is an assumption - set it to suit the sensor. */
    [SENSOR_STEERING] = { ADS7038_CH_SPARE1,    500,   4500,       0,    1000,    100,    100,   4750 },  /* 0-100 %   */
    /* [SENSOR_SPARE2] = { ADS7038_CH_SPARE2,   500,   4500,       0,    1000,    100,    100,   4750 }, */
};

/*
 * The scaling range actually in use. It starts as mv_min/mv_max from the
 * table and can be replaced by the pedal calibration.
 */
typedef struct {
    uint16_t mv_min;
    uint16_t mv_max;
} sensor_range_t;

static sensor_range_t s_range[SENSOR_COUNT];

static bool s_adc_ok;
static bool s_adc_tried;
static uint32_t s_last_init_ms;

/* One bit per ADC input that has a sensor on it */
static uint8_t channel_mask(void)
{
    uint8_t mask = 0;
    for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
        mask |= (uint8_t)(1u << s_sensors[i].adc_channel);
    }
    return mask;
}

void sensors_init(void)
{
    for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
        s_range[i].mv_min = s_sensors[i].mv_min;
        s_range[i].mv_max = s_sensors[i].mv_max;
    }
}

bool sensors_range_valid(sensor_id_t id, uint16_t mv_min, uint16_t mv_max)
{
    const sensor_cfg_t *cfg = &s_sensors[id];
    int32_t lowest = (int32_t)cfg->mv_min - cfg->range_margin_mv;
    int32_t highest = (int32_t)cfg->mv_max + cfg->range_margin_mv;
    int32_t smallest_span = ((int32_t)cfg->mv_max - cfg->mv_min) / 4;

    return mv_min >= lowest && mv_max <= highest
        && (int32_t)mv_max - (int32_t)mv_min >= smallest_span;
}

bool sensors_set_range(sensor_id_t id, uint16_t mv_min, uint16_t mv_max)
{
    if (!sensors_range_valid(id, mv_min, mv_max)) {
        return false;
    }
    s_range[id].mv_min = mv_min;
    s_range[id].mv_max = mv_max;
    return true;
}

/* Voltage to scaled output, e.g. 2500 mV -> 500 (50.0 %) for an uncalibrated APPS1 */
static int16_t scale(const sensor_cfg_t *cfg, const sensor_range_t *range, int32_t mv)
{
    if (mv <= range->mv_min) {
        return cfg->out_min;
    }
    if (mv >= range->mv_max) {
        return cfg->out_max;
    }

    int32_t span_mv = (int32_t)range->mv_max - range->mv_min;
    int32_t span_out = (int32_t)cfg->out_max - cfg->out_min;

    /* Adding span_mv / 2 before dividing rounds to the nearest step */
    return (int16_t)(cfg->out_min + ((mv - range->mv_min) * span_out + span_mv / 2) / span_mv);
}

/* Decide which fault, if any, a voltage represents. Dead shorts are checked first. */
static sensor_fault_t classify(const sensor_cfg_t *cfg, int32_t mv)
{
    if (mv < cfg->short_gnd_mv) {
        return SENSOR_SHORT_TO_GND;
    }
    if (mv > cfg->short_vcc_mv) {
        return SENSOR_SHORT_TO_VCC;
    }
    if (mv < (int32_t)cfg->mv_min - cfg->range_margin_mv ||
        mv > (int32_t)cfg->mv_max + cfg->range_margin_mv) {
        return SENSOR_OUT_OF_RANGE;
    }
    return SENSOR_OK;
}

/* Returns true once the ADC is set up and answering */
static bool adc_ready(uint32_t now_ms)
{
    if (s_adc_ok) {
        return true;
    }

    /* Setting up takes ~10 ms, so after a failure only retry now and then */
    if (s_adc_tried && (now_ms - s_last_init_ms) < ADC_RETRY_PERIOD_MS) {
        return false;
    }

    s_adc_tried = true;
    s_last_init_ms = now_ms;
    s_adc_ok = ads7038_init(channel_mask(), ADC_OVERSAMPLING_LOG2);
    return s_adc_ok;
}

void sensors_read(uint32_t now_ms, sensor_reading_t readings[SENSOR_COUNT])
{
    uint16_t raw[ADS7038_NUM_CH];

    if (adc_ready(now_ms) && !ads7038_read(channel_mask(), raw)) {
        s_adc_ok = false;       /* stopped answering: set it up again later */
    }

    for (uint32_t i = 0; i < SENSOR_COUNT; i++) {
        const sensor_cfg_t *cfg = &s_sensors[i];
        sensor_reading_t *r = &readings[i];

        if (!s_adc_ok) {
            /* No ADC means we know nothing about any sensor */
            r->millivolts = 0;
            r->value = 0;
            r->fault = SENSOR_NO_DATA;
            continue;
        }

        int32_t mv = (int32_t)(((uint32_t)raw[cfg->adc_channel] * ADC_VREF_MV) / ADS7038_FULL_SCALE);

        r->millivolts = (uint16_t)mv;
        r->value = scale(cfg, &s_range[i], mv);
        r->fault = classify(cfg, mv);
    }
}
