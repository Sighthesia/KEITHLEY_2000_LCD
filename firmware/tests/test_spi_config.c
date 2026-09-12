#include <assert.h>

#include "spi_config.h"

int main(void)
{
    assert(!k2000_spi_config_missing(true, K2000_SPI_CR1_EXPECTED,
                                     K2000_SPI_CR2_EXPECTED));
    assert(k2000_spi_config_missing(false, K2000_SPI_CR1_EXPECTED,
                                    K2000_SPI_CR2_EXPECTED));
    assert(k2000_spi_config_missing(true, K2000_SPI_CR1_EXPECTED & ~0x40u,
                                    K2000_SPI_CR2_EXPECTED));
    assert(k2000_spi_config_missing(true, K2000_SPI_CR1_EXPECTED,
                                    0x00000001u));
    assert(k2000_spi_config_missing(true, 0x00000002u, 0u));
    return 0;
}
