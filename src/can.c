#include "can.h"
#include "board.h"

static FDCAN_HandleTypeDef s_can;

/*
 * Work out the bit timing for the requested bitrate.
 *
 * A CAN bit is divided into "time quanta" (tq). We look for a prescaler that
 * gives a whole number of tq per bit, then put the sample point about 80 %
 * of the way through the bit:
 *
 *      | sync (1 tq) |      segment 1      | segment 2 |
 *                                          ^ sample point
 */
static bool bit_timing(uint32_t clock_hz, uint32_t bitrate, FDCAN_InitTypeDef *init)
{
    static const uint8_t tq_options[] = { 25, 20, 16, 12, 10, 8 };

    for (uint32_t i = 0; i < sizeof(tq_options); i++) {
        uint32_t tq = tq_options[i];
        uint32_t divider = bitrate * tq;

        if (clock_hz % divider == 0u && clock_hz / divider <= 512u) {
            uint32_t seg2 = tq / 5u;

            init->NominalPrescaler = clock_hz / divider;
            init->NominalTimeSeg2 = seg2;
            init->NominalTimeSeg1 = tq - 1u - seg2;
            init->NominalSyncJumpWidth = seg2;
            return true;
        }
    }
    return false;
}

bool can_init(uint32_t bitrate)
{
    s_can.Instance = FDCAN1;
    s_can.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    s_can.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    s_can.Init.Mode = FDCAN_MODE_NORMAL;
    s_can.Init.AutoRetransmission = ENABLE;
    s_can.Init.TransmitPause = DISABLE;
    s_can.Init.ProtocolException = DISABLE;
    s_can.Init.StdFiltersNbr = 0;
    s_can.Init.ExtFiltersNbr = 0;
    s_can.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    /* CAN FD data-phase timing: not used, but must hold valid values */
    s_can.Init.DataPrescaler = 1;
    s_can.Init.DataSyncJumpWidth = 1;
    s_can.Init.DataTimeSeg1 = 1;
    s_can.Init.DataTimeSeg2 = 1;

    if (!bit_timing(HAL_RCC_GetPCLK1Freq(), bitrate, &s_can.Init)) {
        return false;
    }

    /* Pins and clocks are set up by HAL_FDCAN_MspInit() in board.c */
    if (HAL_FDCAN_Init(&s_can) != HAL_OK) {
        return false;
    }

    /* This board only transmits for now, so ignore everything received */
    if (HAL_FDCAN_ConfigGlobalFilter(&s_can, FDCAN_REJECT, FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) {
        return false;
    }

    return HAL_FDCAN_Start(&s_can) == HAL_OK;
}

bool can_send(uint16_t id, const uint8_t *data, uint8_t len)
{
    FDCAN_ProtocolStatusTypeDef status;
    FDCAN_TxHeaderTypeDef header = { 0 };

    /*
     * Bus-off: after too many transmit errors the peripheral takes itself off
     * the bus and stays off until restarted. Restart it and skip this frame.
     */
    if (HAL_FDCAN_GetProtocolStatus(&s_can, &status) == HAL_OK && status.BusOff != 0u) {
        HAL_FDCAN_Stop(&s_can);
        HAL_FDCAN_Start(&s_can);
        return false;
    }

    /*
     * The transmit queue only fills up if frames are not getting out. Old
     * sensor values are worthless, so throw the queued frames away to make
     * room for fresh ones rather than sending stale data later.
     */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&s_can) == 0u) {
        HAL_FDCAN_AbortTxRequest(&s_can, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
        return false;
    }

    header.Identifier = id;
    header.IdType = FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = (len > 8u) ? 8u : len;      /* for 0-8 bytes the DLC code is the length */
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_OFF;
    header.FDFormat = FDCAN_CLASSIC_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0;

    return HAL_FDCAN_AddMessageToTxFifoQ(&s_can, &header, data) == HAL_OK;
}
