#include "sim.h"
#include <stdio.h>
#include "cal_storage.h"

/* -------------------------------------------------------------------------
 * Fake ADS7038: returns whatever voltages the test has set
 * ------------------------------------------------------------------------- */

uint32_t sim_mv[ADS7038_NUM_CH];
bool sim_adc_dead;
int sim_adc_inits;

bool ads7038_init(uint8_t channel_mask, uint8_t oversampling_log2)
{
    (void)channel_mask;
    (void)oversampling_log2;
    sim_adc_inits++;
    return !sim_adc_dead;
}

bool ads7038_read(uint8_t channel_mask, uint16_t raw[ADS7038_NUM_CH])
{
    (void)channel_mask;
    if (sim_adc_dead) {
        return false;
    }
    for (int i = 0; i < (int)ADS7038_NUM_CH; i++) {
        /* millivolts -> 16-bit ADC code with a 5 V reference, rounded */
        uint32_t code = (uint32_t)(((uint64_t)sim_mv[i] * 65536u + 2500u) / 5000u);
        raw[i] = (code > 65535u) ? 65535u : (uint16_t)code;
    }
    return true;
}

void sim_apps_mv(uint32_t apps1_mv, uint32_t apps2_mv)
{
    sim_mv[ADS7038_CH_APPS1] = apps1_mv;
    sim_mv[ADS7038_CH_APPS2] = apps2_mv;
}

void sim_brake_mv(uint32_t mv)
{
    sim_mv[ADS7038_CH_BPS] = mv;
}

void sim_pedal_percent(int percent)
{
    /* Nominal: APPS1 0.5-4.5 V, APPS2 0.25-2.25 V */
    sim_apps_mv(500u + 40u * (uint32_t)percent, 250u + 20u * (uint32_t)percent);
}

/* -------------------------------------------------------------------------
 * Fake calibration storage: a variable instead of the EEPROM
 * ------------------------------------------------------------------------- */

apps_calibration_t sim_stored_cal;
bool sim_storage_valid;
bool sim_storage_broken;
int sim_storage_saves;

bool cal_storage_load(apps_calibration_t *cal)
{
    if (!sim_storage_valid) {
        return false;
    }
    *cal = sim_stored_cal;
    return true;
}

bool cal_storage_save(const apps_calibration_t *cal)
{
    if (sim_storage_broken) {
        return false;
    }
    sim_stored_cal = *cal;
    sim_storage_valid = true;
    sim_storage_saves++;
    return true;
}

/* -------------------------------------------------------------------------
 * Checking
 * ------------------------------------------------------------------------- */

static int s_failures;
static int s_checks;

void check_near(const char *what, int got, int want, int tolerance)
{
    int difference = (got > want) ? (got - want) : (want - got);
    bool ok = difference <= tolerance;

    s_checks++;
    if (!ok) {
        s_failures++;
    }
    printf("%-4s %-64s got %d, want %d\n", ok ? "ok" : "FAIL", what, got, want);
}

void check(const char *what, int got, int want)
{
    check_near(what, got, want, 0);
}

int test_summary(void)
{
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures;
}
