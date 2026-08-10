#pragma once

#include <stdbool.h>
#include <stdint.h>

/* LT768x serial (4-wire SPI) host interface:
 *   first byte of every SPI frame is the command byte (bit7 = A0,
 *   bit6 = RW#), followed by the payload byte(s):
 *   0x00: write register address (payload = 8-bit REG address)
 *   0x80: write data (payload = register value or Display RAM data)
 *   0x40: read status register (payload = dummy, returns status)
 *   0xC0: read register data (payload = dummy, returns value)
 * Registers are 8-bit addressed (REG[00h]..REG[FFh]); multi-byte
 * registers are written LSB first, see datasheet V4.2 section 7.2. */
#define LT7680_SPI_CMD_WRITE_REG 0x00u
#define LT7680_SPI_CMD_WRITE_DATA 0x80u
#define LT7680_SPI_CMD_READ_STATUS 0x40u
#define LT7680_SPI_CMD_READ_REG 0xC0u
#define LT7680_SPI_CMD_READ_DATA 0xE0u

/* Status register (read via LT7680_SPI_CMD_READ_STATUS) bit masks.
 * Datasheet V4.2, section 19.1. */
#define LT7680_STATUS_MEMWR_FIFO_FULL 0x80u
#define LT7680_STATUS_MEMRD_FIFO_FULL 0x20u
#define LT7680_STATUS_CORE_BUSY 0x08u
#define LT7680_STATUS_DPRAM_READY 0x04u
#define LT7680_STATUS_INHIBIT 0x02u
#define LT7680_STATUS_BUSY (LT7680_STATUS_CORE_BUSY | LT7680_STATUS_INHIBIT)

typedef enum {
    LT7680_OK = 0,
    LT7680_ERR_TIMEOUT,
    LT7680_ERR_BUS,
    LT7680_ERR_PARAM,
    LT7680_ERR_BUSY,
} lt7680_status_t;

typedef struct {
    void (*cs)(bool level);
    void (*rst)(bool level);
    uint8_t (*spi_xfer)(uint8_t byte);
    void (*delay_ms)(uint32_t ms);
} lt7680_bus_io_t;

void lt7680_bus_init(const lt7680_bus_io_t *io);
lt7680_status_t lt7680_reset(void);
lt7680_status_t lt7680_wait_ready(uint32_t timeout_ms);
lt7680_status_t lt7680_read_reg(uint8_t reg, uint8_t *value);
lt7680_status_t lt7680_select_reg(uint8_t reg);
lt7680_status_t lt7680_write_reg(uint8_t reg, uint8_t value);
lt7680_status_t lt7680_write_reg_bytes(uint8_t reg, const uint8_t *data,
                                       uint8_t len);
lt7680_status_t lt7680_read_status(uint8_t *status);
lt7680_status_t lt7680_write_data(const uint8_t *data, uint32_t len);
