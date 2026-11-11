#include "mcp9800.h"
#include "config.h"
#include "i2c_bus.h"

#define REG_TEMPERATURE     0x00u
#define REG_CONFIG          0x01u

/* 12-bit resolution: steps of 1/16 degC, one conversion every 240 ms */
#define CONFIG_12BIT        0x60u

static bool s_ok;
static bool s_tried;
static uint32_t s_last_try_ms;

static bool setup(void)
{
    uint8_t config = CONFIG_12BIT;
    return i2c_bus_write(I2C_ADDR_TEMP_SENSOR, REG_CONFIG, I2C_REG_8BIT, &config, 1);
}

bool mcp9800_read(uint32_t now_ms, int16_t *deci_celsius)
{
    uint8_t raw[2];

    /* Not working (or first call): set it up, but only retry now and then */
    if (!s_ok) {
        if (s_tried && (now_ms - s_last_try_ms) < I2C_RETRY_PERIOD_MS) {
            return false;
        }
        s_tried = true;
        s_last_try_ms = now_ms;
        s_ok = setup();
        if (!s_ok) {
            return false;
        }
    }

    if (!i2c_bus_read(I2C_ADDR_TEMP_SENSOR, REG_TEMPERATURE, I2C_REG_8BIT, raw, sizeof(raw))) {
        s_ok = false;
        return false;
    }

    /*
     * The two bytes are a signed number with the temperature in its top
     * 12 bits. Shifting right by 4 leaves sixteenths of a degree.
     */
    int16_t sixteenths = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]) >> 4;
    *deci_celsius = (int16_t)((sixteenths * 10) / 16);
    return true;
}
