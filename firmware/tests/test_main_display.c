#include <assert.h>
#include <string.h>

#include "main_display.h"

int main(void)
{
    ui_model_t model;
    main_display_frame_t frame;
    main_display_layout_t layout;
    trend_buffer_t trend;

    assert(MAIN_DISPLAY_STATUS_H + MAIN_DISPLAY_READING_H +
           MAIN_DISPLAY_TREND_H == 320u);
    /* Left-aligned value: 6 chars start at x=12 and advance by 64 each. */
    layout = main_display_layout_value(6u, 12u, 10u);
    assert(layout.valid && layout.start_x == 12u &&
           layout.end_x == 12u + 6u * FONT_DIGIT_WIDTH);
    assert(main_display_cursor_x(&layout, 99u) == layout.end_x);
    assert(!main_display_layout_value(11u, 12u, 10u).valid);
    assert(main_display_special_color(0u) == MAIN_DISPLAY_COLOR_GREEN);
    assert(main_display_special_color(1u) == MAIN_DISPLAY_COLOR_RED);
    assert(strcmp(main_display_rate_text(UI_RATE_FAST), "500 Read/s") == 0);
    assert(strcmp(main_display_rate_text(UI_RATE_MED), "50 Read/s") == 0);
    assert(strcmp(main_display_rate_text(UI_RATE_SLOW), "5 Read/s") == 0);

    ui_model_init(&model);
    ui_model_apply_reading(&model, "+03.68900", 9u, "VDC", 3u, 0u);
    ui_model_apply_status(&model, 0x06u, 0x0Fu);
    ui_model_apply_status(&model, 0x07u, 0x20u);
    ui_model_apply_status(&model, 0x08u, 0x0Cu);
    ui_model_apply_status(&model, 0x09u, 0x72u);
    main_display_format(&model, &frame);
    assert(strcmp(frame.value, "+03.68900") == 0);
    assert(strcmp(frame.unit, "V") == 0);
    assert(strcmp(frame.unit_suffix, "DC") == 0);
    assert(frame.unit_len == 1u);
    assert(frame.reading_y == MAIN_DISPLAY_READING_VALUE_Y);
    assert(strcmp(frame.function, "DC VOLTAGE") == 0);
    assert(strcmp(frame.impedance, "--") == 0);
    assert(strcmp(frame.range, "AUTO") == 0);
    assert(strcmp(frame.filter, "Filter: ON") == 0);
    assert(strcmp(frame.rate, "500 Read/s") == 0);
    assert(strcmp(frame.gpib, "GPIB: --") == 0);
    assert(strcmp(frame.buffer, "BUFFER: RECALL") == 0);
    assert(frame.status_active[0] && frame.status_active[1] &&
           frame.status_active[2] && frame.status_active[3]);
    assert(frame.status_active[5] && frame.status_active[6] &&
           frame.status_active[7] && frame.status_active[8] &&
           frame.status_active[10] && frame.status_active[11] &&
           frame.status_active[12]);
    assert(strcmp(frame.x_labels[0], "10.00s") == 0);
    assert(strcmp(frame.x_labels[4], "0.00s") == 0);

    /* DC/AC suffix split: "AAC" -> base "A" + suffix "AC"; "mV" keeps the
     * whole unit; the model still infers from the original unit string. */
    ui_model_init(&model);
    ui_model_apply_reading(&model, "1.50000", 7u, "AAC", 3u, 0u);
    main_display_format(&model, &frame);
    assert(strcmp(frame.unit, "A") == 0);
    assert(strcmp(frame.unit_suffix, "AC") == 0);
    assert(ui_model_infer_function("AAC") == UI_FUNCTION_AC_CURRENT);
    ui_model_init(&model);
    ui_model_apply_reading(&model, "1.2000", 6u, "mV", 2u, 0u);
    main_display_format(&model, &frame);
    assert(strcmp(frame.unit, "mV") == 0);
    assert(frame.unit_suffix[0] == '\0');
    assert(ui_model_infer_function("mV") == UI_FUNCTION_DC_VOLTAGE);

    assert(ui_model_infer_function("VAC") == UI_FUNCTION_AC_VOLTAGE);
    assert(ui_model_infer_function("ADC") == UI_FUNCTION_DC_CURRENT);
    assert(ui_model_infer_function("AAC") == UI_FUNCTION_AC_CURRENT);
    assert(ui_model_infer_function("OHM") == UI_FUNCTION_2W_OHM);
    assert(ui_model_infer_function("4W OHM") == UI_FUNCTION_4W_OHM);
    assert(ui_model_infer_function("Hz") == UI_FUNCTION_FREQUENCY);
    assert(ui_model_infer_function("DEGC") == UI_FUNCTION_TEMPERATURE);
    assert(strcmp(main_display_function_text(UI_FUNCTION_2W_OHM),
                  "2W \xCE\xA9") == 0);
    assert(strcmp(main_display_function_text(UI_FUNCTION_4W_OHM),
                  "4W \xCE\xA9") == 0);
    assert(strcmp(main_display_function_text(UI_FUNCTION_DC_VOLTAGE),
                  "DC VOLTAGE") == 0);

    /* Resistance units are normalized to the UTF-8 Ohm sign so the display
     * shows Ω / kΩ / MΩ; the function is inferred after normalization. */
    ui_model_init(&model);
    ui_model_apply_reading(&model, "2000.0000", 9u, "OHM", 3u, 0u);
    assert(strcmp(model.unit, "\xCE\xA9") == 0);
    assert(ui_model_infer_function(model.unit) == UI_FUNCTION_2W_OHM);
    ui_model_init(&model);
    ui_model_apply_reading(&model, "2000.0000", 9u, "KOHM", 4u, 0u);
    assert(strcmp(model.unit, "k\xCE\xA9") == 0);
    assert(ui_model_infer_function(model.unit) == UI_FUNCTION_2W_OHM);
    ui_model_init(&model);
    ui_model_apply_reading(&model, "2000.0000", 9u, "MOHM", 4u, 0u);
    assert(strcmp(model.unit, "M\xCE\xA9") == 0);
    assert(ui_model_infer_function(model.unit) == UI_FUNCTION_2W_OHM);

    trend_buffer_init(&trend);
    assert(trend_buffer_add(&trend, 0u, "1.0", "V"));
    assert(trend_buffer_add(&trend, 20u, "2.0", "V"));
    main_display_format_trend(&trend, 20u, "V", &frame);
    assert(frame.trend_has_data && frame.trend_maximum > 2.0f &&
           frame.trend_minimum < 1.0f);
    assert(strstr(frame.y_labels[0], "V") != 0);
    trend_buffer_reset(&trend);
    assert(trend_buffer_add(&trend, 40u, "1000", "mV"));
    main_display_format_trend(&trend, 40u, "mV", &frame);
    assert(strstr(frame.y_labels[0], "mV") != 0);
    assert(strstr(frame.y_labels[0], "1000") == 0);
    trend_buffer_reset(&trend);
    assert(trend_buffer_add(&trend, 60u, "1", "A"));
    assert(trend_buffer_add(&trend, 80u, "1.2", "A"));
    main_display_format_trend(&trend, 80u, "A", &frame);
    assert(strstr(frame.y_labels[0], "A") != 0);
    assert(strstr(frame.y_labels[0], "6.89") == 0);

    /* Negative and sub-unit values must keep a finite, ordered scale. */
    trend_buffer_reset(&trend);
    assert(trend_buffer_add(&trend, 100u, "-0.002", "mA"));
    assert(trend_buffer_add(&trend, 120u, "-0.001", "mA"));
    main_display_format_trend(&trend, 120u, "mA", &frame);
    assert(frame.trend_has_data);
    assert(strstr(frame.y_labels[0], "mA") != 0);
    assert(strstr(frame.y_labels[3], "mA") != 0);
    return 0;
}
