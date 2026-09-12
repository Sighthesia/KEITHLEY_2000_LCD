#pragma once

#include <stdbool.h>
#include <stdint.h>

/* SPI1: Mode 0, master, software NSS, /16, enabled. */
#define K2000_SPI_CR1_EXPECTED 0x0000035Cu
#define K2000_SPI_CR1_CONFIG_MASK 0x000007FFu
#define K2000_SPI_CR2_EXPECTED 0u

bool k2000_spi_config_missing(bool clock_enabled, uint32_t cr1,
                              uint32_t cr2);
