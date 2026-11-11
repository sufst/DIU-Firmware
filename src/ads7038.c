/*
 * ADS7038 driver.
 *
 * How the chip is used:
 *
 *  - "Auto-sequence" mode. Every time chip select (CS) goes high the ADC
 *    converts the next enabled channel, and the result is clocked out in the
 *    following SPI frame. So reading N frames in a row walks through N
 *    channels with no need to keep telling it which channel we want.
 *
 *  - Oversampling. On each CS rising edge the ADC takes several conversions
 *    back to back with its own internal clock and returns the average as a
 *    16-bit number. This lowers the noise; the only cost is that we must wait
 *    for the conversions to finish before starting the next frame.
 *
 *  - Channel ID. The ADC appends the 4-bit channel number to every result,
 *    so we never have to assume which channel a result belongs to. This also
 *    tells us straight away if the ADC has reset and lost its setup.
 *
 * Every SPI frame is 24 bits:
 *    with oversampling:     [16-bit result][4-bit channel][4 unused bits]
 *    without oversampling:  [12-bit result][4-bit channel][8 unused bits]
 */
#include "ads7038.h"
#include "board.h"

/* SPI command bytes */
#define OP_NOP              0x00u
#define OP_REG_READ         0x10u
#define OP_REG_WRITE        0x08u

/* Register addresses */
#define REG_SYSTEM_STATUS   0x00u
#define REG_GENERAL_CFG     0x01u
#define REG_DATA_CFG        0x02u
#define REG_OSR_CFG         0x03u
#define REG_PIN_CFG         0x05u
#define REG_SEQUENCE_CFG    0x10u
#define REG_AUTO_SEQ_CH_SEL 0x12u

/* Register values */
#define SYSTEM_STATUS_BOR   0x01u   /* "has been reset" flag, write 1 to clear   */
#define GENERAL_CFG_RST     0x01u   /* software reset                           */
#define GENERAL_CFG_CAL     0x02u   /* offset self-calibration (clears itself)  */
#define DATA_CFG_APPEND_ID  0x10u   /* append the channel number to each result */
#define SEQUENCE_CFG_AUTO   0x11u   /* auto-sequence mode + start sequencing    */

#define RESET_TIME_MS       5u      /* datasheet power-up time                  */
#define SPI_TIMEOUT_MS      2u

static SPI_HandleTypeDef s_spi;
static bool s_spi_ready;
static uint8_t s_oversampling_log2;
static uint32_t s_conversion_time_us;

/* -------------------------------------------------------------------------
 * SPI helpers
 * ------------------------------------------------------------------------- */

static bool spi_init(void)
{
    s_spi.Instance = SPI1;
    s_spi.Init.Mode = SPI_MODE_MASTER;
    s_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity = SPI_POLARITY_LOW;          /* SPI mode 0 */
    s_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_spi.Init.NSS = SPI_NSS_SOFT;                      /* we drive CS ourselves */
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;   /* 25 MHz / 8 = 3.1 MHz */
    s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_spi.Init.CRCPolynomial = 7;
    s_spi.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    s_spi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;

    /* Pins and clocks are set up by HAL_SPI_MspInit() in board.c */
    return HAL_SPI_Init(&s_spi) == HAL_OK;
}

/*
 * Send one 24-bit frame and return the 24 bits received.
 *
 * Raising CS at the end starts the next conversion(s), so we then wait for
 * them to finish. That way the result is always ready by the next frame.
 */
static uint32_t spi_frame(uint8_t b0, uint8_t b1, uint8_t b2)
{
    uint8_t tx[3] = { b0, b1, b2 };
    uint8_t rx[3] = { 0, 0, 0 };

    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&s_spi, tx, rx, sizeof(tx), SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_SET);

    delay_us(s_conversion_time_us);

    return ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
}

static void reg_write(uint8_t reg, uint8_t value)
{
    spi_frame(OP_REG_WRITE, reg, value);
}

/* A register read takes two frames: ask, then collect the answer. */
static uint8_t reg_read(uint8_t reg)
{
    spi_frame(OP_REG_READ, reg, 0);
    return (uint8_t)(spi_frame(OP_NOP, 0, 0) >> 16);
}

/* -------------------------------------------------------------------------
 * Public functions
 * ------------------------------------------------------------------------- */

bool ads7038_init(uint8_t channel_mask, uint8_t oversampling_log2)
{
    if (!s_spi_ready) {
        s_spi_ready = spi_init();
        if (!s_spi_ready) {
            return false;
        }
    }

    s_oversampling_log2 = oversampling_log2 & 0x07u;

    /*
     * Each conversion takes 1 us on the ADC's internal clock, which can run
     * up to 6 % slow. Allow 12.5 % plus a couple of microseconds of margin.
     */
    uint32_t samples = 1u << s_oversampling_log2;
    s_conversion_time_us = samples + samples / 8u + 2u;

    /* Start from a known state */
    reg_write(REG_GENERAL_CFG, GENERAL_CFG_RST);
    HAL_Delay(RESET_TIME_MS);
    reg_write(REG_SYSTEM_STATUS, SYSTEM_STATUS_BOR);
    reg_write(REG_GENERAL_CFG, GENERAL_CFG_CAL);
    HAL_Delay(2);

    reg_write(REG_PIN_CFG, 0x00);                       /* all pins are analog inputs */
    reg_write(REG_OSR_CFG, s_oversampling_log2);
    reg_write(REG_DATA_CFG, DATA_CFG_APPEND_ID);
    reg_write(REG_AUTO_SEQ_CH_SEL, channel_mask);

    /* Read one setting back to prove the ADC is really there and listening */
    if (reg_read(REG_AUTO_SEQ_CH_SEL) != channel_mask) {
        return false;
    }

    reg_write(REG_SEQUENCE_CFG, SEQUENCE_CFG_AUTO);
    return true;
}

bool ads7038_read(uint8_t channel_mask, uint16_t raw[ADS7038_NUM_CH])
{
    uint8_t seen = 0;

    /*
     * The result waiting in the ADC was converted at the end of the previous
     * call, so it is a whole sample period old. Throw it away; doing so also
     * starts a fresh conversion.
     */
    (void)spi_frame(OP_NOP, 0, 0);

    /*
     * Now read until every wanted channel has turned up. Normally that is
     * exactly one frame per channel; two spare frames are allowed.
     */
    uint32_t max_frames = (uint32_t)__builtin_popcount(channel_mask) + 2u;

    for (uint32_t i = 0; i < max_frames && seen != channel_mask; i++) {
        uint32_t frame = spi_frame(OP_NOP, 0, 0);
        uint16_t value;
        uint8_t channel;

        if (s_oversampling_log2 != 0u) {
            value = (uint16_t)(frame >> 8);                 /* 16-bit average */
            channel = (uint8_t)((frame >> 4) & 0x0Fu);
        } else {
            value = (uint16_t)((frame >> 12) << 4);         /* 12-bit, scaled up to 16 */
            channel = (uint8_t)((frame >> 8) & 0x0Fu);
        }

        if (channel < ADS7038_NUM_CH && (channel_mask & (1u << channel)) != 0u) {
            raw[channel] = value;
            seen |= (uint8_t)(1u << channel);
        }
    }

    return seen == channel_mask;
}
