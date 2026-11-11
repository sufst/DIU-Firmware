#include "serial.h"
#include "board.h"
#include "config.h"

static UART_HandleTypeDef s_uart;
static DMA_HandleTypeDef s_dma;
static bool s_ready;

bool serial_init(void)
{
    /*
     * DMA channel: once started it copies bytes from our buffer to the UART
     * one at a time, as fast as the UART can take them, without the CPU
     * being involved.
     */
    __HAL_RCC_DMA1_CLK_ENABLE();
    s_dma.Instance = DMA1_Channel1;
    s_dma.Init.Request = DMA_REQUEST_USART2_TX;
    s_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    s_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_dma.Init.MemInc = DMA_MINC_ENABLE;
    s_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_dma.Init.Mode = DMA_NORMAL;
    s_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&s_dma) != HAL_OK) {
        return false;
    }
    __HAL_LINKDMA(&s_uart, hdmatx, s_dma);

    /* The pin is set up by HAL_UART_MspInit() in board.c */
    s_uart.Instance = USART2;
    s_uart.Init.BaudRate = DEBUG_BAUD;
    s_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_uart.Init.StopBits = UART_STOPBITS_1;
    s_uart.Init.Parity = UART_PARITY_NONE;
    s_uart.Init.Mode = UART_MODE_TX;
    s_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    s_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&s_uart) != HAL_OK) {
        return false;
    }

    /* Two interrupts tell the HAL when a transfer has finished. Lowest
     * priority: debug output must never hold anything else up. */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    s_ready = true;
    return true;
}

bool serial_busy(void)
{
    return !s_ready || s_uart.gState != HAL_UART_STATE_READY;
}

bool serial_send(const char *data, uint16_t len)
{
    if (serial_busy() || len == 0u) {
        return false;
    }
    return HAL_UART_Transmit_DMA(&s_uart, (const uint8_t *)data, len) == HAL_OK;
}

/* Interrupt handlers: pass the events on to the HAL */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_dma);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&s_uart);
}
