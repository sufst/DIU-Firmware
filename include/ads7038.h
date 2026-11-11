/*
 * TI ADS7038 8-channel 12-bit SPI ADC (U2), on SPI1.
 *
 * All the analog sensors go through this chip, not the microcontroller's own
 * ADC. Board wiring of its inputs:
 *   AIN0  SPARE2_SIG_F
 *   AIN1  APPS1_SIG_F
 *   AIN2  APPS2_SIG_F
 *   AIN3  BPS_SIG_F
 *   AIN4  SPARE1_SIG_F   (steering angle sensor)
 *   AIN5-7 not connected
 */
#ifndef ADS7038_H
#define ADS7038_H

#include <stdbool.h>
#include <stdint.h>

#define ADS7038_CH_SPARE2   0u
#define ADS7038_CH_APPS1    1u
#define ADS7038_CH_APPS2    2u
#define ADS7038_CH_BPS      3u
#define ADS7038_CH_SPARE1   4u

#define ADS7038_NUM_CH      8u

/* Results are always scaled to 16 bits: 0 = 0 V, 65535 = full scale (AVDD). */
#define ADS7038_FULL_SCALE  65536u

/*
 * Reset the ADC and set it up to step through the channels in channel_mask
 * (bit n = AINn), averaging 2^oversampling_log2 conversions per result.
 * Takes about 10 ms. Returns false if the ADC does not respond.
 */
bool ads7038_init(uint8_t channel_mask, uint8_t oversampling_log2);

/*
 * Take one fresh reading of every channel in channel_mask and store it in
 * raw[channel] (16-bit scale). Returns false if the ADC did not deliver all
 * of them: it is not responding, or it has reset and lost its setup.
 */
bool ads7038_read(uint8_t channel_mask, uint16_t raw[ADS7038_NUM_CH]);

#endif
