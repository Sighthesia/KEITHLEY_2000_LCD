#include "sht3x.h"

#define SHT3X_HALF_US 5u
#define SHT3X_MEAS_MS 4u

static const sht3x_io_t *s_io = NULL;
static uint8_t s_addr = SHT3X_ADDR_DEFAULT;

void sht3x_init(const sht3x_io_t *io, uint8_t address)
{
    s_io = io;
    s_addr = (address == 0u) ? SHT3X_ADDR_DEFAULT : address;
}

uint8_t sht3x_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFu;

    if (data == NULL) {
        return crc;
    }
    for (uint8_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x31u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

int32_t sht3x_temp_ticks_to_milli(uint16_t ticks)
{
    return ((int32_t)(((int32_t)21875 * (int32_t)ticks) >> 13)) - 45000;
}

int32_t sht3x_rh_ticks_to_milli(uint16_t ticks)
{
    int32_t rh = (int32_t)(((int32_t)12500 * (int32_t)ticks) >> 13);

    if (rh < 0) {
        rh = 0;
    }
    if (rh > 100000) {
        rh = 100000;
    }
    return rh;
}

static void i2c_half(void)
{
    s_io->delay_us(SHT3X_HALF_US);
}

static void i2c_start(void)
{
    s_io->sda_high();
    s_io->scl_high();
    i2c_half();
    s_io->sda_low();
    i2c_half();
    s_io->scl_low();
    i2c_half();
}

static void i2c_stop(void)
{
    s_io->sda_low();
    i2c_half();
    s_io->scl_high();
    i2c_half();
    s_io->sda_high();
    i2c_half();
}

/* Returns true when the slave ACKed (pulled SDA low). */
static bool i2c_write_byte(uint8_t byte)
{
    bool ack;

    for (int8_t bit = 7; bit >= 0; bit--) {
        if ((byte & (uint8_t)(1u << bit)) != 0u) {
            s_io->sda_high();
        } else {
            s_io->sda_low();
        }
        i2c_half();
        s_io->scl_high();
        i2c_half();
        s_io->scl_low();
        i2c_half();
    }
    s_io->sda_high();
    i2c_half();
    s_io->scl_high();
    i2c_half();
    ack = !s_io->sda_read();
    s_io->scl_low();
    i2c_half();
    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t byte = 0u;

    s_io->sda_high();
    for (int8_t bit = 7; bit >= 0; bit--) {
        i2c_half();
        s_io->scl_high();
        i2c_half();
        if (s_io->sda_read()) {
            byte |= (uint8_t)(1u << bit);
        }
        s_io->scl_low();
        i2c_half();
    }
    if (ack) {
        s_io->sda_low();
    } else {
        s_io->sda_high();
    }
    i2c_half();
    s_io->scl_high();
    i2c_half();
    s_io->scl_low();
    i2c_half();
    s_io->sda_high();
    return byte;
}

static sht3x_status_t write_command(uint16_t command)
{
    uint8_t hi = (uint8_t)(command >> 8);
    uint8_t lo = (uint8_t)(command & 0xFFu);

    if (s_io == NULL) {
        return SHT3X_ERR_PARAM;
    }
    i2c_start();
    if (!i2c_write_byte((uint8_t)(s_addr << 1))) {
        i2c_stop();
        return SHT3X_ERR_ACK;
    }
    if (!i2c_write_byte(hi)) {
        i2c_stop();
        return SHT3X_ERR_ACK;
    }
    if (!i2c_write_byte(lo)) {
        i2c_stop();
        return SHT3X_ERR_ACK;
    }
    i2c_stop();
    return SHT3X_OK;
}

sht3x_status_t sht3x_soft_reset(void)
{
    sht3x_status_t st = write_command(SHT3X_CMD_SOFT_RESET);

    if (st != SHT3X_OK) {
        return st;
    }
    s_io->delay_ms(2u);
    return SHT3X_OK;
}

sht3x_status_t sht3x_measure_ticks(uint16_t *temp_ticks, uint16_t *rh_ticks)
{
    sht3x_status_t st;
    uint8_t buf[6];

    if (s_io == NULL || temp_ticks == NULL || rh_ticks == NULL) {
        return SHT3X_ERR_PARAM;
    }
    st = write_command(SHT3X_CMD_MEASURE_L);
    if (st != SHT3X_OK) {
        return st;
    }
    s_io->delay_ms(SHT3X_MEAS_MS);

    i2c_start();
    if (!i2c_write_byte((uint8_t)((s_addr << 1) | 0x01u))) {
        i2c_stop();
        return SHT3X_ERR_ACK;
    }
    for (uint8_t i = 0u; i < 6u; i++) {
        buf[i] = i2c_read_byte(i < 5u);
    }
    i2c_stop();

    if (sht3x_crc8(buf, 2u) != buf[2] ||
        sht3x_crc8(buf + 3u, 2u) != buf[5]) {
        return SHT3X_ERR_CRC;
    }
    *temp_ticks = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    *rh_ticks = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);
    return SHT3X_OK;
}

sht3x_status_t sht3x_measure_milli(int32_t *temp_milli_c,
                                   int32_t *rh_milli_pct)
{
    uint16_t temp_ticks = 0u, rh_ticks = 0u;
    sht3x_status_t st;

    if (temp_milli_c == NULL || rh_milli_pct == NULL) {
        return SHT3X_ERR_PARAM;
    }
    st = sht3x_measure_ticks(&temp_ticks, &rh_ticks);
    if (st != SHT3X_OK) {
        return st;
    }
    *temp_milli_c = sht3x_temp_ticks_to_milli(temp_ticks);
    *rh_milli_pct = sht3x_rh_ticks_to_milli(rh_ticks);
    return SHT3X_OK;
}
