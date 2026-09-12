#include "hal_board.h"

#include "keypad.h"
#include "lt7680_bus.h"
#include "sht3x.h"
#include "spi_timeout.h"
#include "stm32f1xx_hal.h"
#include "uart_rx_queue.h"

/* V16-reverse-engineered link rate: the upstream firmware computes
 * BRR = (25*fclk/baud/100)<<4|frac for nominal 38400 (its CR1 bit15 test
 * always reads 0), which lands on USARTDIV=468.75 at 72 MHz = exactly
 * 9600 baud, 8N1, no flow control. The host speaks 9600; do not change. */
#define UART_BAUD 9600u

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

/* SHT3x soft-I2C: PB15=SCL, PB14=SDA, open-drain with 4.7k pull-ups. */
#define SHT3X_GPIO_PORT GPIOB
#define SHT3X_SCL_PIN GPIO_PIN_15
#define SHT3X_SDA_PIN GPIO_PIN_14

#define KEY_ROW_GPIO_PORT GPIOB
#define KEY_ROW_PIN_MASK (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3)
#define KEY_COL_GPIO_PORT GPIOB
#define KEY_COL_PIN_MASK (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | \
                          GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11)

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

static void sht3x_sda_high(void)
{
    HAL_GPIO_WritePin(SHT3X_GPIO_PORT, SHT3X_SDA_PIN, GPIO_PIN_SET);
}

static void sht3x_sda_low(void)
{
    HAL_GPIO_WritePin(SHT3X_GPIO_PORT, SHT3X_SDA_PIN, GPIO_PIN_RESET);
}

static void sht3x_scl_high(void)
{
    HAL_GPIO_WritePin(SHT3X_GPIO_PORT, SHT3X_SCL_PIN, GPIO_PIN_SET);
}

static void sht3x_scl_low(void)
{
    HAL_GPIO_WritePin(SHT3X_GPIO_PORT, SHT3X_SCL_PIN, GPIO_PIN_RESET);
}

static bool sht3x_sda_read(void)
{
    return HAL_GPIO_ReadPin(SHT3X_GPIO_PORT, SHT3X_SDA_PIN) == GPIO_PIN_SET;
}

static void sht3x_delay_us(uint32_t us)
{
    /* Crude ~1 us per 8 NOPs at 8 MHz SYSCLK; I2C timing is tolerant
     * (sensor supports up to 1 MHz, we run near 100 kHz). */
    volatile uint32_t n = us * 8u;

    while (n-- > 0u) {
        __NOP();
    }
}

static void sht3x_delay_ms_wrap(uint32_t ms)
{
    HAL_Delay(ms);
}

static const sht3x_io_t s_sht3x_io = {
    .sda_high = sht3x_sda_high,
    .sda_low = sht3x_sda_low,
    .scl_high = sht3x_scl_high,
    .scl_low = sht3x_scl_low,
    .sda_read = sht3x_sda_read,
    .delay_us = sht3x_delay_us,
    .delay_ms = sht3x_delay_ms_wrap,
};

static volatile bool s_spi_transfer_failed;
static uint32_t s_spi_timeout_count;

/* A byte at ~4.5 MHz completes in under 2 us. Two milliseconds leaves ample
 * room for interrupt latency while keeping a wedged peripheral bounded. */
#define LT7680_SPI_WAIT_TIMEOUT_MS 2u

static bool hal_spi_failed(void)
{
    bool failed = s_spi_transfer_failed;
    s_spi_transfer_failed = false;
    return failed;
}

static uint8_t hal_spi_xfer(uint8_t byte)
{
#if LT7680_SPI_HW
    /* Bounded waits + OVR recovery: a single glitch that sets overrun
     * used to wedge every subsequent transfer forever (RXNE never set),
     * freezing the whole product mid-render. Now a wedged transfer
     * degrades to a dummy byte and the higher-level status/timeout
     * checks recover the link. */
    uint32_t start_tick;

    if (SPI1->SR & SPI_SR_OVR) {
        volatile uint32_t purge = SPI1->DR;
        purge = SPI1->SR;
        (void)purge;
    }
    start_tick = HAL_GetTick();
    while ((SPI1->SR & SPI_SR_TXE) == 0u) {
        if (k2000_timeout_expired(start_tick, HAL_GetTick(),
                                  LT7680_SPI_WAIT_TIMEOUT_MS)) {
            s_spi_transfer_failed = true;
            s_spi_timeout_count++;
            return 0xFFu;
        }
    }
    SPI1->DR = byte;
    start_tick = HAL_GetTick();
    while ((SPI1->SR & SPI_SR_RXNE) == 0u) {
        if (k2000_timeout_expired(start_tick, HAL_GetTick(),
                                  LT7680_SPI_WAIT_TIMEOUT_MS)) {
            volatile uint32_t purge = SPI1->DR;
            purge = SPI1->SR;
            (void)purge;
            s_spi_transfer_failed = true;
            s_spi_timeout_count++;
            return 0xFFu;
        }
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
    .spi_failed = hal_spi_failed,
    .delay_ms = hal_delay_ms,
};

static uart_rx_queue_t s_uart_rx;

static void init_gpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Park the UART TX data latch high BEFORE the pin switches to AF: PA9
     * floats through the clock-init window and the BSS138 stage can emit a
     * break (0x00) on the 5V side, which the host would read as garbage. */
    HAL_GPIO_WritePin(UART_TX_GPIO_PORT, UART_TX_PIN, GPIO_PIN_SET);

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

    /* Key matrix (Netlist_Schematic1_2026-09-08.tel, authoritative): 4 row
     * lines are outputs on PB0..PB3 (idle low, one driven high at a time),
     * 8 column lines are inputs on PB4..PB11 with 33k external pull-DOWNS
     * to GND (R9..R16 pin 2 = GND). No internal pull (it would fight the
     * pull-down: 3.3V over ~40k against 33k to GND parks the column at
     * ~1.5V, inside the undefined input zone). PC13/PC14/PC15 are free. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = KEY_ROW_PIN_MASK;
    HAL_GPIO_Init(KEY_ROW_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(KEY_ROW_GPIO_PORT, KEY_ROW_PIN_MASK, GPIO_PIN_RESET);

    /* Keyboard columns are inputs, held low by the external pull-downs. */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = KEY_COL_PIN_MASK;
    HAL_GPIO_Init(KEY_COL_GPIO_PORT, &gpio);

    /* SHT3x soft-I2C on PB15=SCL/PB14=SDA: open-drain, idle released high.
     * External 4.7k pull-ups to 3.3V are required; internal pull-up backs up. */
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = SHT3X_SCL_PIN | SHT3X_SDA_PIN;
    HAL_GPIO_Init(SHT3X_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(SHT3X_GPIO_PORT, SHT3X_SCL_PIN | SHT3X_SDA_PIN,
                      GPIO_PIN_SET);

    /* Defaults: chip selects idle high. LCM_RES parks LOW: the shared
     * LT7680/panel reset stays asserted from GPIO init until the boot
     * sequence releases it with the display blank already applied (see
     * hal_display_boot_blank). Releasing it here instead would let the
     * LT7680 stream its default colour-bar pattern for the whole slow
     * boot banner (visible bars/stale-pixel debris on the backlit panel). */
    HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LT7680_CS_GPIO_PORT, LT7680_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCM_RES_GPIO_PORT, LCM_RES_PIN, GPIO_PIN_RESET);

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
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART1_IRQn, 4u, 0u);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

#if LT7680_SPI_HW
static void init_spi1(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();
    SPI1->CR1 = 0;
    /* Mode 0, master, 8-bit, MSB first, /16 prescaler. PCLK2 = 72 MHz
     * gives approximately 4.5 MHz, a conservative diagnostic rate for the
     * LT7680/ST7701 control path. USART1 remains independently at 9600 baud. */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM |
                SPI_CR1_BR_1 | SPI_CR1_BR_0;
    SPI1->CR2 = 0;
    SPI1->CR1 |= SPI_CR1_SPE;
}
#endif

static void uart_put_byte(uint8_t b)
{
    /* Bounded wait: if the USART dies (clock/pin glitch), dropping the
     * byte must NOT hang the whole product -- every diagnostic print
     * funnels through here, and an unbounded wait turned any lower-level
     * fault into an unrecoverable silent freeze. */
    uint32_t spin = 0u;
    while ((USART1->SR & USART_SR_TXE) == 0u) {
        if (++spin > 500000u)
            return;
    }
    USART1->DR = b;
}

void hal_board_init(void)
{
    uart_rx_queue_init(&s_uart_rx);
    init_gpio();
    init_uart();
#if LT7680_SPI_HW
    init_spi1();
#endif
    lt7680_bus_init(&s_lt7680_io);
    sht3x_init(&s_sht3x_io, SHT3X_ADDR_DEFAULT);
}

void hal_display_early_reset_hold(void)
{
    /* Park the shared LT7680/panel reset line LOW before anything slow can
     * run (HSE lock, the ~150 ms of 9600-bd boot banner prints). From
     * power-on until this point the LT7680 has already left its own
     * power-on reset (PA3 floats high through the board pull-up while the
     * MCU boots) and streams its default register state: display ON with
     * the colour-bar test pattern at default panel timing -- seen as
     * rolling colour bars in part of the screen -- and a warm reboot
     * first leaks stale SDRAM pixels (broken green glyph debris). On an
     * MCU-only reset this must re-assert the hold. Runs before HAL_Init,
     * so no SysTick/HAL_Delay is available -- and none is needed. */
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = LCM_RES_PIN;
    /* Write ODR first: the pin leaves reset state already driving LOW. */
    HAL_GPIO_WritePin(LCM_RES_GPIO_PORT, LCM_RES_PIN, GPIO_PIN_RESET);
    HAL_GPIO_Init(LCM_RES_GPIO_PORT, &gpio);
}

bool hal_display_boot_blank(void)
{
    /* V16-verified reset timings: hold low >=10 ms; the LT7680 accepts SPI
     * traffic ~50 ms after release. During that settle its default register
     * state streams the colour-bar test pattern, so REG[12h]=0x08 must
     * become the FIRST accepted transaction -- not the step after
     * read_status/wait_ready. From +10 ms after release, retry the blank
     * write with readback verification every 5 ms; a too-early write is
     * simply ignored by the chip, and the loop gives up at 100 ms (the
     * caller's wait_ready still gates the rest of the flow). */
    uint32_t start;

    hal_rst(false);
    HAL_Delay(10u);
    hal_rst(true);
    start = HAL_GetTick();
    HAL_Delay(10u);
    for (;;)
    {
        uint8_t value = 0xFFu;

        if (lt7680_write_reg(0x12u, 0x08u) == LT7680_OK &&
            lt7680_read_reg(0x12u, &value) == LT7680_OK &&
            value == 0x08u)
        {
            break;
        }
        if ((uint32_t)(HAL_GetTick() - start) >= 100u)
        {
            return false;
        }
        HAL_Delay(5u);
    }
    /* Honour the remaining settle time so downstream traffic (status read,
     * panel init) keeps the verified post-release margin. */
    {
        uint32_t elapsed = (uint32_t)(HAL_GetTick() - start);

        if (elapsed < 60u)
        {
            HAL_Delay(60u - elapsed);
        }
    }
    return true;
}

bool hal_sht3x_read_milli(int32_t *temp_milli_c, int32_t *rh_milli_pct)
{
    if (temp_milli_c == NULL || rh_milli_pct == NULL) {
        return false;
    }
    return sht3x_measure_milli(temp_milli_c, rh_milli_pct) == SHT3X_OK;
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
#if PANEL_LANDSCAPE
    /* Landscape: ST7701S MADCTL (0x36) row/column exchange so each 960-wide
     * LT7680 scan line lands on the panel's long (960 px) axis. 0x60 = MX|MV
     * pairs with LT7680 VDIR=1 (the long-verified V16 combo). PIP was probed
     * (VDIR=0 + MADCTL 0xE0) but this die's PIP compositor does not output,
     * so the VDIR pairing stays. */
    panel_write_command(0x36u, (const uint8_t[]){0x60u}, 1u);
#endif
    HAL_Delay(120u);
    panel_write_command(0x35u, (const uint8_t[]){0x00u}, 1u);
    panel_write_command(0x3Au, (const uint8_t[]){0x66u}, 1u);
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
#if K2000_UART_LOG
    while (*text != '\0') {
        uart_put_byte((uint8_t)*text++);
    }
#else
    /* Host build: text is protocol garbage on the shared TX line. Drop it;
     * key bytes go through hal_uart_send() and are unaffected. */
    (void)text;
#endif
}

void hal_uart_send_hex8(uint8_t value)
{
#if K2000_UART_LOG
    static const char digits[] = "0123456789ABCDEF";

    uart_put_byte((uint8_t)digits[value >> 4]);
    uart_put_byte((uint8_t)digits[value & 0x0Fu]);
#else
    /* Same silence rule as hal_uart_send_text: hex dumps only ever
     * accompany dropped text lines. */
    (void)value;
#endif
}

int hal_uart_receive_byte(void)
{
    int value;
    /* pop() and the ISR both update queue indices during overflow recovery.
     * Keep that short transition atomic; disabling the whole IRQ (rather than
     * global interrupts) does not delay SysTick or display completion. */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    value = uart_rx_queue_pop(&s_uart_rx);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return value;
}

void hal_uart_rx_irq(void)
{
    uint32_t sr = USART1->SR;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE | USART_SR_FE)) != 0u) {
        /* Reading DR after SR clears RXNE and all receive error conditions. */
        uint8_t byte = (uint8_t)(USART1->DR & 0xFFu);
#if K2000_IDENTITY_REPLY
        /* V16-faithful: reply to the FIRST 0x0F poll only (its IRQ latches
         * a done-flag). The host polls twice at boot; a second reply lands
         * mid-stream and derails its parser (cal-prompt state). */
        if (byte == 0x0Fu) {
            static bool s_identity_sent;
            static const uint8_t identity[12] = {
                '2', '0', '0', '0', ' ', 'V', '1', '6',
                ' ', ' ', 0x80u, 0x00u
            };
            if (!s_identity_sent) {
                uint8_t i;
                s_identity_sent = true;
                for (i = 0u; i < sizeof(identity); i++) {
                    uart_put_byte(identity[i]);
                }
            }
        }
#endif
        uart_rx_queue_push_isr(&s_uart_rx, byte);
    }
}

uint32_t hal_uart_rx_overflow_count(void)
{
    return uart_rx_queue_overflow_count(&s_uart_rx);
}

bool hal_uart_rx_recovering(void)
{
    return uart_rx_queue_recovering(&s_uart_rx);
}

/* Logical row r = PB(r) (PB0->row0 ... PB3->row3, top to bottom).
 * If rows come back vertically flipped on the real board, swap this
 * table end-for-end (do NOT touch keypad.c or the ODS code table). */
static const uint16_t s_key_row_pin[KEYPAD_ROWS] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3,
};
/* Schematic breakout runs R9->PB11 ... R16->PB4, so logical column c
 * (R9=col0 ... R16=col7) maps to pins in reverse: col0=PB11 ...
 * col7=PB4. keypad.c's (row,col)->code table is written in logical
 * (row, column) coordinates and stays untouched. TEL cross-check:
 * row0 = SW1..SW8, row1 = SW11..SW16 (no PB11/PB10), row2 = SW17..SW24,
 * row3 = SW26..SW32 (no PB11). */
static const uint16_t s_key_col_pin[KEYPAD_COLS] = {
    GPIO_PIN_11, GPIO_PIN_10, GPIO_PIN_9, GPIO_PIN_8,
    GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_5, GPIO_PIN_4,
};

/* One full matrix sweep; returns the contact set as KEYPAD_CELL bits and
 * leaves the rows idle-low. Shared by the host path and the bench poll. */
static uint32_t keypad_sweep_mask(void)
{
    uint32_t mask = 0u;
    uint8_t row;

    for (row = 0u; row < KEYPAD_ROWS; row++) {
        uint8_t col;
        /* All rows idle low, then pull only this row high. */
        HAL_GPIO_WritePin(KEY_ROW_GPIO_PORT, KEY_ROW_PIN_MASK, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(KEY_ROW_GPIO_PORT, s_key_row_pin[row], GPIO_PIN_SET);
        /* Settle: 33k pull-down + wiring capacitance needs ~us before the
         * column input reflects the driven row. HAL call overhead already
         * covers most of it; a few NOPs close the gap deterministically. */
        for (volatile uint32_t n = 0u; n < 60u; n++) {
            __NOP();
        }
        for (col = 0u; col < KEYPAD_COLS; col++) {
            if (HAL_GPIO_ReadPin(KEY_COL_GPIO_PORT, s_key_col_pin[col]) ==
                GPIO_PIN_SET) {
                mask |= KEYPAD_CELL(row, col);
            }
        }
    }
    /* No key pressed: restore idle-low rows. */
    HAL_GPIO_WritePin(KEY_ROW_GPIO_PORT, KEY_ROW_PIN_MASK, GPIO_PIN_RESET);
    return mask;
}

int hal_keypad_read_mask(uint32_t *mask)
{
    uint32_t m = keypad_sweep_mask();
    uint8_t n = 0u;
    uint8_t i;
    if (mask == 0) {
        return -1;
    }
    *mask = m;
    for (i = 0u; i < KEYPAD_CELLS; i++) {
        if (((m >> i) & 1u) != 0u) {
            n++;
        }
    }
    return (int)n;
}

#if K2000_KEY_DEBUG
void hal_keypad_debug_poll(void)
{
    /* 32-bit contact mask, KEYPAD_CELL bits. Printed only on change so the
     * terminal stays readable while a key is held. */
    static uint32_t s_last_mask;
    static bool s_have_last;
    uint32_t mask = keypad_sweep_mask();
    uint8_t row;
    uint8_t col;

    if (s_have_last && mask == s_last_mask) {
        return;
    }
    s_have_last = true;
    s_last_mask = mask;
    uart_put_byte('K');
    uart_put_byte('E');
    uart_put_byte('Y');
    uart_put_byte('S');
    if (mask == 0u) {
        uart_put_byte(' ');
        uart_put_byte('-');
    } else {
        for (row = 0u; row < KEYPAD_ROWS; row++) {
            for (col = 0u; col < KEYPAD_COLS; col++) {
                if ((mask & KEYPAD_CELL(row, col)) != 0u) {
                    /* Named cell prints its key name ("FREQ"); unwired
                     * cells fall back to coordinates ("r3c0"). */
                    const char *name = keypad_name(row, col);
                    uart_put_byte(' ');
                    if (name[0] != '\0') {
                        uint8_t i = 0u;
                        while (name[i] != '\0') {
                            uart_put_byte((uint8_t)name[i]);
                            i++;
                        }
                    } else {
                        uart_put_byte('r');
                        uart_put_byte((uint8_t)('0' + row));
                        uart_put_byte('c');
                        uart_put_byte((uint8_t)('0' + col));
                    }
                }
            }
        }
    }
    uart_put_byte('\r');
    uart_put_byte('\n');
}
#endif
