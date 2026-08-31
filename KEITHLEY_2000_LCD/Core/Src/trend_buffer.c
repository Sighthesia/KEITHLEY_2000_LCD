#include <string.h>
#include <float.h>

#include "trend_buffer.h"

static bool occupied_get(const trend_buffer_t *t, uint16_t i)
{
    return (t->occupied[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0u;
}

static void occupied_set(trend_buffer_t *t, uint16_t i, bool value)
{
    uint8_t mask = (uint8_t)(1u << (i & 7u));
    if (value) t->occupied[i >> 3] |= mask;
    else t->occupied[i >> 3] &= (uint8_t)~mask;
}

void trend_buffer_init(trend_buffer_t *trend)
{
    trend_buffer_reset(trend);
}

void trend_buffer_reset(trend_buffer_t *trend)
{
    if (trend != 0) memset(trend, 0, sizeof(*trend));
}

static bool parse_decimal(const char *text, float *value)
{
    const char *p = text;
    float v = 0.0f, scale = 1.0f;
    int sign = 1, exponent = 0, exponent_sign = 1;
    bool digit = false;
    if (p == 0 || value == 0) return false;
    if (*p == '+' || *p == '-') { if (*p++ == '-') sign = -1; }
    while (*p >= '0' && *p <= '9') {
        digit = true;
        if (v > (FLT_MAX - 9.0f) / 10.0f) return false;
        v = v * 10.0f + (float)(*p++ - '0');
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') { digit = true; scale *= 0.1f; v += (float)(*p++ - '0') * scale; }
    }
    if (!digit) return false;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') { if (*p++ == '-') exponent_sign = -1; }
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            if (exponent >= 100) return false;
            exponent = exponent * 10 + (*p++ - '0');
        }
        while (exponent-- > 0) {
            if (exponent_sign > 0) {
                if (v > FLT_MAX / 10.0f) return false;
                v *= 10.0f;
            } else {
                v *= 0.1f;
            }
        }
    }
    if (*p != '\0') return false;
    *value = sign < 0 ? -v : v;
    return true;
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t a, b;
    if (text == 0 || suffix == 0) return false;
    a = strlen(text); b = strlen(suffix);
    return a >= b && memcmp(text + a - b, suffix, b) == 0;
}

static void copy_unit(char *out, uint8_t size, const char *unit)
{
    uint8_t i = 0u;
    if (out == 0 || size == 0u) return;
    while (unit != 0 && unit[i] != '\0' && i + 1u < size) {
        out[i] = unit[i];
        i++;
    }
    out[i] = '\0';
}

static void normalize_display_unit(const char *unit, char *out, uint8_t size)
{
    char tmp[TREND_UNIT_ID_MAX];
    copy_unit(tmp, sizeof(tmp), unit);
    if (strcmp(tmp, "MVDC") == 0) copy_unit(out, size, "mVDC");
    else if (strcmp(tmp, "MVAC") == 0) copy_unit(out, size, "mVAC");
    else if (strcmp(tmp, "MADC") == 0) copy_unit(out, size, "mADC");
    else if (strcmp(tmp, "MAAC") == 0) copy_unit(out, size, "mAAC");
    else if (strcmp(tmp, "KOHM") == 0) copy_unit(out, size, "k\xCE\xA9");
    else if (strcmp(tmp, "MOHM") == 0) copy_unit(out, size, "M\xCE\xA9");
    else if (strcmp(tmp, "OHM") == 0) copy_unit(out, size, "\xCE\xA9");
    else copy_unit(out, size, unit);
}

bool trend_parse_reading_display(const char *text, const char *unit,
                                 float *base_value, trend_dimension_t *dimension,
                                 const char **base_unit, char *display_unit,
                                 uint8_t display_unit_size)
{
    float value, factor = 1.0f;
    const char *u = unit;
    trend_dimension_t dim = TREND_DIM_NONE;
    const char *base = "";
    if (!parse_decimal(text, &value) || unit == 0 || unit[0] == '\0') return false;
    if (unit[0] == 'm') { factor = 0.001f; u++; }
    else if (unit[0] == 'u' ||
             ((uint8_t)unit[0] == 0xC2u && (uint8_t)unit[1] == 0xB5u)) {
        factor = 0.000001f;
        u += (uint8_t)unit[0] == 0xC2u ? 2 : 1;
    } else if (unit[0] == 'k') { factor = 1000.0f; u++; }
    else if (unit[0] == 'M' && (unit[1] == 'V' || unit[1] == 'A')) {
        /* The host uses uppercase MV/MA for milli-volt/ampere modes. */
        factor = 0.001f;
        u++;
    } else if (unit[0] == 'M') { factor = 1000000.0f; u++; }
    if (factor > 1.0f && value > FLT_MAX / factor) return false;
    if (factor > 1.0f && value < -FLT_MAX / factor) return false;
    if (strstr(u, "V") != 0) { dim = TREND_DIM_VOLTAGE; base = "V"; }
    else if (strstr(u, "A") != 0) { dim = TREND_DIM_CURRENT; base = "A"; }
    else if (strstr(u, "OHM") != 0 || strstr(u, "Ohm") != 0 || strstr(u, "\xCE\xA9") != 0) { dim = TREND_DIM_RESISTANCE; base = "\xCE\xA9"; }
    else if (ends_with(u, "Hz")) { dim = TREND_DIM_FREQUENCY; base = "Hz"; }
    else if (strstr(u, "s") != 0) { dim = TREND_DIM_TIME; base = "s"; }
    else if (strstr(u, "DEG") != 0 || strstr(u, "\xC2\xB0") != 0) { dim = TREND_DIM_TEMPERATURE; base = "C"; }
    if (dim == TREND_DIM_NONE) return false;
    if (base_value != 0) *base_value = value * factor;
    if (dimension != 0) *dimension = dim;
    if (base_unit != 0) *base_unit = base;
    normalize_display_unit(unit, display_unit, display_unit_size);
    return true;
}

static void advance_to(trend_buffer_t *t, uint32_t bucket)
{
    uint32_t b;
    if (!t->has_sample) { t->newest_bucket = bucket; return; }
    if ((bucket - t->newest_bucket) >= TREND_BUCKET_COUNT) {
        memset(t->occupied, 0, sizeof(t->occupied));
    } else {
        for (b = t->newest_bucket + 1u; b <= bucket; b++)
            occupied_set(t, (uint16_t)(b % TREND_BUCKET_COUNT), false);
    }
    t->newest_bucket = bucket;
}

bool trend_buffer_add(trend_buffer_t *trend, uint32_t now_ms, const char *text,
                      const char *unit)
{
    float value;
    trend_dimension_t dim;
    char display_unit[TREND_UNIT_ID_MAX];
    uint32_t bucket;
    uint16_t index;
    if (trend == 0 || !trend_parse_reading_display(text, unit, &value, &dim, 0,
                                                    display_unit, sizeof(display_unit))) return false;
    /* A backwards local tick (including the SysTick wrap) cannot be mapped to
     * the monotonically numbered bucket ring without ambiguity. */
    if (trend->has_sample && now_ms < trend->last_sample_ms)
        trend_buffer_reset(trend);
    if (trend->has_sample && (dim != trend->dimension ||
                              strcmp(display_unit, trend->unit_identity) != 0))
        trend_buffer_reset(trend);
    if (trend->has_sample && (now_ms - trend->last_sample_ms) >= TREND_WINDOW_MS)
        trend_buffer_reset(trend);
    bucket = now_ms / TREND_BUCKET_MS;
    advance_to(trend, bucket);
    index = (uint16_t)(bucket % TREND_BUCKET_COUNT);
    if (!occupied_get(trend, index)) {
        trend->minimum[index] = value;
        trend->maximum[index] = value;
        occupied_set(trend, index, true);
    } else {
        if (value < trend->minimum[index]) trend->minimum[index] = value;
        if (value > trend->maximum[index]) trend->maximum[index] = value;
    }
    if (trend->first_sample_ms == 0u)
        trend->first_sample_ms = now_ms == 0u ? 1u : now_ms;
    trend->dimension = dim;
    copy_unit(trend->unit_identity, sizeof(trend->unit_identity), display_unit);
    trend->newest_bucket = bucket;
    trend->last_sample_ms = now_ms;
    trend->has_sample = true;
    return true;
}

bool trend_buffer_window_full(const trend_buffer_t *trend)
{
    return trend != 0 && trend->first_sample_ms != 0u &&
           trend->has_sample &&
           (trend->last_sample_ms - trend->first_sample_ms) >=
               TREND_WINDOW_MS;
}

const char *trend_buffer_display_unit(const trend_buffer_t *trend)
{
    return trend != 0 && trend->unit_identity[0] != '\0' ? trend->unit_identity : "";
}

float trend_buffer_display_scale(const trend_buffer_t *trend)
{
    const char *unit = trend_buffer_display_unit(trend);
    if (unit[0] == 'm') return 1000.0f;
    if (unit[0] == 'u' || ((uint8_t)unit[0] == 0xC2u && (uint8_t)unit[1] == 0xB5u)) return 1000000.0f;
    if (unit[0] == 'k') return 0.001f;
    if (unit[0] == 'M' && unit[1] != '\0') return 0.000001f;
    return 1.0f;
}

void trend_buffer_update(trend_buffer_t *trend, uint32_t now_ms)
{
    if (trend != 0 && trend->has_sample &&
        (now_ms - trend->last_sample_ms) >= TREND_WINDOW_MS) trend_buffer_reset(trend);
}

static bool bucket_visible(const trend_buffer_t *t, uint32_t bucket,
                            float *minimum, float *maximum)
{
    uint16_t index = (uint16_t)(bucket % TREND_BUCKET_COUNT);
    /* The occupancy bit belongs to the current generation of this ring slot.
     * Reject future/overwritten bucket numbers so time advancing without a
     * sample cannot make stale slots appear twice in a projection. */
    if (bucket > t->newest_bucket ||
        (t->newest_bucket - bucket) >= TREND_BUCKET_COUNT) return false;
    if (!occupied_get(t, index)) return false;
    *minimum = t->minimum[index]; *maximum = t->maximum[index];
    return true;
}

uint16_t trend_buffer_project(const trend_buffer_t *trend, uint32_t now_ms,
                              trend_column_t *columns, uint16_t capacity)
{
    uint16_t count, c;
    uint32_t now_bucket, first;
    if (trend == 0 || columns == 0 || capacity == 0u || !trend->has_sample) return 0u;
    count = capacity > TREND_MAX_COLUMNS ? TREND_MAX_COLUMNS : capacity;
    now_bucket = now_ms / TREND_BUCKET_MS;
    first = now_bucket >= TREND_BUCKET_COUNT - 1u ? now_bucket - (TREND_BUCKET_COUNT - 1u) : 0u;
    for (c = 0u; c < count; c++) {
        uint32_t begin = first + ((uint32_t)c * TREND_BUCKET_COUNT) / count;
        uint32_t end = first + ((uint32_t)(c + 1u) * TREND_BUCKET_COUNT) / count;
        uint32_t b;
        columns[c].occupied = false;
        columns[c].x = c;
        for (b = begin; b < end; b++) {
            float lo, hi;
            if (bucket_visible(trend, b, &lo, &hi)) {
                if (!columns[c].occupied || lo < columns[c].minimum) columns[c].minimum = lo;
                if (!columns[c].occupied || hi > columns[c].maximum) columns[c].maximum = hi;
                columns[c].occupied = true;
            }
        }
    }
    return count;
}

bool trend_buffer_range(const trend_buffer_t *trend, uint32_t now_ms,
                        float *minimum, float *maximum)
{
    uint32_t now_bucket, first, b;
    bool found = false;
    float lo = 0.0f, hi = 0.0f;
    if (trend == 0 || minimum == 0 || maximum == 0 || !trend->has_sample) return false;
    now_bucket = now_ms / TREND_BUCKET_MS;
    first = now_bucket >= TREND_BUCKET_COUNT - 1u ? now_bucket - (TREND_BUCKET_COUNT - 1u) : 0u;
    for (b = first; b <= now_bucket; b++) {
        float a, z;
        if (bucket_visible(trend, b, &a, &z)) {
            if (!found || a < lo) lo = a;
            if (!found || z > hi) hi = z;
            found = true;
        }
    }
    if (!found) return false;
    {
        float span = hi - lo;
        if (span < 0.000001f) {
            float magnitude = hi < 0.0f ? -hi : hi;
            span = magnitude * 0.02f;
            if (span < 0.000001f) span = 0.000001f;
        }
        *minimum = lo - span * 0.1f;
        *maximum = hi + span * 0.1f;
    }
    return true;
}
