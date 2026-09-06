#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "main_display.h"

static void copy_text(char *out, uint8_t size, const char *text)
{
    uint8_t n = 0u;
    if (out == 0 || size == 0u) return;
    if (text != 0) while (text[n] != '\0' && n + 1u < size) { out[n] = text[n]; n++; }
    out[n] = '\0';
}

static void append_text(char *out, uint8_t size, const char *text)
{
    uint8_t n = (uint8_t)strlen(out), i = 0u;
    while (text != 0 && text[i] != '\0' && n + 1u < size) out[n++] = text[i++];
    out[n] = '\0';
}

static void format_runtime_text(char *temperature, uint8_t temperature_size,
                                char *uptime, uint8_t uptime_size,
                                int16_t temperature_tenths_c,
                                int16_t humidity_percent,
                                uint32_t uptime_ms)
{
    uint32_t seconds = uptime_ms / 1000u;
    uint32_t hours = seconds / 3600u;
    uint8_t minutes = (uint8_t)((seconds / 60u) % 60u);
    uint8_t secs = (uint8_t)(seconds % 60u);
    uint16_t magnitude;
    char sign = '+';

    if (temperature_tenths_c < 0) {
        sign = '-';
        magnitude = (uint16_t)(-temperature_tenths_c);
    } else {
        magnitude = (uint16_t)temperature_tenths_c;
    }
    if (temperature_tenths_c == INT16_MIN) {
        copy_text(temperature, temperature_size, "--.-\xC2\xB0" "C");
    } else {
        (void)snprintf(temperature, temperature_size, "%c%u.%u\xC2\xB0" "C",
                       sign, magnitude / 10u, magnitude % 10u);
    }
    /* Humidity rides on the temperature field (no extra header cell):
     * integer percent, "--%" while the SHT3x has no valid sample. Manual
     * digits keep -Werror quiet where snprintf %d into a fixed buffer
     * would trip -Wformat-truncation. */
    append_text(temperature, temperature_size, " ");
    if (humidity_percent < 0 || humidity_percent > 100) {
        append_text(temperature, temperature_size, "--%");
    } else {
        char digits[4];
        uint8_t n = 0u;
        uint16_t rest = (uint16_t)humidity_percent;
        uint16_t div = 100u;
        bool started = false;

        while (div > 0u) {
            uint8_t d = (uint8_t)(rest / div);

            if (d != 0u || started || div == 1u) {
                digits[n++] = (char)('0' + d);
                started = true;
            }
            rest %= div;
            div /= 10u;
        }
        digits[n] = '\0';
        append_text(temperature, temperature_size, digits);
        append_text(temperature, temperature_size, "%RH");
    }
    (void)snprintf(uptime, uptime_size, "%02lu:%02u:%02u",
                   (unsigned long)(hours > 99u ? 99u : hours), minutes, secs);
}

static void format_active_status(const main_display_frame_t *frame,
                                 char *out, uint8_t size)
{
    uint8_t i;
    bool first = true;
    out[0] = '\0';
    for (i = 0u; i < frame->status_count; i++) {
        if (!frame->status_active[i]) continue;
        if (!first) append_text(out, size, " ");
        append_text(out, size, frame->status_text[i]);
        first = false;
    }
}

static void split_function_name(const char *function, char *line1, uint8_t line1_size,
                                char *line2, uint8_t line2_size)
{
    const char *space = function != 0 ? strchr(function, ' ') : 0;
    if (space == 0) {
        copy_text(line1, line1_size, function);
        line2[0] = '\0';
        return;
    }
    {
        uint8_t n = (uint8_t)(space - function);
        if (n >= line1_size) n = (uint8_t)(line1_size - 1u);
        memcpy(line1, function, n);
        line1[n] = '\0';
    }
    copy_text(line2, line2_size, space + 1);
}

main_display_layout_t main_display_layout_value(uint8_t value_len,
                                                 uint16_t start_x,
                                                 uint8_t slots)
{
    main_display_layout_t layout;
    layout.start_x = start_x; layout.end_x = start_x; layout.valid = 0u;
    if (slots == 0u) return layout;
    if (value_len == 0u) { layout.valid = 1u; return layout; }
    layout.end_x = (uint16_t)(start_x + (uint16_t)value_len * FONT_DIGIT_WIDTH);
    layout.valid = value_len <= slots;
    return layout;
}

uint16_t main_display_cursor_x(const main_display_layout_t *layout,
                               uint16_t cursor_pos)
{
    uint16_t count;
    if (layout == 0 || !layout->valid) return layout != 0 ? layout->end_x : 0u;
    count = (uint16_t)((layout->end_x - layout->start_x) / FONT_DIGIT_WIDTH);
    if (cursor_pos > count) cursor_pos = count;
    return (uint16_t)(layout->start_x + cursor_pos * FONT_DIGIT_WIDTH);
}

uint16_t main_display_special_color(uint8_t special)
{
    if (special == 1u) return MAIN_DISPLAY_COLOR_RED;
    if (special == 2u) return MAIN_DISPLAY_COLOR_MUTED;
    return MAIN_DISPLAY_COLOR_GREEN;
}

const char *main_display_rate_text(ui_rate_t rate)
{
    switch (rate) {
    /* Short form: the row-2 Rate value zone fits 5 glyphs (64px); the long
     * form overflowed into the lamp zone and collided with FILT/REL/MATH. */
    case UI_RATE_FAST: return "500/s";
    case UI_RATE_MED: return "50/s";
    case UI_RATE_SLOW: return "5/s";
    default: return "--/s";
    }
}

const char *main_display_function_text(ui_function_t function)
{
    switch (function) {
    case UI_FUNCTION_DC_VOLTAGE: return "DC Voltage";
    case UI_FUNCTION_AC_VOLTAGE: return "AC Voltage";
    case UI_FUNCTION_DC_CURRENT: return "DC Current";
    case UI_FUNCTION_AC_CURRENT: return "AC Current";
    case UI_FUNCTION_2W_OHM: return "2W \xCE\xA9";
    case UI_FUNCTION_4W_OHM: return "4W \xCE\xA9";
    case UI_FUNCTION_FREQUENCY: return "Frequency";
    case UI_FUNCTION_PERIOD: return "Period";
    case UI_FUNCTION_TEMPERATURE: return "Temperature";
    default: return "Measurement";
    }
}

static void append_unsigned(char *out, uint8_t size, uint32_t value,
                            uint8_t minimum_digits)
{
    char reverse[12];
    uint8_t n = 0u;
    do { reverse[n++] = (char)('0' + value % 10u); value /= 10u; }
    while ((value != 0u || n < minimum_digits) && n < sizeof(reverse));
    while (n > 0u) { char c[2] = {reverse[--n], '\0'}; append_text(out, size, c); }
}

static float nice_step(float span)
{
    float step = 1.0f;
    float scaled = span / 2.0f;
    while (scaled >= 10.0f) { scaled *= 0.1f; step *= 10.0f; }
    while (scaled < 1.0f) { scaled *= 10.0f; step *= 0.1f; }
    if (scaled <= 1.0f) return step;
    if (scaled <= 2.0f) return step * 2.0f;
    if (scaled <= 5.0f) return step * 5.0f;
    return step * 10.0f;
}

static void format_fixed_axis(float value, const char *unit, float step,
                              char *out, uint8_t out_size);

static void format_stat_value(float value, const char *unit, float span,
                              char *out, uint8_t out_size)
{
    char number[MAIN_DISPLAY_AXIS_LABEL_MAX];
    float step = span / 10.0f;

    if (step < 0.000001f)
        step = 0.000001f;
    format_fixed_axis(value, unit, step, number, sizeof(number));
    copy_text(out, out_size, number);
}

static void format_fixed_axis(float value, const char *unit, float step,
                              char *out, uint8_t out_size)
{
    uint8_t decimals = 0u;
    float scale = 1.0f, rounded;
    uint32_t whole, fraction;
    /* Overwrite semantics: main_display_set_trend_axis() regenerates labels
     * on frames whose cells already hold the auto-fit text; appending would
     * grow the label every keep-resident frame until the buffer saturates. */
    if (out == 0 || out_size == 0u) return;
    out[0] = '\0';
    while (step * scale < 1.0f && decimals < 6u) { scale *= 10.0f; decimals++; }
    rounded = value * scale;
    if (rounded < 0.0f) { append_text(out, out_size, "-"); rounded = -rounded; }
    rounded += 0.5f;
    whole = (uint32_t)(rounded / scale);
    fraction = (uint32_t)rounded - whole * (uint32_t)scale;
    append_unsigned(out, out_size, whole, 1u);
    if (decimals != 0u) {
        append_text(out, out_size, ".");
        append_unsigned(out, out_size, fraction, decimals);
    }
    append_text(out, out_size, unit);
}

/* Fixed-decimal stat formatting: value is already in DISPLAY units, exactly
 * `decimals` fractional digits (padded, never trimmed — "4.0000Ω" next to a
 * "2.3624Ω" reading), unit appended, all bounded by out_size. No float
 * printf involved. */
void main_display_format_stat_fixed(float value, const char *unit,
                                    uint8_t decimals, char *out,
                                    uint8_t out_size)
{
    float scale = 1.0f;
    float rounded;
    uint32_t whole, fraction;
    uint8_t d;

    if (out == 0 || out_size == 0u) return;
    out[0] = '\0';
    if (decimals > 5u) decimals = 5u;
    for (d = 0u; d < decimals; d++) scale *= 10.0f;
    rounded = value * scale;
    if (rounded < 0.0f) { append_text(out, out_size, "-"); rounded = -rounded; }
    rounded += 0.5f;
    whole = (uint32_t)(rounded / scale);
    fraction = (uint32_t)rounded - whole * (uint32_t)scale;
    append_unsigned(out, out_size, whole, 1u);
    if (decimals != 0u) {
        append_text(out, out_size, ".");
        append_unsigned(out, out_size, fraction, decimals);
    }
    append_text(out, out_size, unit);
}

uint8_t main_display_trend_plot_y(float value, float minimum, float maximum)
{
    float span;
    float fy;

    span = maximum - minimum;
    if (span < 0.000001f)
        return (uint8_t)(MAIN_DISPLAY_PLOT_H / 2u);
    fy = (maximum - value) * (float)MAIN_DISPLAY_PLOT_H / span;
    if (fy < 0.0f)
        fy = 0.0f;
    if (fy > (float)MAIN_DISPLAY_PLOT_H - 1.0f)
        fy = (float)MAIN_DISPLAY_PLOT_H - 1.0f;
    return (uint8_t)fy;
}

void main_display_format_linear_trend_labels(main_display_frame_t *frame)
{
    float scale;
    float minimum;
    float maximum;
    float span;
    const char *unit;
    uint8_t i;

    if (frame == 0 || !frame->trend_has_data)
        return;
    unit = frame->trend_axis_unit[0] != '\0' ? frame->trend_axis_unit : "";
    scale = 1.0f;
    if (unit[0] == 'm')
        scale = 1000.0f;
    else if (unit[0] == 'u' ||
             ((uint8_t)unit[0] == 0xC2u && (uint8_t)unit[1] == 0xB5u))
        scale = 1000000.0f;
    else if (unit[0] == 'k')
        scale = 0.001f;
    else if (unit[0] == 'M' && unit[1] != '\0')
        scale = 0.000001f;
    minimum = frame->trend_minimum * scale;
    maximum = frame->trend_maximum * scale;
    span = maximum - minimum;
    if (span < 0.000001f)
        span = 0.000001f;
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
        format_fixed_axis(maximum - span * (float)i / 2.0f, unit, span / 2.0f,
                          frame->y_labels[i], MAIN_DISPLAY_AXIS_LABEL_MAX);
}

void main_display_set_trend_axis(main_display_frame_t *frame, float step,
                                 float top, const char *unit)
{
    uint8_t i;
    if (frame == 0 || step <= 0.0f || unit == 0)
        return;
    frame->trend_axis_step = step;
    frame->trend_axis_top = top;
    copy_text(frame->trend_axis_unit, sizeof(frame->trend_axis_unit), unit);
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
        format_fixed_axis(top - step * i, unit, step,
                          frame->y_labels[i], MAIN_DISPLAY_AXIS_LABEL_MAX);
}

static void format_impedance(const ui_model_t *model, char *out, uint8_t size)
{
    /* The observed panel protocol does not provide range. Even AC impedance
     * must remain unknown until both function and range are authoritative.
     * The bare value is rendered inside the info panel's value cell with the
     * fixed "Zin" name drawn by the renderer. */
    (void)model;
    copy_text(out, size, "--");
}

void main_display_format(const ui_model_t *model, main_display_frame_t *frame)
{
    main_display_layout_t layout;
    uint8_t i, n;
    const status_bar_indicator_t *indicator;
    if (frame == 0) return;
    memset(frame, 0, sizeof(*frame));
    copy_text(frame->x_labels[0], MAIN_DISPLAY_AXIS_LABEL_MAX, "10.00s");
    copy_text(frame->x_labels[1], MAIN_DISPLAY_AXIS_LABEL_MAX, "7.50s");
    copy_text(frame->x_labels[2], MAIN_DISPLAY_AXIS_LABEL_MAX, "5.00s");
    copy_text(frame->x_labels[3], MAIN_DISPLAY_AXIS_LABEL_MAX, "2.50s");
    copy_text(frame->x_labels[4], MAIN_DISPLAY_AXIS_LABEL_MAX, "0.00s");
    if (model == 0) return;
    copy_text(frame->value, sizeof(frame->value), model->value);
    copy_text(frame->unit, sizeof(frame->unit), model->unit);
    frame->value_len = (uint8_t)strlen(frame->value);
    frame->unit_len = (uint8_t)strlen(frame->unit);
    /* "VDC"/"VAC"/"ADC"/"AAC" split into a full-size base unit plus a
     * half-height DC/AC suffix; "mV", "kHz", "OHM" etc. keep the whole unit. */
    frame->unit_suffix[0] = '\0';
    if (frame->unit_len >= 3u &&
        frame->unit[frame->unit_len - 2u] == 'D' &&
        frame->unit[frame->unit_len - 1u] == 'C') {
        memcpy(frame->unit_suffix, "DC", 3u);
        frame->unit_len -= 2u;
    } else if (frame->unit_len >= 3u &&
               frame->unit[frame->unit_len - 2u] == 'A' &&
               frame->unit[frame->unit_len - 1u] == 'C') {
        memcpy(frame->unit_suffix, "AC", 3u);
        frame->unit_len -= 2u;
    }
    frame->unit[frame->unit_len] = '\0';
    frame->special = model->special;
    frame->no_data = !model->any_message || frame->value_len == 0u;
    frame->value_color = main_display_special_color(model->special);
    frame->reading_y = MAIN_DISPLAY_READING_VALUE_Y;
    layout = main_display_layout_value(frame->value_len, MAIN_DISPLAY_READING_X,
                                       MAIN_DISPLAY_MAX_SLOTS);
    frame->start_x = layout.start_x; frame->end_x = layout.end_x;
    copy_text(frame->function, sizeof(frame->function),
              main_display_function_text(model->function_id));
    copy_text(frame->brand, sizeof(frame->brand), "KEITHLEY 2000");
    split_function_name(frame->function, frame->function_line1,
                        sizeof(frame->function_line1), frame->function_line2,
                        sizeof(frame->function_line2));
    format_impedance(model, frame->impedance, sizeof(frame->impedance));
    copy_text(frame->range, sizeof(frame->range), model->auto_range ? "AUTO" : "MANUAL");
    copy_text(frame->filter, sizeof(frame->filter), model->filter_on ? "Filter: ON" : "Filter: OFF");
    copy_text(frame->rate, sizeof(frame->rate), main_display_rate_text(model->rate));
    copy_text(frame->gpib, sizeof(frame->gpib), "GPIB: --");
    copy_text(frame->buffer, sizeof(frame->buffer), model->buffer_recall ?
              "BUFFER: RECALL" : "BUFFER: IDLE (1024 MAX)");
    for (i = 0u; i < STATUS_BAR_CORE_COUNT; i++) {
        if (!status_bar_core_get(i, &indicator)) continue;
        n = (uint8_t)strlen(indicator->label);
        if (n >= MAIN_DISPLAY_STATUS_LABEL_MAX) n = MAIN_DISPLAY_STATUS_LABEL_MAX - 1u;
        memcpy(frame->status_text[i], indicator->label, n);
        frame->status_text[i][n] = '\0';
        frame->status_active[i] = indicator->tag == 0u ? true :
            status_bar_active(&model->status, indicator->tag, indicator->bit);
        frame->status_count++;
    }
    format_active_status(frame, frame->active_status, sizeof(frame->active_status));
    format_runtime_text(frame->temperature, sizeof(frame->temperature),
                        frame->uptime, sizeof(frame->uptime), INT16_MIN,
                        INT16_MIN, 0u);
}

void main_display_format_runtime(main_display_frame_t *frame,
                                 int16_t temperature_tenths_c,
                                 int16_t humidity_percent,
                                 uint32_t uptime_ms)
{
    if (frame == 0) return;
    format_runtime_text(frame->temperature, sizeof(frame->temperature),
                        frame->uptime, sizeof(frame->uptime),
                        temperature_tenths_c, humidity_percent, uptime_ms);
}

void main_display_get_trend_axis(const main_display_frame_t *frame,
                                 main_display_trend_axis_t *axis)
{
    if (axis == 0) return;
    axis->valid = frame != 0 && frame->trend_has_data &&
                  frame->trend_axis_step > 0.0f;
    if (!axis->valid) {
        axis->step = 0.0f;
        axis->top = 0.0f;
        axis->unit[0] = '\0';
        return;
    }
    axis->step = frame->trend_axis_step;
    axis->top = frame->trend_axis_top;
    copy_text(axis->unit, sizeof(axis->unit), frame->trend_axis_unit);
}

void main_display_format_trend(const trend_buffer_t *trend, uint32_t now_ms,
                               const char *unit, main_display_frame_t *frame)
{
    float minimum, maximum, step, top, scale;
    const char *axis_unit = unit;
    uint8_t i;
    if (frame == 0) return;
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
        frame->y_labels[i][0] = '\0';
    if (!trend_buffer_range(trend, now_ms, &minimum, &maximum)) {
        frame->trend_has_data = false;
        frame->trend_axis_step = 0.0f;
        frame->trend_axis_top = 0.0f;
        frame->trend_axis_unit[0] = '\0';
        return;
    }
    frame->trend_has_data = true;
    /* trend_minimum/trend_maximum stay DATA bounds (scaled window range).
     * The projection bounds of the resident axis are applied by the caller
     * that decides residency, right before columns are drawn. */
    frame->trend_minimum = minimum; frame->trend_maximum = maximum;
    /* trend_buffer stores normalized base-unit values. Axis labels must use
     * that same unit even when the latest host reading changes prefix. */
    scale = trend_buffer_display_scale(trend);
    if (trend != 0) {
        if (trend_buffer_display_unit(trend)[0] != '\0')
            axis_unit = trend_buffer_display_unit(trend);
    }
    minimum *= scale;
    maximum *= scale;
    {
        float pad = (maximum - minimum) * 0.5f;

        minimum -= pad;
        maximum += pad;
    }
    step = nice_step(maximum - minimum);
    top = (float)((int32_t)(maximum / step)) * step;
    if (top < maximum) top += step;
    frame->trend_axis_step = step;
    frame->trend_axis_top = top;
    copy_text(frame->trend_axis_unit, sizeof(frame->trend_axis_unit),
              axis_unit);
    for (i = 0u; i < MAIN_DISPLAY_Y_LABEL_COUNT; i++)
        format_fixed_axis(top - step * i, axis_unit, step,
                          frame->y_labels[i], MAIN_DISPLAY_AXIS_LABEL_MAX);

    frame->trend_stat_minimum = minimum;
    frame->trend_stat_maximum = maximum;
    frame->trend_stat_average = (minimum + maximum) * 0.5f;
    {
        uint32_t count = 0u;
        uint32_t b;
        float sum = 0.0f;
        float lo = 0.0f;
        float hi = 0.0f;
        bool found = false;
        uint32_t now_bucket = now_ms / TREND_BUCKET_MS;
        uint32_t first = now_bucket >= TREND_BUCKET_COUNT - 1u
                              ? now_bucket - (TREND_BUCKET_COUNT - 1u)
                              : 0u;

        for (b = first; b <= now_bucket; b++)
        {
            uint16_t index = (uint16_t)(b % TREND_BUCKET_COUNT);
            if (b <= trend->newest_bucket &&
                trend->newest_bucket - b < TREND_BUCKET_COUNT &&
                (trend->occupied[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u)
            {
                float mid = (trend->minimum[index] + trend->maximum[index]) * 0.5f;
                if (!found || trend->minimum[index] < lo) lo = trend->minimum[index];
                if (!found || trend->maximum[index] > hi) hi = trend->maximum[index];
                sum += mid;
                count++;
                found = true;
            }
        }
        if (found)
        {
            frame->trend_stat_minimum = lo;
            frame->trend_stat_maximum = hi;
            frame->trend_stat_average = sum / (float)count;
        }
    }
    format_stat_value(frame->trend_stat_maximum * scale, axis_unit,
                      maximum - minimum, frame->trend_stat_maximum_text,
                      sizeof(frame->trend_stat_maximum_text));
    format_stat_value(frame->trend_stat_minimum * scale, axis_unit,
                      maximum - minimum, frame->trend_stat_minimum_text,
                      sizeof(frame->trend_stat_minimum_text));
    format_stat_value(frame->trend_stat_average * scale, axis_unit,
                      maximum - minimum, frame->trend_stat_average_text,
                      sizeof(frame->trend_stat_average_text));
}
