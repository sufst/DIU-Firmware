#include "cal_storage.h"
#include "eeprom.h"

/* Where the record lives in the EEPROM */
#define CAL_EEPROM_ADDRESS  0u

/* Marks the record as a calibration ("DIU" + layout version 1) */
#define CAL_MAGIC           0x44495501u

/* What is stored: 16 bytes */
typedef struct {
    uint32_t magic;
    apps_calibration_t cal;     /* 4 x uint16_t */
    uint32_t check;             /* catches a corrupt or half-written record */
} cal_record_t;

_Static_assert(sizeof(cal_record_t) == 16, "calibration record should be 16 bytes");

/* Simple check value over the stored numbers; a blank EEPROM (all 1s) never matches */
static uint32_t check_value(const apps_calibration_t *cal)
{
    uint32_t sum = CAL_MAGIC;
    sum = sum * 31u + cal->apps1_zero_mv;
    sum = sum * 31u + cal->apps1_full_mv;
    sum = sum * 31u + cal->apps2_zero_mv;
    sum = sum * 31u + cal->apps2_full_mv;
    return ~sum;
}

bool cal_storage_load(apps_calibration_t *cal)
{
    cal_record_t record;

    if (!eeprom_read(CAL_EEPROM_ADDRESS, (uint8_t *)&record, sizeof(record))) {
        return false;
    }
    if (record.magic != CAL_MAGIC || record.check != check_value(&record.cal)) {
        return false;
    }

    *cal = record.cal;
    return true;
}

bool cal_storage_save(const apps_calibration_t *cal)
{
    cal_record_t record;
    apps_calibration_t readback;

    record.magic = CAL_MAGIC;
    record.cal = *cal;
    record.check = check_value(cal);

    if (!eeprom_write(CAL_EEPROM_ADDRESS, (const uint8_t *)&record, sizeof(record))) {
        return false;
    }

    /* Read it back to be sure */
    return cal_storage_load(&readback)
        && readback.apps1_zero_mv == cal->apps1_zero_mv
        && readback.apps1_full_mv == cal->apps1_full_mv
        && readback.apps2_zero_mv == cal->apps2_zero_mv
        && readback.apps2_full_mv == cal->apps2_full_mv;
}
