#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TREND_BUCKET_MS 20u
#define TREND_BUCKET_COUNT 500u
#define TREND_WINDOW_MS (TREND_BUCKET_MS * TREND_BUCKET_COUNT)
#define TREND_MAX_COLUMNS 240u
#define TREND_UNIT_ID_MAX 8u

typedef enum {
    TREND_DIM_NONE = 0,
    TREND_DIM_VOLTAGE,
    TREND_DIM_CURRENT,
    TREND_DIM_RESISTANCE,
    TREND_DIM_FREQUENCY,
    TREND_DIM_TIME,
    TREND_DIM_TEMPERATURE,
} trend_dimension_t;

typedef struct {
    float minimum;
    float maximum;
    uint16_t x;
    bool occupied;
} trend_column_t;

typedef struct {
    float minimum[TREND_BUCKET_COUNT];
    float maximum[TREND_BUCKET_COUNT];
    uint8_t occupied[(TREND_BUCKET_COUNT + 7u) / 8u];
    uint32_t newest_bucket;
    uint32_t first_sample_ms;   /* 0 = no sample since reset */
    uint32_t last_sample_ms;
    trend_dimension_t dimension;
    char unit_identity[TREND_UNIT_ID_MAX];
    bool has_sample;
} trend_buffer_t;

void trend_buffer_init(trend_buffer_t *trend);
void trend_buffer_reset(trend_buffer_t *trend);
bool trend_parse_reading_display(const char *text, const char *unit,
                                 float *base_value, trend_dimension_t *dimension,
                                 const char **base_unit, char *display_unit,
                                 uint8_t display_unit_size);
const char *trend_buffer_display_unit(const trend_buffer_t *trend);
/* True when the ring has samples spanning the whole window: before that,
 * the range grows every sample and axis auto-scaling would thrash. */
bool trend_buffer_window_full(const trend_buffer_t *trend);
float trend_buffer_display_scale(const trend_buffer_t *trend);
bool trend_buffer_add(trend_buffer_t *trend, uint32_t now_ms, const char *text,
                      const char *unit);
void trend_buffer_update(trend_buffer_t *trend, uint32_t now_ms);
uint16_t trend_buffer_project(const trend_buffer_t *trend, uint32_t now_ms,
                              trend_column_t *columns, uint16_t capacity);
bool trend_buffer_range(const trend_buffer_t *trend, uint32_t now_ms,
                        float *minimum, float *maximum);
