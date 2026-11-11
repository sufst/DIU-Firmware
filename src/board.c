#include "board.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * Clock
 * ------------------------------------------------------------------------- */

/*
 * Run the whole chip from the 25 MHz crystal (the STM32C0 has no PLL).
 * If the crystal does not start, fall back to the internal oscillator at
 * 24 MHz so the board still talks on CAN instead of going silent.
 */
static void clock_init(void)
{
    RCC_OscInitTypeDef osc = { 0 };
    RCC_ClkInitTypeDef clk = { 0 };

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV1;
    clk.APB1CLKDivider = RCC_APB1_DIV1;

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;

    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSEState = RCC_HSE_OFF;
        osc.HSIState = RCC_HSI_ON;
        osc.HSIDiv = RCC_HSI_DIV2;                      /* 48 MHz / 2 */
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
            board_fatal();
        }
        clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    }

    /* One flash wait state is required above 24 MHz. */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) {
        board_fatal();
    }
}

/* -------------------------------------------------------------------------
 * Case button and LED
 * ------------------------------------------------------------------------- */

static void button_led_init(void)
{
    GPIO_InitTypeDef gpio = { 0 };

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* LED: output, starting switched off */
    board_led_set(false);
    gpio.Pin = LED_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &gpio);

    /* Button: input with a pull resistor so it reads "released" when open */
    gpio.Pin = BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = BUTTON_PULL;
    HAL_GPIO_Init(BUTTON_PORT, &gpio);
}

bool board_button_pressed(void)
{
    return HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == BUTTON_PRESSED_LEVEL;
}

void board_led_set(bool on)
{
    GPIO_PinState off_level = (LED_ON_LEVEL == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, on ? LED_ON_LEVEL : off_level);
}

void board_init(void)
{
    HAL_Init();         /* starts the 1 ms SysTick used by HAL_GetTick()/HAL_Delay() */
    clock_init();       /* also re-times the SysTick for the new clock */
    button_led_init();
}

/* -------------------------------------------------------------------------
 * Timing
 * ------------------------------------------------------------------------- */

/*
 * Microsecond delay. SysTick counts down once per CPU clock and reloads every
 * millisecond, so we add up how far it has moved until enough clocks have
 * passed. Works for any length of delay.
 */
void delay_us(uint32_t us)
{
    uint32_t wanted = us * (SystemCoreClock / 1000000u);
    uint32_t reload = SysTick->LOAD + 1u;
    uint32_t last = SysTick->VAL;
    uint32_t elapsed = 0;

    while (elapsed < wanted) {
        uint32_t now = SysTick->VAL;
        elapsed += (last >= now) ? (last - now) : (last + reload - now);
        last = now;
    }
}

/* -------------------------------------------------------------------------
 * Watchdog
 * ------------------------------------------------------------------------- */

static IWDG_HandleTypeDef s_watchdog;

void board_watchdog_start(void)
{
    /* Pause the watchdog while the CPU is halted in the debugger, otherwise
     * every breakpoint would end in a reset. */
    __HAL_RCC_DBGMCU_CLK_ENABLE();
    __HAL_DBGMCU_FREEZE_IWDG();

    /* The watchdog has its own 32 kHz oscillator. Divided by 32 it counts
     * roughly once per millisecond, so the reload value is the timeout in ms. */
    s_watchdog.Instance = IWDG;
    s_watchdog.Init.Prescaler = IWDG_PRESCALER_32;
    s_watchdog.Init.Reload = WATCHDOG_TIMEOUT_MS;
    s_watchdog.Init.Window = IWDG_WINDOW_DISABLE;
    HAL_IWDG_Init(&s_watchdog);
}

void board_watchdog_kick(void)
{
    HAL_IWDG_Refresh(&s_watchdog);
}

void board_fatal(void)
{
    NVIC_SystemReset();
    for (;;) {
    }
}

/* -------------------------------------------------------------------------
 * Interrupt handlers
 * ------------------------------------------------------------------------- */

/* 1 ms tick: keeps HAL_GetTick() running. */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* A crash restarts the board rather than leaving it hung. */
void HardFault_Handler(void)
{
    board_fatal();
}

/* -------------------------------------------------------------------------
 * Pin setup
 *
 * The HAL calls these "MSP" functions from HAL_SPI_Init() / HAL_FDCAN_Init()
 * to switch on the peripheral clock and connect it to its pins.
 * ------------------------------------------------------------------------- */

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (hspi->Instance != SPI1) {
        return;
    }

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Chip select: ordinary output, idle high (set before it becomes an output) */
    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_SET);
    gpio.Pin = ADC_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ADC_CS_PORT, &gpio);

    /* PA5 = SCK, PA6 = MISO, PA7 = MOSI */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Alternate = GPIO_AF0_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/* I2C2: PB3 = SCL, PA12 = SDA. The pull-ups (R24, R25) are on the board. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (hi2c->Instance != I2C2) {
        return;
    }

    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_OD;        /* open drain, as I2C requires */
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* PB4, PB5 and PB6 share package pin 20 with PB3 and must stay analog. */
    gpio.Pin = GPIO_PIN_3;
    gpio.Alternate = GPIO_AF5_I2C2;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = GPIO_PIN_12;
    gpio.Alternate = GPIO_AF6_I2C2;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/* ADC1 input 0 on PA0: the GLV supply monitor */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (hadc->Instance != ADC1) {
        return;
    }

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/* USART2 transmit on PA2 (the DMA channel is set up in serial.c) */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (huart->Instance != USART2) {
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan)
{
    GPIO_InitTypeDef gpio = { 0 };

    if (hfdcan->Instance != FDCAN1) {
        return;
    }

    __HAL_RCC_FDCAN1_CLK_ENABLE();      /* clocked from PCLK by default */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* PA11 = CAN RX */
    gpio.Pin = GPIO_PIN_11;
    gpio.Alternate = GPIO_AF4_FDCAN1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PB1 = CAN TX. On this package PB0, PB2 and PA8 are bonded to the same
     * pin, so they must be left in their reset (analog) state.
     */
    gpio.Pin = GPIO_PIN_1;
    gpio.Alternate = GPIO_AF3_FDCAN1;
    HAL_GPIO_Init(GPIOB, &gpio);
}
