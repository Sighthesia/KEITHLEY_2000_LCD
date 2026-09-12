#include "spi_config.h"

bool k2000_spi_config_missing(bool clock_enabled, uint32_t cr1,
                              uint32_t cr2)
{
    if (!clock_enabled || cr2 != K2000_SPI_CR2_EXPECTED)
        return true;
    return (cr1 & K2000_SPI_CR1_CONFIG_MASK) != K2000_SPI_CR1_EXPECTED;
}
