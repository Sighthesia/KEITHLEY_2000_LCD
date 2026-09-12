#include <assert.h>
#include <stdint.h>

#include "spi_timeout.h"

int main(void)
{
    assert(!k2000_timeout_expired(100u, 101u, 2u));
    assert(k2000_timeout_expired(100u, 102u, 2u));
    /* HAL ticks wrap at uint32_t; the subtraction remains well-defined. */
    assert(!k2000_timeout_expired(UINT32_MAX - 1u, UINT32_MAX, 2u));
    assert(k2000_timeout_expired(UINT32_MAX - 1u, 1u, 2u));
    return 0;
}
