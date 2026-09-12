#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * STM32F103 @ 72 MHz: 25,000 volatile register polls are approximately
 * 1.0-2.1 ms (about 3-6 CPU cycles per poll). This is deliberately a count,
 * not a HAL_GetTick deadline, so a stopped SysTick cannot wedge SPI forever.
 */
#define K2000_SPI_POLL_LIMIT 25000u

bool k2000_spi_poll_expired(volatile uint32_t *polls);

/* Wrap-safe millisecond deadline retained for non-SPI host-side logic. */
bool k2000_timeout_expired(uint32_t start_tick, uint32_t now_tick,
                           uint32_t limit_ms);
