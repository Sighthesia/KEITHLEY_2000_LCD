#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LT7680_SPI_CMD_WRITE_REG 0x00u
#define LT7680_SPI_CMD_WRITE_DATA 0x80u
#define LT7680_SPI_CMD_READ_STATUS 0x40u
#define LT7680_SPI_CMD_READ_REG 0xC0u
#define LT7680_SPI_CMD_READ_DATA 0xE0u

#define LT7680_STATUS_CORE_BUSY 0x08u
#define LT7680_STATUS_BUSY LT7680_STATUS_CORE_BUSY

typedef enum {
    LT7680_OK = 0,
    LT7680_ERR_TIMEOUT,
    LT7680_ERR_BUS,
    LT7680_ERR_PARAM,
    LT7680_ERR_BUSY,
    LT7680_ERR_UNSUPPORTED,
} lt7680_status_t;

typedef struct {
    void (*cs)(bool level);
    void (*rst)(bool level);
    uint8_t (*spi_xfer)(uint8_t byte);
    void (*delay_ms)(uint32_t ms);
} lt7680_bus_io_t;

void lt7680_bus_init(const lt7680_bus_io_t *io);
lt7680_status_t lt7680_delay_ms(uint32_t ms);
lt7680_status_t lt7680_reset(void);
lt7680_status_t lt7680_wait_ready(uint32_t timeout_ms);
lt7680_status_t lt7680_read_reg(uint8_t reg, uint8_t *value);
lt7680_status_t lt7680_select_reg(uint8_t reg);
lt7680_status_t lt7680_write_reg(uint8_t reg, uint8_t value);
lt7680_status_t lt7680_read_status(uint8_t *status);
lt7680_status_t lt7680_write_data(const uint8_t *data, uint32_t len);
