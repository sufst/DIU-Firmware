/*
 * Simulated hardware and small helpers shared by the PC tests.
 *
 * sim.c provides fake versions of the ADS7038 driver and the calibration
 * storage, so the real sensors.c / safety.c / calibration.c can be run on a
 * PC with any sensor voltages we like and with time stepped 1 ms at a time.
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "ads7038.h"
#include "calibration.h"

/* ---- Fake ADC ---- */
extern uint32_t sim_mv[ADS7038_NUM_CH];     /* voltage on each ADC input, mV   */
extern bool sim_adc_dead;                   /* true = ADC does not answer      */
extern int sim_adc_inits;                   /* how many times it was set up    */

/* Set both accelerator sensor voltages / the brake sensor voltage */
void sim_apps_mv(uint32_t apps1_mv, uint32_t apps2_mv);
void sim_brake_mv(uint32_t mv);

/* Set both accelerator sensors to a pedal position, using the nominal ranges */
void sim_pedal_percent(int percent);

/* Brake sensor voltage for a pressure in bar */
#define BAR_TO_MV(bar) (500u + 20u * (bar))

/* ---- Fake calibration storage ---- */
extern apps_calibration_t sim_stored_cal;
extern bool sim_storage_valid;              /* something has been saved        */
extern bool sim_storage_broken;             /* true = saving fails             */
extern int sim_storage_saves;               /* how many successful saves       */

/* ---- Checking ---- */

/* Compare a result with what is expected, print a line, count failures */
void check(const char *what, int got, int want);

/* As check(), but allows got to differ from want by up to tolerance */
void check_near(const char *what, int got, int want, int tolerance);

/* Print the verdict; returns the number of failures (use as main's return) */
int test_summary(void);

#endif
