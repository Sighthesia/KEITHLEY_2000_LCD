#include <stdbool.h>
#include <stdint.h>

#include "lt7680_bus.h"

static uint8_t hal_spi_xfer(uint8_t byte)
{
    (void)byte;
    return 0xFFu;
}

static void hal_cs(bool level)
{
    (void)level;
}

static void hal_rst(bool level)
{
    (void)level;
}

static void hal_delay_ms(uint32_t ms)
{
    (void)ms;
}

static const lt7680_bus_io_t s_io = {
    .cs = hal_cs,
    .rst = hal_rst,
    .spi_xfer = hal_spi_xfer,
    .delay_ms = hal_delay_ms,
};

void hal_lt7680_init(void)
{
    lt7680_bus_init(&s_io);
}
