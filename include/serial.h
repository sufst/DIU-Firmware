/*
 * Serial debug port: USART2 transmit on PA2 (test point TP5), DEBUG_BAUD,
 * 8 data bits, no parity, 1 stop bit.
 *
 * Sending is done in the background by DMA: serial_send() hands over a
 * buffer and returns straight away, so printing never delays the 1 ms loop.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stdbool.h>
#include <stdint.h>

bool serial_init(void);

/* True while the previous serial_send() is still going out. */
bool serial_busy(void);

/*
 * Start sending len bytes. The buffer must stay untouched until
 * serial_busy() is false again. Returns false (sends nothing) if busy.
 */
bool serial_send(const char *data, uint16_t len);

#endif
