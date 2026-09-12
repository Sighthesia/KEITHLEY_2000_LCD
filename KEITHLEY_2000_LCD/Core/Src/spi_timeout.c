#include "spi_timeout.h"

bool k2000_timeout_expired(uint32_t start_tick, uint32_t now_tick,
                           uint32_t limit_ms)
{
    return (uint32_t)(now_tick - start_tick) >= limit_ms;
}
