#include "hal_board.h"

#include "lt7680_bus.h"
#include "stm32f1xx_hal.h"

#define UART_BAUD 115200u

/* LT7680 SPI transport: 1 = SPI1 hardware (PA4 CS, PA5 SCK, PA6 MISO,
 * PA7 MOSI), 0 = software bit-bang fallback. Hardware SPI is ~25-30x faster;
 * keep the bit-bang path for on-target diagnosis. */
#define LT7680_SPI_HW 1u

#define LCD_CS_GPIO_PORT GPIOA
#define LCD_CS_PIN GPIO_PIN_0
#define LCD_SCLK_GPIO_PORT GPIOA
#define LCD_SCLK_PIN GPIO_PIN_1
#define LCD_SDI_GPIO_PORT GPIOA
#define LCD_SDI_PIN GPIO_PIN_2
#define LCM_RES_GPIO_PORT GPIOA
#define LCM_RES_PIN GPIO_PIN_3
#define LT7680_CS_GPIO_PORT GPIOA
#define LT7680_CS_PIN GPIO_PIN_4
#define LCM_SDO_GPIO_PORT GPIOA
#define LCM_SDO_PIN GPIO_PIN_6
#define LCM_SCK_GPIO_PORT GPIOA
#define LCM_SCK_PIN GPIO_PIN_5
#define LT7680_SDI_GPIO_PORT GPIOA
#define LT7680_SDI_PIN GPIO_PIN_7
#define LCM_INT_GPIO_PORT GPIOA
#define LCM_INT_PIN GPIO_PIN_8

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
    HAL_GPIO_WritePin(LT7680_CS_GPIO_PORT, LT7680_CS_PIN,
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
#if LT7680_SPI_HW
    while ((SPI1->SR & SPI_SR_TXE) == 0u) {
    }
    SPI1->DR = byte;
    while ((SPI1->SR & SPI_SR_RXNE) == 0u) {
    }
    return (uint8_t)SPI1->DR;
#else
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LT7680_SDI_GPIO_PORT, LT7680_SDI_PIN,
                          (byte & (1u << i)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(LCM_SDO_GPIO_PORT, LCM_SDO_PIN) == GPIO_PIN_SET) {
            in |= (1u << i);
        }
        HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN, GPIO_PIN_RESET);
    }
    return in;
#endif
}

static void panel_write_9bit(bool data, uint8_t value)
{
    uint16_t frame = (uint16_t)value | (data ? 0x100u : 0u);

    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    for (int bit = 8; bit >= 0; bit--) {
        HAL_GPIO_WritePin(LCD_SCLK_GPIO_PORT, LCD_SCLK_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LCD_SDI_GPIO_PORT, LCD_SDI_PIN,
                          (frame & (1u << bit)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LCD_SCLK_GPIO_PORT, LCD_SCLK_PIN, GPIO_PIN_SET);
    }
    HAL_GPIO_WritePin(LCD_SCLK_GPIO_PORT, LCD_SCLK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
}

static void panel_write_command(uint8_t command, const uint8_t *data, uint8_t length)
{
    panel_write_9bit(false, command);
    for (uint8_t i = 0u; i < length; i++) {
        panel_write_9bit(true, data[i]);
    }
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

    /* Latest schematic: PA0..PA2 configure the ER-PCBA5981. LT7680 uses
     * PA3=RST, PA4=SCS, PA5=SCK, PA6=SDO, PA7=SDI, and PA8=INT. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = LCD_CS_PIN | LCD_SCLK_PIN | LCD_SDI_PIN | LCM_RES_PIN |
               LT7680_CS_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

#if LT7680_SPI_HW
    /* SPI1 hardware: PA5 (SCK) and PA7 (MOSI) are alternate-function
     * push-pull. CS (PA4) stays a plain GPIO so the bit-bang and hardware
     * paths share the same chip-select handling. */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = LCM_SCK_PIN | LT7680_SDI_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);
#else
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = LCM_SCK_PIN | LT7680_SDI_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);
#endif

    /* LT7680 SDO is PA6 and INT is PA8. Do not drive either line. */
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

    /* Defaults: both chip selects idle high, LT7680 reset released. */
    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LT7680_CS_GPIO_PORT, LT7680_CS_PIN, GPIO_PIN_SET);
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
    uint32_t brr = (pclk2 + (UART_BAUD / 2u)) / UART_BAUD;

    __HAL_RCC_USART1_CLK_ENABLE();
    USART1->CR1 = 0;
    USART1->BRR = brr;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

#if LT7680_SPI_HW
static void init_spi1(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    SPI1->CR1 = 0;
    /* Mode 0 (CPOL=0, CPHA=0), master, 8-bit, MSB first, /2 prescaler.
     * PCLK2 = 8 MHz -> SPI clock = 4 MHz.  Mode 0 matches the verified
     * bit-bang signalling the LT7680 color-bars milestone used. */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM | SPI_CR1_BR_0;
    SPI1->CR2 = 0;
    SPI1->CR1 |= SPI_CR1_SPE;
}
#endif

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
#if LT7680_SPI_HW
    init_spi1();
#endif
    lt7680_bus_init(&s_lt7680_io);
}

void hal_panel_init(void)
{
    /* TFT_RST (X6 pin6) is an RC auto-reset (R20 10k + C37/C40, tau ~11 ms);
     * wait for the panel to exit reset before sending the 9-bit init. */
    HAL_Delay(200u);

    static const uint8_t page_13[] = {0x77u, 0x01u, 0x00u, 0x00u, 0x13u};
    static const uint8_t page_10[] = {0x77u, 0x01u, 0x00u, 0x00u, 0x10u};
    static const uint8_t page_11[] = {0x77u, 0x01u, 0x00u, 0x00u, 0x11u};
    static const uint8_t page_00[] = {0x77u, 0x01u, 0x00u, 0x00u, 0x00u};
    static const uint8_t gamma_p[] = {
        0x40u, 0x14u, 0x59u, 0x10u, 0x12u, 0x08u, 0x03u, 0x09u,
        0x05u, 0x1Eu, 0x05u, 0x14u, 0x10u, 0x68u, 0x33u, 0x15u,
    };
    static const uint8_t gamma_n[] = {
        0x40u, 0x08u, 0x53u, 0x09u, 0x11u, 0x09u, 0x02u, 0x07u,
        0x09u, 0x1Au, 0x04u, 0x12u, 0x12u, 0x64u, 0x29u, 0x29u,
    };
    static const uint8_t e1[] = {0x02u, 0x8Cu, 0x00u, 0x00u, 0x03u, 0x8Cu,
                                 0x00u, 0x00u, 0x00u, 0x33u, 0x33u};
    static const uint8_t e2[] = {0x33u, 0x33u, 0x33u, 0x33u, 0xC9u, 0x3Cu,
                                 0x00u, 0x00u, 0xCAu, 0x3Cu, 0x00u, 0x00u,
                                 0x00u};
    static const uint8_t e5[] = {0x05u, 0xCDu, 0x82u, 0x82u, 0x01u, 0xC9u,
                                 0x82u, 0x82u, 0x07u, 0xCFu, 0x82u, 0x82u,
                                 0x03u, 0xCBu, 0x82u, 0x82u};
    static const uint8_t e8[] = {0x06u, 0xCEu, 0x82u, 0x82u, 0x02u, 0xCAu,
                                 0x82u, 0x82u, 0x08u, 0xD0u, 0x82u, 0x82u,
                                 0x04u, 0xCCu, 0x82u, 0x82u};
    static const uint8_t ed[] = {0xFFu, 0xF0u, 0x07u, 0x65u, 0x4Fu, 0xFCu,
                                 0xC2u, 0x2Fu, 0xF2u, 0x2Cu, 0xCFu, 0xF4u,
                                 0x56u, 0x70u, 0x0Fu, 0xFFu};

    panel_write_command(0xFFu, page_13, sizeof(page_13));
    panel_write_command(0xEFu, (const uint8_t[]){0x08u}, 1u);
    panel_write_command(0xFFu, page_10, sizeof(page_10));
    panel_write_command(0xC0u, (const uint8_t[]){0x77u, 0x00u}, 2u);
    panel_write_command(0xC1u, (const uint8_t[]){0x09u, 0x08u}, 2u);
    panel_write_command(0xC2u, (const uint8_t[]){0x37u, 0x02u}, 2u);
    panel_write_command(0xC3u, (const uint8_t[]){0x80u, 0x05u, 0x0Du}, 3u);
    panel_write_command(0xCCu, (const uint8_t[]){0x10u}, 1u);
    panel_write_command(0xB0u, gamma_p, sizeof(gamma_p));
    panel_write_command(0xB1u, gamma_n, sizeof(gamma_n));

    panel_write_command(0xFFu, page_11, sizeof(page_11));
    panel_write_command(0xB0u, (const uint8_t[]){0x6Du}, 1u);
    panel_write_command(0xB1u, (const uint8_t[]){0x1Du}, 1u);
    panel_write_command(0xB2u, (const uint8_t[]){0x87u}, 1u);
    panel_write_command(0xB3u, (const uint8_t[]){0x80u}, 1u);
    panel_write_command(0xB5u, (const uint8_t[]){0x49u}, 1u);
    panel_write_command(0xB7u, (const uint8_t[]){0x85u}, 1u);
    panel_write_command(0xB8u, (const uint8_t[]){0x20u}, 1u);
    panel_write_command(0xC1u, (const uint8_t[]){0x78u}, 1u);
    panel_write_command(0xC2u, (const uint8_t[]){0x78u}, 1u);
    panel_write_command(0xD0u, (const uint8_t[]){0x88u}, 1u);
    panel_write_command(0xE0u, (const uint8_t[]){0x00u, 0x00u, 0x02u}, 3u);
    panel_write_command(0xE1u, e1, sizeof(e1));
    panel_write_command(0xE2u, e2, sizeof(e2));
    panel_write_command(0xE3u, (const uint8_t[]){0x00u, 0x00u, 0x33u, 0x33u}, 4u);
    panel_write_command(0xE4u, (const uint8_t[]){0x44u, 0x44u}, 2u);
    panel_write_command(0xE5u, e5, sizeof(e5));
    panel_write_command(0xE6u, (const uint8_t[]){0x00u, 0x00u, 0x33u, 0x33u}, 4u);
    panel_write_command(0xE7u, (const uint8_t[]){0x44u, 0x44u}, 2u);
    panel_write_command(0xE8u, e8, sizeof(e8));
    panel_write_command(0xEBu, (const uint8_t[]){0x08u, 0x01u, 0xE4u, 0xE4u,
                                                  0x88u, 0x00u, 0x40u}, 7u);
    panel_write_command(0xECu, (const uint8_t[]){0x00u, 0x00u, 0x00u}, 3u);
    panel_write_command(0xEDu, ed, sizeof(ed));
    panel_write_command(0xEFu, (const uint8_t[]){0x10u, 0x0Du, 0x04u, 0x08u,
                                                  0x3Fu, 0x1Fu}, 6u);
    panel_write_command(0xFFu, page_00, sizeof(page_00));
    panel_write_command(0x11u, 0, 0u);
    HAL_Delay(120u);
    panel_write_command(0x35u, (const uint8_t[]){0x00u}, 1u);
    panel_write_command(0x3Au, (const uint8_t[]){0x66u}, 1u);
#if PANEL_LANDSCAPE
    /* Landscape: ST7701S MADCTL (0x36) row/column exchange so each 960-wide
     * LT7680 scan line lands on the panel's long (960 px) axis. 0x60 = MX|MV
     * (90 deg); alternate 0xE0 = MY|MV flips the other way. Value must be
     * confirmed on real device; if MADCTL is ignored in RGB mode, fall back
     * to rotating via LT7680 MACR REG[02h] bit[2:1] in lt7680_gfx.c. PCLK
     * (LT7680 PLL C1/C2) may need retuning for the swapped 960x320 timing. */
    panel_write_command(0x36u, (const uint8_t[]){0x60u}, 1u);
#endif
    panel_write_command(0x29u, 0, 0u);
}

void hal_uart_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uart_put_byte(data[i]);
    }
}

void hal_uart_send_text(const char *text)
{
    while (*text != '\0') {
        uart_put_byte((uint8_t)*text++);
    }
}

void hal_uart_send_hex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    uart_put_byte((uint8_t)digits[value >> 4]);
    uart_put_byte((uint8_t)digits[value & 0x0Fu]);
}

int hal_uart_receive_byte(void)
{
    if ((USART1->SR & USART_SR_RXNE) == 0u) {
        return -1;
    }
    return (int)(USART1->DR & 0xFFu);
}
