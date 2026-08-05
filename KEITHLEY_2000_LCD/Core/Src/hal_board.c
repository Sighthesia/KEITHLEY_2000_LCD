#include "hal_board.h"

#include "lt7680_bus.h"
#include "stm32f1xx_hal.h"

#define UART_BAUD 115200u

#define LCD_CS_GPIO_PORT GPIOA
#define LCD_CS_PIN GPIO_PIN_0
#define LCD_SCLK_GPIO_PORT GPIOA
#define LCD_SCLK_PIN GPIO_PIN_1
#define LCD_SDI_GPIO_PORT GPIOA
#define LCD_SDI_PIN GPIO_PIN_2
#define LCM_RES_GPIO_PORT GPIOA
#define LCM_RES_PIN GPIO_PIN_3
#define LCM_SS_GPIO_PORT GPIOA
#define LCM_SS_PIN GPIO_PIN_4
#define LCM_SCK_GPIO_PORT GPIOA
#define LCM_SCK_PIN GPIO_PIN_5
#define LCM_SDO_GPIO_PORT GPIOA
#define LCM_SDO_PIN GPIO_PIN_6
#define LCM_INT_GPIO_PORT GPIOA
#define LCM_INT_PIN GPIO_PIN_7

#define UART_TX_GPIO_PORT GPIOA
#define UART_TX_PIN GPIO_PIN_9
#define UART_RX_GPIO_PORT GPIOA
#define UART_RX_PIN GPIO_PIN_10

#define KEY_COL_GPIO_PORT GPIOB
#define KEY_COL_PIN_MASK (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | \
                          GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)
#define KEY_ROW1_PIN GPIO_PIN_13
#define KEY_ROW2_PIN GPIO_PIN_14
#define KEY_ROW3_PIN GPIO_PIN_15
#define KEY_ROW4_PIN GPIO_PIN_10

static void hal_cs(bool level)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN,
                      level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void hal_rst(bool level)
{
    HAL_GPIO_WritePin(LCM_RES_GPIO_PORT, LCM_RES_PIN,
                      level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void hal_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static uint8_t hal_spi_xfer(uint8_t byte)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LCD_SDI_GPIO_PORT, LCD_SDI_PIN,
                          (byte & (1u << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(LCM_SDO_GPIO_PORT, LCM_SDO_PIN) == GPIO_PIN_SET) {
            in |= (1u << i);
        }
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_RESET);
    }
    return in;
}

static const lt7680_bus_io_t s_lt7680_io = {
    .cs = hal_cs,
    .rst = hal_rst,
    .spi_xfer = hal_spi_xfer,
    .delay_ms = hal_delay_ms,
};

static void init_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LCM control outputs (PA0..PA5): SCS, panel SCL, SDI(4-wire), RST, SS, SCK. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = LCD_CS_PIN | LCD_SCLK_PIN | LCD_SDI_PIN | LCM_RES_PIN |
               LCM_SS_PIN | LCM_SCK_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* LCM_SDO is driven by LT7680 (4-wire SDO), so it is an input. */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = LCM_SDO_PIN | LCM_INT_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Keyboard columns are outputs, driven low when not scanning. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = KEY_COL_PIN_MASK;
    HAL_GPIO_Init(KEY_COL_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(KEY_COL_GPIO_PORT, KEY_COL_PIN_MASK, GPIO_PIN_RESET);

    /* Keyboard rows are inputs with pull-up. */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = KEY_ROW4_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = KEY_ROW1_PIN | KEY_ROW2_PIN | KEY_ROW3_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Defaults: CS idle high, reset released. */
    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCM_RES_GPIO_PORT, LCM_RES_PIN, GPIO_PIN_SET);

    /* USART1 TX on PA9 (AF push-pull), RX on PA10 (input). */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = UART_TX_PIN;
    HAL_GPIO_Init(UART_TX_GPIO_PORT, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = UART_RX_PIN;
    HAL_GPIO_Init(UART_RX_GPIO_PORT, &gpio);
}

static void init_uart(void)
{
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    uint32_t brr = ((pclk2 * 16u) + (UART_BAUD / 2u)) / UART_BAUD;

    __HAL_RCC_USART1_CLK_ENABLE();
    USART1->CR1 = 0;
    USART1->BRR = brr;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_put_byte(uint8_t b)
{
    while ((USART1->SR & USART_SR_TXE) == 0u) {
    }
    USART1->DR = b;
}

void hal_board_init(void)
{
    init_gpio();
    init_uart();
    lt7680_bus_init(&s_lt7680_io);
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uart_put_byte(data[i]);
    }
}

int hal_uart_receive_byte(void)
{
    if ((USART1->SR & USART_SR_RXNE) == 0u) {
        return -1;
    }
    return (int)(USART1->DR & 0xFFu);
}
