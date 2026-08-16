#include <assert.h>
#include <string.h>

#include "trend_buffer.h"

int main(void)
{
    trend_buffer_t trend;
    trend_column_t columns[TREND_MAX_COLUMNS];
    float value, minimum, maximum;
    trend_dimension_t dimension;
    const char *unit;
    uint16_t count, i;
    bool peak = false;

    assert(trend_parse_reading_display("+03.68900", "VDC", &value, &dimension, &unit, 0, 0u));
    assert(value > 3.688f && value < 3.690f && dimension == TREND_DIM_VOLTAGE);
    assert(trend_parse_reading_display("1000", "mV", &value, &dimension, &unit, 0, 0u));
    assert(value > 0.999f && value < 1.001f);
    assert(trend_parse_reading_display("1000000", "\xC2\xB5V", &value, &dimension,
                                       &unit, 0, 0u));
    assert(value > 0.999f && value < 1.001f);
    assert(!trend_parse_reading_display("1", "\xC2X", &value, &dimension, &unit, 0, 0u));
    assert(!trend_parse_reading_display("1e999", "V", &value, &dimension, &unit, 0, 0u));
    assert(!trend_parse_reading_display("9999999999999999999999999999999999999999", "V",
                                        &value, &dimension, &unit, 0, 0u));
    assert(!trend_parse_reading_display("1.2.3", "V", &value, &dimension, &unit, 0, 0u));
    assert(!trend_parse_reading_display("OVERFLOW", "V", &value, &dimension, &unit, 0, 0u));
    assert(!trend_parse_reading_display("----", "V", &value, &dimension, &unit, 0, 0u));

    trend_buffer_init(&trend);
    assert(trend_buffer_add(&trend, 0u, "1", "V"));
    assert(trend_buffer_add(&trend, 1u, "9", "V"));
    assert(trend_buffer_add(&trend, 19u, "-2", "V"));
    assert(trend_buffer_add(&trend, 20u, "2", "V"));
    count = trend_buffer_project(&trend, 20u, columns, TREND_MAX_COLUMNS);
    assert(count == TREND_MAX_COLUMNS);
    for (i = 0u; i < count; i++)
        if (columns[i].occupied && columns[i].minimum <= -2.0f && columns[i].maximum >= 9.0f) peak = true;
    assert(peak);
    assert(trend_buffer_range(&trend, 20u, &minimum, &maximum));
    assert(minimum < -2.0f && maximum > 9.0f);

    /* Advancing the view without samples must not reinterpret wrapped ring
     * slots as future data. */
    count = trend_buffer_project(&trend, 9000u, columns, TREND_MAX_COLUMNS);
    for (i = 0u; i < count; i++)
        assert(!columns[i].occupied || columns[i].maximum <= 9.0f);

    /* A complete display-unit change clears history; repeated units preserve it. */
    assert(trend_buffer_add(&trend, 40u, "3000", "mV"));
    assert(strcmp(trend_buffer_display_unit(&trend), "mV") == 0);
    assert(trend_buffer_add(&trend, 50u, "1", "mV"));
    assert(trend.has_sample);
    assert(trend_buffer_add(&trend, 60u, "1", "V"));
    assert(strcmp(trend_buffer_display_unit(&trend), "V") == 0);
    assert(trend_buffer_range(&trend, 60u, &minimum, &maximum));
    assert(minimum > 0.9f && maximum < 1.1f);
    assert(trend_buffer_add(&trend, 70u, "1", "A"));
    assert(trend.dimension == TREND_DIM_CURRENT);
    assert(trend_buffer_range(&trend, 60u, &minimum, &maximum));
    assert(minimum < 1.0f && maximum > 1.0f && minimum > 0.9f);
    assert(trend_buffer_add(&trend, 80u, "2", "mA"));
    assert(strcmp(trend_buffer_display_unit(&trend), "mA") == 0);
    assert(trend_buffer_add(&trend, 90u, "2", "mA"));
    assert(trend.has_sample);

    /* Ring wraps and timeout clears. */
    for (i = 0u; i < TREND_BUCKET_COUNT + 10u; i++)
        assert(trend_buffer_add(&trend, (uint32_t)i * TREND_BUCKET_MS, "1", "A"));
    trend_buffer_update(&trend, trend.last_sample_ms + TREND_WINDOW_MS);
    assert(!trend.has_sample);
    return 0;
}
