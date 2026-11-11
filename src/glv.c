#include "glv.h"
#include "board.h"
#include "config.h"

#define ADC_FULL_SCALE  4095u   /* 12-bit */

static ADC_HandleTypeDef s_adc;
static bool s_running;

bool glv_init(void)
{
    ADC_ChannelConfTypeDef channel = { 0 };

    /*
     * The ADC free-runs on one channel: it converts over and over by itself
     * and each new result replaces the last, so reading the voltage is just
     * picking up the latest value - no waiting.
     *
     * Each result is the average of 16 conversions (hardware oversampling).
     */
    s_adc.Instance = ADC1;
    s_adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_adc.Init.Resolution = ADC_RESOLUTION_12B;
    s_adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    s_adc.Init.LowPowerAutoWait = DISABLE;
    s_adc.Init.LowPowerAutoPowerOff = DISABLE;
    s_adc.Init.ContinuousConvMode = ENABLE;
    s_adc.Init.NbrOfConversion = 1;
    s_adc.Init.DiscontinuousConvMode = DISABLE;
    s_adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_adc.Init.DMAContinuousRequests = DISABLE;
    s_adc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    s_adc.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_160CYCLES_5;    /* long: the divider is high impedance */
    s_adc.Init.SamplingTimeCommon2 = ADC_SAMPLETIME_160CYCLES_5;
    s_adc.Init.OversamplingMode = ENABLE;
    s_adc.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_16;
    s_adc.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;    /* divide the sum of 16 by 16 */
    s_adc.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    s_adc.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;

    /* The pin is set up by HAL_ADC_MspInit() in board.c */
    if (HAL_ADC_Init(&s_adc) != HAL_OK) {
        return false;
    }

    channel.Channel = ADC_CHANNEL_0;                    /* PA0 */
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if (HAL_ADC_ConfigChannel(&s_adc, &channel) != HAL_OK) {
        return false;
    }

    if (HAL_ADCEx_Calibration_Start(&s_adc) != HAL_OK) {
        return false;
    }

    s_running = HAL_ADC_Start(&s_adc) == HAL_OK;
    return s_running;
}

uint16_t glv_read_mv(void)
{
    if (!s_running) {
        return 0;
    }

    /* Voltage at the pin ... */
    uint32_t pin_mv = (HAL_ADC_GetValue(&s_adc) * MCU_VREF_MV) / ADC_FULL_SCALE;

    /* ... scaled back up through the divider to the voltage on the rail */
    uint32_t rail_mv = (pin_mv * (GLV_DIVIDER_TOP_OHMS + GLV_DIVIDER_BOTTOM_OHMS))
                     / GLV_DIVIDER_BOTTOM_OHMS;

    return (uint16_t)rail_mv;
}
