#include "lt7680_bus.h"

static const lt7680_bus_io_t *s_io;

void lt7680_bus_init(const lt7680_bus_io_t *io)
{
    s_io = io;
}

lt7680_status_t lt7680_delay_ms(uint32_t ms)
{
    if (s_io == 0 || s_io->delay_ms == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->delay_ms(ms);
    return LT7680_OK;
}

static lt7680_status_t xfer_byte(uint8_t out, uint8_t *in)
{
    if (s_io == 0 || s_io->spi_xfer == 0) {
        return LT7680_ERR_BUS;
    }
    if (in != 0) {
        *in = s_io->spi_xfer(out);
    } else {
        (void)s_io->spi_xfer(out);
    }
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
        if (xfer_byte(LT7680_SPI_CMD_READ_STATUS, 0) != LT7680_OK ||
            xfer_byte(0xFFu, &status) != LT7680_OK) {
            s_io->cs(true);
            return LT7680_ERR_BUS;
        }
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
    if (xfer_byte(LT7680_SPI_CMD_READ_STATUS, 0) != LT7680_OK ||
        xfer_byte(0xFFu, status) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    s_io->cs(true);
    return LT7680_OK;
}

lt7680_status_t lt7680_select_reg(uint8_t reg)
{
    if (s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->cs(false);
    if (xfer_byte(LT7680_SPI_CMD_WRITE_REG, 0) != LT7680_OK ||
        xfer_byte(reg, 0) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    s_io->cs(true);
    return LT7680_OK;
}

lt7680_status_t lt7680_write_reg(uint8_t reg, uint8_t value)
{
    if (s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_BUS;
    }
    s_io->cs(false);
    if (xfer_byte(LT7680_SPI_CMD_WRITE_REG, 0) != LT7680_OK ||
        xfer_byte(reg, 0) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    s_io->cs(true);
    return lt7680_write_data(&value, 1u);
}

lt7680_status_t lt7680_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0 || s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_PARAM;
    }
    s_io->cs(false);
    if (xfer_byte(LT7680_SPI_CMD_WRITE_REG, 0) != LT7680_OK ||
        xfer_byte(reg, 0) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    s_io->cs(true);

    s_io->cs(false);
    if (xfer_byte(LT7680_SPI_CMD_READ_REG, 0) != LT7680_OK ||
        xfer_byte(0xFFu, value) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    s_io->cs(true);
    return LT7680_OK;
}

lt7680_status_t lt7680_write_data(const uint8_t *data, uint32_t len)
{
    if (data == 0 || s_io == 0 || s_io->cs == 0) {
        return LT7680_ERR_PARAM;
    }
    s_io->cs(false);
    if (xfer_byte(LT7680_SPI_CMD_WRITE_DATA, 0) != LT7680_OK) {
        s_io->cs(true);
        return LT7680_ERR_BUS;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (xfer_byte(data[i], 0) != LT7680_OK) {
            s_io->cs(true);
            return LT7680_ERR_BUS;
        }
    }
    s_io->cs(true);
    return LT7680_OK;
}
