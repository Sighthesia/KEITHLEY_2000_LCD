#include <assert.h>
#include <stdint.h>

#include "spi_timeout.h"

int main(void)
{
    volatile uint32_t polls = K2000_SPI_POLL_LIMIT - 2u;

    assert(!k2000_spi_poll_expired(&polls));
    assert(polls == K2000_SPI_POLL_LIMIT - 1u);
    assert(k2000_spi_poll_expired(&polls));
    assert(polls == K2000_SPI_POLL_LIMIT);
    assert(k2000_spi_poll_expired(0));

    assert(!k2000_timeout_expired(100u, 101u, 2u));
    assert(k2000_timeout_expired(100u, 102u, 2u));
    /* HAL ticks wrap at uint32_t; the subtraction remains well-defined. */
    assert(!k2000_timeout_expired(UINT32_MAX - 1u, UINT32_MAX, 2u));
    assert(k2000_timeout_expired(UINT32_MAX - 1u, 1u, 2u));
    return 0;
}
