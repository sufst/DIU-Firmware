#include "debug.h"

/*
 * The columns, in the order they are printed. Each column is 'width'
 * characters wide with its content right-aligned, so the heading and every
 * value line come out the same length and line up.
 *
 * To add a column: add it here, then add the matching put_... call at the
 * same position in debug_format_line(). The PC test checks they agree.
 */
typedef struct {
    const char *heading;
    uint8_t width;
} column_t;

static const column_t s_columns[] = {
    { "TIME_s",  9 },   /* seconds since power-up                    */
    { "A1_mV",   6 },   /* APPS1 voltage                             */
    { "A1_%",    6 },   /* APPS1 position                            */
    { "A2_mV",   6 },   /* APPS2 voltage                             */
    { "A2_%",    6 },   /* APPS2 position                            */
    { "PEDAL%",  7 },   /* combined pedal position, as sent on CAN   */
    { "BRK_mV",  7 },   /* brake pressure sensor voltage             */
    { "BAR",     6 },   /* brake pressure                            */
    { "STR_mV",  7 },   /* steering sensor voltage                   */
    { "STR_%",   6 },   /* steering position                         */
    { "F1",      3 },   /* fault code: APPS1                         */
    { "F2",      3 },   /* fault code: APPS2                         */
    { "FB",      3 },   /* fault code: brake                         */
    { "FS",      3 },   /* fault code: steering                      */
    { "IMP",     4 },   /* APPS implausibility flag                  */
    { "BPP",     4 },   /* APPS / brake plausibility (EV6.3) flag    */
    { "CAL",     4 },   /* NO = not calibrated, OK, RUN = in progress */
    { "GLV_V",   7 },   /* GLV supply voltage                        */
    { "TEMP_C",  7 },   /* board temperature                         */
    { "AX_g",    8 },   /* acceleration                              */
    { "AY_g",    8 },
    { "AZ_g",    8 },
    { "GX_dps",  8 },   /* rotation rate                             */
    { "GY_dps",  8 },
    { "GZ_dps",  8 },
};

#define COLUMN_COUNT (sizeof(s_columns) / sizeof(s_columns[0]))

/* Where we have got to while building a line */
typedef struct {
    char *buf;
    size_t length;
    size_t column;
} writer_t;

static void put_char(writer_t *w, char c)
{
    if (w->length < DEBUG_LINE_MAX) {
        w->buf[w->length++] = c;
    }
}

/* Print text right-aligned in the next column */
static void put_text(writer_t *w, const char *text)
{
    size_t width = (w->column < COLUMN_COUNT) ? s_columns[w->column].width : 1u;
    size_t text_length = 0;

    while (text[text_length] != '\0') {
        text_length++;
    }
    for (size_t i = text_length; i < width; i++) {
        put_char(w, ' ');
    }
    for (size_t i = 0; i < text_length; i++) {
        put_char(w, text[i]);
    }
    w->column++;
}

/*
 * Print a number in the next column. 'decimals' says how many of its digits
 * come after the decimal point: put_number(w, 1234, 1) prints "123.4".
 */
static void put_number(writer_t *w, int32_t value, uint32_t decimals)
{
    char text[16];
    size_t pos = sizeof(text);
    bool negative = value < 0;
    uint32_t magnitude = negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    uint32_t digits = 0;

    /* Build the text backwards, from the last digit */
    text[--pos] = '\0';
    do {
        text[--pos] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
        digits++;
        if (digits == decimals) {
            text[--pos] = '.';
        }
    } while (magnitude != 0u || digits <= decimals);

    if (negative) {
        text[--pos] = '-';
    }
    put_text(w, &text[pos]);
}

static void end_line(writer_t *w)
{
    put_char(w, '\r');
    put_char(w, '\n');
}

size_t debug_format_header(char buf[DEBUG_LINE_MAX])
{
    writer_t w = { buf, 0, 0 };

    for (size_t i = 0; i < COLUMN_COUNT; i++) {
        put_text(&w, s_columns[i].heading);
    }
    end_line(&w);
    return w.length;
}

size_t debug_format_line(char buf[DEBUG_LINE_MAX], const debug_data_t *d)
{
    writer_t w = { buf, 0, 0 };
    const sensor_reading_t *apps1 = &d->readings[SENSOR_APPS1];
    const sensor_reading_t *apps2 = &d->readings[SENSOR_APPS2];
    const sensor_reading_t *brake = &d->readings[SENSOR_BRAKE];
    const sensor_reading_t *steer = &d->readings[SENSOR_STEERING];

    put_number(&w, (int32_t)(d->time_ms / 10u), 2);     /* TIME_s  */
    put_number(&w, apps1->millivolts, 0);               /* A1_mV   */
    put_number(&w, apps1->value, 1);                    /* A1_%    */
    put_number(&w, apps2->millivolts, 0);               /* A2_mV   */
    put_number(&w, apps2->value, 1);                    /* A2_%    */
    put_number(&w, d->safety.pedal, 1);                 /* PEDAL%  */
    put_number(&w, brake->millivolts, 0);               /* BRK_mV  */
    put_number(&w, brake->value, 1);                    /* BAR     */
    put_number(&w, steer->millivolts, 0);               /* STR_mV  */
    put_number(&w, steer->value, 1);                    /* STR_%   */
    put_number(&w, apps1->fault, 0);                    /* F1      */
    put_number(&w, apps2->fault, 0);                    /* F2      */
    put_number(&w, brake->fault, 0);                    /* FB      */
    put_number(&w, steer->fault, 0);                    /* FS      */
    put_number(&w, d->safety.apps_implausible, 0);      /* IMP     */
    put_number(&w, d->safety.brake_plausibility_tripped, 0);    /* BPP */
    put_text(&w, d->calibrating ? "RUN" : (d->calibrated ? "OK" : "NO"));   /* CAL */
    put_number(&w, d->glv_mv / 10, 2);                  /* GLV_V   */

    if (d->temperature_ok) {
        put_number(&w, d->temperature_dc, 1);           /* TEMP_C  */
    } else {
        put_text(&w, "---");
    }

    for (uint32_t axis = 0; axis < 3u; axis++) {        /* AX_g, AY_g, AZ_g */
        if (d->imu_ok) {
            put_number(&w, d->imu.accel_mg[axis], 3);
        } else {
            put_text(&w, "---");
        }
    }
    for (uint32_t axis = 0; axis < 3u; axis++) {        /* GX_dps, GY_dps, GZ_dps */
        if (d->imu_ok) {
            put_number(&w, d->imu.gyro_ddps[axis], 1);
        } else {
            put_text(&w, "---");
        }
    }

    end_line(&w);
    return w.length;
}
