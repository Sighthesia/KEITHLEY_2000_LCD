#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SHT3x soft-I2C driver, single-shot polling only.
 * Reference: Sensirion/embedded-i2c-sht3x (commands, CRC, conversion)
 * and henriheimann/stm32-hal-sht3x (CRC8 poly 0x31, init 0xFF).
 * Only the subset needed for temperature/humidity readout is kept so the
 * 64 KiB Flash budget is not blown: soft-reset + 0x2416 low-repeatability
 * no-stretch measurement + CRC + integer (milli) conversion. No heater,
 * no periodic mode, no float. Low repeatability (≈4 ms) is plenty for an
 * ambient header field and keeps the blocking read far below the 33 ms
 * display budget (high repeatability costs ≈15 ms and stretched one
 * reading frame every period -- visible as a longer interval in
 * slow-motion footage). */

#define SHT3X_ADDR_DEFAULT 0x44u
#define SHT3X_ADDR_ALT 0x45u

/* Single-shot low-repeatability, clock-stretching disabled. */
#define SHT3X_CMD_MEASURE_L 0x2416u
/* Single-shot high-repeatability, clock-stretching disabled (kept for
 * reference; the runtime uses MEASURE_L to stay inside the frame budget). */
#define SHT3X_CMD_MEASURE_H 0x2400u
#define SHT3X_CMD_SOFT_RESET 0x30A2u

typedef enum {
    SHT3X_OK = 0,
    SHT3X_ERR_PARAM,
    SHT3X_ERR_ACK,
    SHT3X_ERR_CRC,
} sht3x_status_t;

/* Bit-bang line operations. SDA/SCL are open-drain with external 4.7k
 * pull-ups: *_high releases the line, *_low drives it low. */
typedef struct {
    void (*sda_high)(void);
    void (*sda_low)(void);
    void (*scl_high)(void);
    void (*scl_low)(void);
    bool (*sda_read)(void);
    void (*delay_us)(uint32_t us);
    void (*delay_ms)(uint32_t ms);
} sht3x_io_t;

void sht3x_init(const sht3x_io_t *io, uint8_t address);
uint8_t sht3x_crc8(const uint8_t *data, uint8_t len);

/* Integer conversion (official formulas, no float):
 *   temp_mC  = ((21875 * ticks) >> 13) - 45000   [milli degrees C]
 *   rh_milli = ((12500 * ticks) >> 13)           [milli %RH] */
int32_t sht3x_temp_ticks_to_milli(uint16_t ticks);
int32_t sht3x_rh_ticks_to_milli(uint16_t ticks);

sht3x_status_t sht3x_soft_reset(void);
sht3x_status_t sht3x_measure_ticks(uint16_t *temp_ticks, uint16_t *rh_ticks);
sht3x_status_t sht3x_measure_milli(int32_t *temp_milli_c,
                                   int32_t *rh_milli_pct);
