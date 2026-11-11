/*
 * LSM6DSO32 inertial measurement unit (U9) on I2C: 3-axis accelerometer and
 * 3-axis gyroscope.
 *
 * Axes are the chip's own X/Y/Z as marked in its datasheet. Nothing here
 * rotates them to match how the board is mounted in the car.
 */
#ifndef LSM6DSO32_H
#define LSM6DSO32_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t accel_mg[3];        /* X, Y, Z acceleration, 1 mg per bit (1000 = 1 g), range +/-8 g */
    int16_t gyro_ddps[3];       /* X, Y, Z rotation rate, 0.1 deg/s per bit, range +/-500 deg/s  */
} imu_data_t;

/*
 * Read the latest measurement. Returns false if the IMU is not answering; it
 * is then set up again every I2C_RETRY_PERIOD_MS. The first call sets it up.
 */
bool lsm6dso32_read(uint32_t now_ms, imu_data_t *data);

#endif
