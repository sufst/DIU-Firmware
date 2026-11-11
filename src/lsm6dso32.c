#include "lsm6dso32.h"
#include "config.h"
#include "i2c_bus.h"

/* Registers */
#define REG_WHO_AM_I        0x0Fu
#define REG_CTRL1_XL        0x10u   /* accelerometer rate and range */
#define REG_CTRL2_G         0x11u   /* gyroscope rate and range     */
#define REG_CTRL3_C         0x12u   /* general control              */
#define REG_OUTX_L_G        0x22u   /* first of 12 data bytes: gyro X/Y/Z then accel X/Y/Z */

#define WHO_AM_I_VALUE      0x6Cu   /* fixed ID of this chip */

/* 104 measurements per second, +/-8 g */
#define CTRL1_XL_104HZ_8G       0x48u
/* 104 measurements per second, +/-500 deg/s */
#define CTRL2_G_104HZ_500DPS    0x44u
/* Keep each 2-byte value consistent while it is being read, and step
 * through the registers automatically in a multi-byte read */
#define CTRL3_C_BDU_AUTOINC     0x44u

/*
 * Size of one bit at the ranges chosen above, from the datasheet:
 *   accelerometer +/-8 g:      0.244 mg per bit
 *   gyroscope +/-500 deg/s:    17.5 mdps per bit = 0.175 of our 0.1 deg/s unit
 */
#define ACCEL_UG_PER_BIT        244     /* micro-g */
#define GYRO_MILLI_DDPS_PER_BIT 175

static bool s_ok;
static bool s_tried;
static uint32_t s_last_try_ms;

static bool write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write(I2C_ADDR_IMU, reg, I2C_REG_8BIT, &value, 1);
}

static bool setup(void)
{
    uint8_t id = 0;

    /* Check it really is the chip we expect before configuring it */
    if (!i2c_bus_read(I2C_ADDR_IMU, REG_WHO_AM_I, I2C_REG_8BIT, &id, 1) || id != WHO_AM_I_VALUE) {
        return false;
    }

    return write_reg(REG_CTRL3_C, CTRL3_C_BDU_AUTOINC)
        && write_reg(REG_CTRL1_XL, CTRL1_XL_104HZ_8G)
        && write_reg(REG_CTRL2_G, CTRL2_G_104HZ_500DPS);
}

/* Two bytes, low byte first, into a signed number */
static int32_t raw16(const uint8_t *bytes)
{
    return (int16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

bool lsm6dso32_read(uint32_t now_ms, imu_data_t *data)
{
    uint8_t raw[12];

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

    if (!i2c_bus_read(I2C_ADDR_IMU, REG_OUTX_L_G, I2C_REG_8BIT, raw, sizeof(raw))) {
        s_ok = false;
        return false;
    }

    for (uint32_t axis = 0; axis < 3u; axis++) {
        data->gyro_ddps[axis] = (int16_t)((raw16(&raw[2u * axis]) * GYRO_MILLI_DDPS_PER_BIT) / 1000);
        data->accel_mg[axis] = (int16_t)((raw16(&raw[6u + 2u * axis]) * ACCEL_UG_PER_BIT) / 1000);
    }
    return true;
}
