/*
 * CAN transmit on FDCAN1, classic CAN frames with 11-bit IDs.
 * (Transceiver: TCAN3404, standby and shutdown pins tied low = always on.)
 */
#ifndef CAN_H
#define CAN_H

#include <stdbool.h>
#include <stdint.h>

/* Start the CAN peripheral. Returns false if the bitrate cannot be produced. */
bool can_init(uint32_t bitrate);

/*
 * Queue one data frame (len 0-8). Returns false if it could not be queued
 * because earlier frames are still waiting to go out (e.g. nothing else is
 * on the bus to acknowledge them) or the bus is in the bus-off state.
 */
bool can_send(uint16_t id, const uint8_t *data, uint8_t len);

#endif
