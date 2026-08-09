#include "lt7680_bus.h"

static const lt7680_bus_io_t *s_io;

void lt7680_bus_init(const lt7680_bus_io_t *io)
{
    s_io = io;
}

static uint8_t xfer_byte(uint8_t out)
{
    return s_io->spi_xfer(out);
}

static lt7680_status_t xfer_byte_st(uint8_t out, uint8_t *in)
{
    if (s_io == 0 || s_io->spi_xfer == 0) {
        return LT7680_ERR_BUS;
    }
    *in = s_io->spi_xfer(out);
    return LT7680_OK;
}

lt7680_status_t lt7680_reset(void)
{
    if (s_io == 0 || s_io->rst == 0 || s_io->delay_ms == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->rst(false);
    s_io->delay_ms(10);
    s_io->rst(true);
    s_io->delay_ms(50);
    return LT7680_OK;
}

lt7680_status_t lt7680_wait_ready(uint32_t timeout_ms)
{
    uint8_t status;
    uint32_t waited = 0;
    if (s_io == 0 || s_io->cs == 0 || s_io->delay_ms == 0) {
        return LT7680_ERR_BUS;
    }
    for (;;) {
        s_io->cs(false);
        (void)xfer_byte_st(LT7680_SPI_CMD_READ_STATUS, &status);
        (void)xfer_byte_st(0xFFu, &status);
        s_io->cs(true);
        if ((status & LT7680_STATUS_BUSY) == 0u) {
            return LT7680_OK;
        }
        if (waited >= timeout_ms) {
            return LT7680_ERR_TIMEOUT;
        }
        s_io->delay_ms(1);
        waited++;
    }
}

lt7680_status_t lt7680_read_status(uint8_t *status)
{
    if (status == 0 || s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_PARAM;
    }
    s_io->cs(false);
    (void)xfer_byte_st(LT7680_SPI_CMD_READ_STATUS, status);
    (void)xfer_byte_st(0xFFu, status);
    s_io->cs(true);
    return LT7680_OK;
}

/* Command write: select register address (REG[00h]..REG[FFh]). */
static lt7680_status_t cmd_write(uint8_t reg)
{
    if (s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->cs(false);
    (void)xfer_byte(LT7680_SPI_CMD_WRITE_REG);
    (void)xfer_byte(reg);
    s_io->cs(true);
    return LT7680_OK;
}

lt7680_status_t lt7680_write_reg(uint8_t reg, uint8_t value)
{
    lt7680_status_t st;
    st = cmd_write(reg);
    if (st != LT7680_OK) {
        return st;
    }
    if (s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->cs(false);
    (void)xfer_byte(LT7680_SPI_CMD_WRITE_DATA);
    (void)xfer_byte(value);
    s_io->cs(true);
    return LT7680_OK;
}

/* Write consecutive bytes to the register/memory port (multi-byte
 * registers are written LSB first, e.g. REG[E2h] then REG[E3h]). */
lt7680_status_t lt7680_write_reg_bytes(uint8_t reg, const uint8_t *data,
                                       uint8_t len)
{
    lt7680_status_t st;
    uint8_t i;

    if (data == 0) {
        return LT7680_ERR_PARAM;
    }
    st = cmd_write(reg);
    if (st != LT7680_OK) {
        return st;
    }
    if (s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->cs(false);
    (void)xfer_byte(LT7680_SPI_CMD_WRITE_DATA);
    for (i = 0; i < len; i++) {
        (void)xfer_byte(data[i]);
    }
    s_io->cs(true);
    return LT7680_OK;
}

lt7680_status_t lt7680_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0 || s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_PARAM;
    }
    s_io->cs(false);
    (void)xfer_byte(LT7680_SPI_CMD_WRITE_REG);
    (void)xfer_byte(reg);
    (void)xfer_byte(LT7680_SPI_CMD_READ_REG);
    (void)xfer_byte(0xFFu);
    s_io->cs(true);
    *value = 0;
    return LT7680_OK;
}

/* Write Display RAM pixel data. Each byte is preceded by the data-write
 * command byte; CS is held low for the whole burst. */
lt7680_status_t lt7680_write_data(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (data == 0 || s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_PARAM;
    }
    s_io->cs(false);
    for (i = 0; i < len; i++) {
        (void)xfer_byte(LT7680_SPI_CMD_WRITE_DATA);
        (void)xfer_byte(data[i]);
    }
    s_io->cs(true);
    return LT7680_OK;
}
