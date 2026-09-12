#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "lt7680_bus.h"

static bool s_failed;
static unsigned s_xfers;

static uint8_t fake_spi(uint8_t byte)
{
    (void)byte;
    s_xfers++;
    return 0x00u;
}

static bool fake_spi_failed(void)
{
    bool failed = s_failed;
    s_failed = false;
    return failed;
}

static void fake_cs(bool level)
{
    (void)level;
}

static void fake_delay(uint32_t ms)
{
    (void)ms;
}

int main(void)
{
    lt7680_bus_io_t io = {
        .cs = fake_cs,
        .rst = 0,
        .spi_xfer = fake_spi,
        .spi_failed = fake_spi_failed,
        .delay_ms = fake_delay,
    };
    uint8_t status = 0u;

    lt7680_bus_init(&io);
    s_failed = true;
    assert(lt7680_read_status(&status) == LT7680_ERR_TIMEOUT);
    assert(s_xfers == 1u);

    /* A failed transaction is contained; the next transaction can recover. */
    assert(lt7680_read_status(&status) == LT7680_OK);
    assert(s_xfers == 3u);
    return 0;
}
