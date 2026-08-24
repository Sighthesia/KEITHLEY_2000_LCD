#include <string.h>

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
    case UI_RATE_FAST: return "500 Read/s";
    case UI_RATE_MED: return "50 Read/s";
    case UI_RATE_SLOW: return "5 Read/s";
    default: return "-- Read/s";
    }
}

const char *main_display_function_text(ui_function_t function)
{
    switch (function) {
    case UI_FUNCTION_DC_VOLTAGE: return "DC VOLTAGE";
    case UI_FUNCTION_AC_VOLTAGE: return "AC VOLTAGE";
    case UI_FUNCTION_DC_CURRENT: return "DC CURRENT";
    case UI_FUNCTION_AC_CURRENT: return "AC CURRENT";
    case UI_FUNCTION_2W_OHM: return "2W \xCE\xA9";
    case UI_FUNCTION_4W_OHM: return "4W \xCE\xA9";
    case UI_FUNCTION_FREQUENCY: return "FREQUENCY";
    case UI_FUNCTION_PERIOD: return "PERIOD";
    case UI_FUNCTION_TEMPERATURE: return "TEMPERATURE";
    default: return "MEASUREMENT";
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
    float scaled = span / 3.0f;
    while (scaled >= 10.0f) { scaled *= 0.1f; step *= 10.0f; }
    while (scaled < 1.0f) { scaled *= 10.0f; step *= 0.1f; }
    if (scaled <= 1.0f) return step;
    if (scaled <= 2.0f) return step * 2.0f;
    if (scaled <= 5.0f) return step * 5.0f;
    return step * 10.0f;
}

static void format_fixed_axis(float value, const char *unit, float step,
                              char *out, uint8_t out_size)
{
    uint8_t decimals = 0u;
    float scale = 1.0f, rounded;
    uint32_t whole, fraction;
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
}

void main_display_format_trend(const trend_buffer_t *trend, uint32_t now_ms,
                               const char *unit, main_display_frame_t *frame)
{
    float minimum, maximum, step, top;
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
    frame->trend_minimum = minimum; frame->trend_maximum = maximum;
    /* trend_buffer stores normalized base-unit values. Axis labels must use
     * that same unit even when the latest host reading changes prefix. */
    if (trend != 0) {
        if (trend_buffer_display_unit(trend)[0] != '\0')
            axis_unit = trend_buffer_display_unit(trend);
    }
    minimum *= trend_buffer_display_scale(trend);
    maximum *= trend_buffer_display_scale(trend);
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
}
