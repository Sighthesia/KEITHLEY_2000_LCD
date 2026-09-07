/*
 * Regression test for the K2000_DEMO_FEED pipeline: the synthetic frame
 * byte stream (0x0D 0x01 value+unit -> 0x09 -> 0x08 -> 0x07) must parse
 * through k2000_proto with zero unknown bytes, update the ui_model, and
 * feed the trend buffer for every unit in the demo table.
 *
 * The demo table below must stay in sync with:
 *   - s_demo_units in KEITHLEY_2000_LCD/Core/Src/main.c (source of truth)
 *   - DEMO_UNITS in sim/index.html
 */
#include <stdio.h>
#include <string.h>
#include "k2000_proto.h"
#include "reading_split.h"
#include "ui_model.h"
#include "trend_buffer.h"

static const char *const units[] = {
    "VDC","VAC","ADC","AAC","mVDC","mVAC","mADC","mAAC",
    "OHM","kOHM","MOHM","Hz","kHz","MHz","\xC2\xB0""C"
};
/* Expected model.unit after ui_model resistance normalization (OHM -> Ohm
 * sign); every other unit passes through unchanged. */
static const char *const expect_units[] = {
    "VDC","VAC","ADC","AAC","mVDC","mVAC","mADC","mAAC",
    "\xCE\xA9","k\xCE\xA9","M\xCE\xA9","Hz","kHz","MHz","\xC2\xB0""C"
};
static const unsigned char intd[] = {2,3,2,2,3,3,2,2,4,3,3,3,3,2,2};
static const unsigned char fracd[] = {5,5,5,5,5,5,5,5,4,4,4,3,3,3,3};
static const uint32_t lo[] = {20000,20000,10000,10000,10000,10000,10000,10000,
                              1000,1000,1000,1000,1000,1000,1000};
static const uint32_t hi[] = {1250000,7000000,300000,300000,100000,100000,200000,200000,
                              2000000,1000000,1000000,1000000,1000000,50000,50000};
static const uint8_t s09[] = {0x10,0x30,0x10,0x40,0x30,0x10,0x50,0x30,
                              0x10,0x50,0x30,0x10,0x00,0x40,0x30};
static const uint8_t s08[] = {0x04,0x02,0x01,0x12,0x04,0x01,0x04,0x02,
                              0x02,0x01,0x0C,0x04,0x14,0x02,0x01};

#define SAMPLES_PER_UNIT 40u
#define UNIT_COUNT (sizeof(units) / sizeof(units[0]))

static ui_model_t model;
static trend_buffer_t trend;
static uint32_t tick;
static int fields, statuses, unknowns, trend_adds;

static void u32_to_padded(char *out, uint32_t v, uint8_t digits)
{
    uint8_t i = digits;
    while (i > 0u) { out[--i] = (char)('0' + (v % 10u)); v /= 10u; }
}

static void format_value(uint8_t unit_index, uint32_t sample, char *out)
{
    uint32_t div = 1u;
    uint32_t ph = sample % SAMPLES_PER_UNIT;
    uint32_t tri = ph < SAMPLES_PER_UNIT / 2u ? ph : (SAMPLES_PER_UNIT - ph);
    uint32_t mant = lo[unit_index] +
                    (hi[unit_index] - lo[unit_index]) * tri * 2u / SAMPLES_PER_UNIT;
    uint8_t i;
    for (i = 0u; i < fracd[unit_index]; i++) div *= 10u;
    u32_to_padded(out, mant / div, intd[unit_index]);
    out[intd[unit_index]] = '.';
    u32_to_padded(out + intd[unit_index] + 1u, mant % div, fracd[unit_index]);
    out[intd[unit_index] + fracd[unit_index] + 1u] = '\0';
}

static void on_event(const k2000_event_t *evt)
{
    char num[32], unit[16];
    uint8_t num_len, unit_len;
    if (evt->type == K2000_EVT_FIELD)
    {
        fields++;
        reading_split(evt->field.value, evt->field.value_len, num, &num_len,
                      unit, &unit_len);
        ui_model_apply_reading(&model, num, num_len, unit, unit_len, 0);
        if (trend_buffer_add(&trend, tick, num, unit)) trend_adds++;
    }
    else if (evt->type == K2000_EVT_STATUS)
    {
        statuses++;
        ui_model_apply_status(&model, evt->status_tag, evt->status_value);
    }
    else
    {
        unknowns++;
    }
}

static void feed_unit(const char *u)
{
    const char *p;
    if (u[0] == (char)0xC2)
    {
        if ((unsigned char)u[1] == 0xB5) { k2000_proto_feed(K2000_TAG_SYM_MICRO); u += 2; }
        else if ((unsigned char)u[1] == 0xB0) { k2000_proto_feed(K2000_TAG_SYM_DEGREE); u += 2; }
    }
    for (p = u; *p; p++) k2000_proto_feed((uint8_t)*p);
}

static int check_model(uint8_t ui, uint32_t sample)
{
    uint32_t ph = sample % SAMPLES_PER_UNIT;
    uint32_t tri = ph < SAMPLES_PER_UNIT / 2u ? ph : (SAMPLES_PER_UNIT - ph);
    uint32_t div = 1u;
    uint8_t i;
    uint32_t expect = lo[ui] + (hi[ui] - lo[ui]) * tri * 2u / SAMPLES_PER_UNIT;
    char expv[16];
    for (i = 0u; i < fracd[ui]; i++) div *= 10u;
    u32_to_padded(expv, expect / div, intd[ui]);
    expv[intd[ui]] = '.';
    u32_to_padded(expv + intd[ui] + 1u, expect % div, fracd[ui]);
    expv[intd[ui] + fracd[ui] + 1u] = '\0';
    if (strcmp(model.value, expv) != 0)
    {
        printf("FAIL unit %s sample %lu: model.value=%s expect %s\n",
               units[ui], (unsigned long)sample, model.value, expv);
        return 1;
    }
    if (strcmp(model.unit, expect_units[ui]) != 0)
    {
        printf("FAIL unit %s: model.unit=%s\n", units[ui], model.unit);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint32_t sample;
    int fails = 0;
    k2000_proto_init(&(k2000_proto_cb_t){on_event, 0});
    ui_model_init(&model);
    trend_buffer_init(&trend);
    for (sample = 0; sample < UNIT_COUNT * SAMPLES_PER_UNIT + 20u; sample++)
    {
        uint8_t ui = (uint8_t)((sample / SAMPLES_PER_UNIT) % UNIT_COUNT);
        char text[16];
        const char *p;
        format_value(ui, sample, text);
        tick = sample * 100u;
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        for (p = text; *p; p++) k2000_proto_feed((uint8_t)*p);
        feed_unit(units[ui]);
        k2000_proto_feed(K2000_TAG_STATUS_REL);
        k2000_proto_feed(s09[ui]);
        k2000_proto_feed(K2000_TAG_STATUS_HOLD);
        k2000_proto_feed(s08[ui]);
        k2000_proto_feed(K2000_TAG_STATUS_SHIFT);
        k2000_proto_feed((ui % 4u == 3u) ? 0x20u : 0x00u);
        fails += check_model(ui, sample);
    }
    printf("fields=%d statuses=%d unknowns=%d trend_adds=%d\n",
           fields, statuses, unknowns, trend_adds);
    printf("model.value=%s model.unit=%s any_message=%d\n",
           model.value, model.unit, model.any_message ? 1 : 0);
    printf("trend dim=%d has_sample=%d\n", (int)trend.dimension,
           trend.has_sample ? 1 : 0);
    if (fields != 620) { printf("FAIL field count %d != 620\n", fields); fails++; }
    if (statuses != 620 * 3) { printf("FAIL status count %d != %d\n", statuses, 620 * 3); fails++; }
    if (unknowns != 0) { printf("FAIL unknowns %d\n", unknowns); fails++; }
    if (trend_adds != 620) { printf("FAIL trend adds %d != 620\n", trend_adds); fails++; }
    if (trend.dimension != TREND_DIM_VOLTAGE) { printf("FAIL last dim %d != VOLTAGE\n", (int)trend.dimension); fails++; }
    printf(fails == 0 ? "DEMO PIPELINE OK\n" : "DEMO PIPELINE FAILED (%d)\n", fails);
    return fails != 0;
}