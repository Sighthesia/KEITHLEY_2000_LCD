#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Wrap-safe millisecond deadline used by hardware polling loops. */
bool k2000_timeout_expired(uint32_t start_tick, uint32_t now_tick,
                           uint32_t limit_ms);
