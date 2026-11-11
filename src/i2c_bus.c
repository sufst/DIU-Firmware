#include "i2c_bus.h"
#include "board.h"

/*
 * Bit timing for 400 kHz from a 25 MHz clock (24 MHz on the fallback
 * oscillator is close enough). The fields are, from the top:
 *   prescaler 1      -> one time unit = 80 ns
 *   data setup  4    -> 400 ns
 *   data hold   1    ->  80 ns
 *   clock high  9    -> 800 ns
 *   clock low  17    -> 1440 ns
 */
#define I2C_TIMING_400KHZ   0x10410911u

/* Longest any single transfer may take before it is abandoned */
#define I2C_TIMEOUT_MS      3u

static I2C_HandleTypeDef s_i2c;
static bool s_ready;

bool i2c_bus_init(void)
{
    s_i2c.Instance = I2C2;
    s_i2c.Init.Timing = I2C_TIMING_400KHZ;
    s_i2c.Init.OwnAddress1 = 0;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.OwnAddress2 = 0;
    s_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    /* Pins and clocks are set up by HAL_I2C_MspInit() in board.c */
    s_ready = HAL_I2C_Init(&s_i2c) == HAL_OK;
    return s_ready;
}

/* The HAL wants the address shifted up one bit, and its own constant for the size */
static uint16_t hal_address(uint8_t address)
{
    return (uint16_t)(address << 1);
}

static uint16_t hal_reg_size(i2c_reg_size_t reg_size)
{
    return (reg_size == I2C_REG_16BIT) ? I2C_MEMADD_SIZE_16BIT : I2C_MEMADD_SIZE_8BIT;
}

bool i2c_bus_read(uint8_t address, uint16_t reg, i2c_reg_size_t reg_size,
                  uint8_t *data, uint16_t len)
{
    return s_ready && HAL_I2C_Mem_Read(&s_i2c, hal_address(address), reg, hal_reg_size(reg_size),
                                       data, len, I2C_TIMEOUT_MS) == HAL_OK;
}

bool i2c_bus_write(uint8_t address, uint16_t reg, i2c_reg_size_t reg_size,
                   const uint8_t *data, uint16_t len)
{
    /* The HAL takes a non-const pointer but does not change the data */
    return s_ready && HAL_I2C_Mem_Write(&s_i2c, hal_address(address), reg, hal_reg_size(reg_size),
                                        (uint8_t *)data, len, I2C_TIMEOUT_MS) == HAL_OK;
}

bool i2c_bus_device_ready(uint8_t address, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    if (!s_ready) {
        return false;
    }

    /* Keep asking until it answers or the time is up */
    do {
        if (HAL_I2C_IsDeviceReady(&s_i2c, hal_address(address), 1, I2C_TIMEOUT_MS) == HAL_OK) {
            return true;
        }
    } while ((HAL_GetTick() - start) < timeout_ms);

    return false;
}
