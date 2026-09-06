#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sht3x.h"

/* Scripted open-drain line: sda_read() serves a queued bit per call so the
 * full measure transaction runs against a canned slave frame. */
#define READ_QUEUE_MAX 64u

static bool s_read_queue[READ_QUEUE_MAX];
static uint8_t s_read_count;
static uint8_t s_read_index;

static void queue_reset(void)
{
    s_read_count = 0u;
    s_read_index = 0u;
}

static void queue_push(bool bit)
{
    assert(s_read_count < READ_QUEUE_MAX);
    s_read_queue[s_read_count++] = bit;
}

static void stub_sda_high(void)
{
}

static void stub_sda_low(void)
{
}

static void stub_scl_high(void)
{
}

static void stub_scl_low(void)
{
}

static bool stub_sda_read(void)
{
    assert(s_read_index < s_read_count);
    return s_read_queue[s_read_index++];
}

static void stub_delay_us(uint32_t us)
{
    (void)us;
}

static void stub_delay_ms(uint32_t ms)
{
    (void)ms;
}

static const sht3x_io_t s_stub_io = {
    .sda_high = stub_sda_high,
    .sda_low = stub_sda_low,
    .scl_high = stub_scl_high,
    .scl_low = stub_scl_low,
    .sda_read = stub_sda_read,
    .delay_us = stub_delay_us,
    .delay_ms = stub_delay_ms,
};

/* Queue one ACK (false) plus 6 frame bytes MSB-first. */
static void queue_frame(const uint8_t frame[6])
{
    /* 3 ACKs for command write + 1 ACK for read address. */
    queue_push(false);
    queue_push(false);
    queue_push(false);
    queue_push(false);
    for (uint8_t i = 0u; i < 6u; i++) {
        for (int8_t bit = 7; bit >= 0; bit--) {
            queue_push((frame[i] & (uint8_t)(1u << bit)) != 0u);
        }
    }
}

int main(void)
{
    uint8_t vec[2] = {0xBEu, 0xEFu};
    uint16_t temp_ticks = 0u, rh_ticks = 0u;
    int32_t temp_milli = 0, rh_milli = 0;

    /* CRC reference vector from the Sensirion datasheet. */
    assert(sht3x_crc8(vec, 2u) == 0x92u);
    assert(sht3x_crc8(NULL, 2u) == 0xFFu);

    /* Integer conversion spot checks (official formulas). */
    assert(sht3x_temp_ticks_to_milli(0x0000u) == -45000);
    assert(sht3x_temp_ticks_to_milli(0x8000u) == 42500);
    assert(sht3x_rh_ticks_to_milli(0x0000u) == 0);
    assert(sht3x_rh_ticks_to_milli(0x8000u) == 50000);
    assert(sht3x_rh_ticks_to_milli(0xFFFFu) <= 100000);

    /* Parameter guards without any bus traffic. */
    sht3x_init(NULL, SHT3X_ADDR_DEFAULT);
    assert(sht3x_measure_ticks(NULL, &rh_ticks) == SHT3X_ERR_PARAM);
    assert(sht3x_measure_ticks(&temp_ticks, NULL) == SHT3X_ERR_PARAM);
    assert(sht3x_measure_milli(NULL, &rh_milli) == SHT3X_ERR_PARAM);

    /* Full transaction against a canned 0x8000/0x8000 frame. */
    sht3x_init(&s_stub_io, SHT3X_ADDR_DEFAULT);
    {
        uint8_t frame[6] = {0x80u, 0x00u, 0u, 0x80u, 0x00u, 0u};

        frame[2] = sht3x_crc8(frame, 2u);
        frame[5] = sht3x_crc8(frame + 3u, 2u);
        queue_reset();
        queue_frame(frame);
        assert(sht3x_measure_ticks(&temp_ticks, &rh_ticks) == SHT3X_OK);
        assert(temp_ticks == 0x8000u);
        assert(rh_ticks == 0x8000u);
        assert(s_read_index == s_read_count);
    }

    /* Milli wrapper on the same frame. */
    {
        uint8_t frame[6] = {0x80u, 0x00u, 0u, 0x80u, 0x00u, 0u};

        frame[2] = sht3x_crc8(frame, 2u);
        frame[5] = sht3x_crc8(frame + 3u, 2u);
        queue_reset();
        queue_frame(frame);
        assert(sht3x_measure_milli(&temp_milli, &rh_milli) == SHT3X_OK);
        assert(temp_milli == 42500);
        assert(rh_milli == 50000);
    }

    /* Corrupted CRC must be rejected. */
    {
        uint8_t frame[6] = {0x80u, 0x00u, 0u, 0x80u, 0x00u, 0u};

        frame[2] = (uint8_t)(sht3x_crc8(frame, 2u) ^ 0x01u);
        frame[5] = sht3x_crc8(frame + 3u, 2u);
        queue_reset();
        queue_frame(frame);
        assert(sht3x_measure_ticks(&temp_ticks, &rh_ticks) == SHT3X_ERR_CRC);
    }

    return 0;
}
