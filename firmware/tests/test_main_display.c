#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "main_display.h"
#include "trend_axis.h"

/* Mirrors the production snapshot sequence in Core/Src/main.c: auto-fit the
 * window into the frame, evaluate the shared trend-axis rules, project the
 * resident span onto the frame when kept, then publish the rendered frame
 * identity as the resident axis on every commit (the commit site runs
 * unconditionally, exactly like main.c). */
static trend_axis_verdict_t production_snapshot(trend_buffer_t *trend,
                                                const char *unit, uint32_t now,
                                                main_display_frame_t *frame,
                                                main_display_trend_axis_t *resident,
                                                trend_axis_candidate_t *candidate)
{
    trend_axis_verdict_t verdict;

    main_display_format_trend(trend, now, unit, frame);
    verdict = trend_axis_update(resident, candidate, frame,
                                trend_buffer_display_scale(trend),
                                trend_buffer_window_full(trend), now);
    if (verdict.keep_resident)
    {
        float scale = trend_buffer_display_scale(trend);
        main_display_set_trend_axis(frame, resident->step, resident->top,
                                    resident->unit);
        frame->trend_minimum =
            (resident->top - 3.0f * resident->step) / scale;
        frame->trend_maximum = resident->top / scale;
    }
    main_display_get_trend_axis(frame, resident);
    return verdict;
}

/* Mid-band reading text for the resident axis: strictly inside the resident
 * span, expressed in display units (mV/k readings are stored base units
 * scaled by display_scale, so the printed number equals the displayed one). */
static void inside_text(const main_display_trend_axis_t *resident, char *out,
                        uint8_t size)
{
    int value = (int)(resident->top - 1.5f * resident->step);
    snprintf(out, size, "%d", value);
}

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
    ui_model_apply_reading(&model, "2000.0000", 9u, "kOHM", 4u, 0u);
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

    /* Prefix-scaled statistics must use the display-unit span only once;
     * otherwise sub-volt mVAC values are rounded to the misleading 0mVAC. */
    trend_buffer_reset(&trend);
    assert(trend_buffer_add(&trend, 100u, "0.300", "MVAC"));
    assert(trend_buffer_add(&trend, 120u, "0.400", "MVAC"));
    main_display_format_trend(&trend, 120u, "mVAC", &frame);
    assert(strcmp(frame.trend_stat_minimum_text, "0.30mVAC") == 0);
    assert(strcmp(frame.trend_stat_maximum_text, "0.40mVAC") == 0);
    assert(strcmp(frame.trend_stat_average_text, "0.35mVAC") == 0);

    /* Negative and sub-unit values must keep a finite, ordered scale. */
    trend_buffer_reset(&trend);
    assert(trend_buffer_add(&trend, 100u, "-0.002", "mA"));
    assert(trend_buffer_add(&trend, 120u, "-0.001", "mA"));
    main_display_format_trend(&trend, 120u, "mA", &frame);
    assert(frame.trend_has_data);
    assert(strstr(frame.y_labels[0], "mA") != 0);
    assert(strstr(frame.y_labels[2], "mA") != 0);

    /* Task 2 (review fix): the axis acceptance rules live in the shared
     * pure module trend_axis.c -- the exact code the firmware snapshot path
     * runs. Every scenario below drives the production sequence
     * (auto-fit into the frame -> trend_axis_update -> commit-time
     * publication), never a test-only shortcut. */
    {
        main_display_frame_t f1;
        main_display_frame_t f2;
        main_display_frame_t f3;
        main_display_trend_axis_t resident;
        main_display_trend_axis_t axis_before;
        trend_axis_candidate_t candidate;
        trend_axis_verdict_t v;
        char text[16];

        /* A commit over an empty buffer carries no axis: publication must
         * invalidate the resident so the next populated frame re-fits once. */
        trend_buffer_reset(&trend);
        trend_axis_init(&resident, &candidate);
        v = production_snapshot(&trend, "VDC", 0u, &frame, &resident,
                                &candidate);
        assert(!v.keep_resident && !v.axis_rebuild);
        assert(!resident.valid);

        /* Same-unit drift (VDC): identity minted once, then kept while the
         * sliding-window data bounds move. */
        trend_buffer_reset(&trend);
        trend_axis_init(&resident, &candidate);
        assert(trend_buffer_add(&trend, 100u, "1.00", "VDC"));
        assert(trend_buffer_add(&trend, 300u, "1.30", "VDC"));
        v = production_snapshot(&trend, "VDC", 400u, &f1, &resident,
                                &candidate);
        assert(v.axis_rebuild && !v.keep_resident);
        assert(resident.valid);
        assert(strcmp(resident.unit, "VDC") == 0);
        axis_before = resident;

        assert(trend_buffer_add(&trend, 700u, "1.10", "VDC"));
        v = production_snapshot(&trend, "VDC", 700u, &f2, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(strcmp(f1.trend_axis_unit, f2.trend_axis_unit) == 0);
        assert(f1.trend_axis_step == f2.trend_axis_step);
        assert(f1.trend_axis_top == f2.trend_axis_top);
        assert(strcmp(f1.y_labels[0], f2.y_labels[0]) == 0);

        assert(trend_buffer_add(&trend, 900u, "1.35", "VDC"));
        v = production_snapshot(&trend, "VDC", 900u, &f3, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(f1.trend_axis_step == f3.trend_axis_step &&
               f1.trend_axis_top == f3.trend_axis_top);
        /* Data bounds moved even though the identity did not. Keep-resident
         * frames render the projection span, so compare the raw auto-fit
         * window bounds against the rebuild frame's true data bounds. */
        {
            main_display_frame_t raw;
            main_display_format_trend(&trend, 900u, "VDC", &raw);
            assert(raw.trend_maximum > f1.trend_maximum);
        }

        /* Same unit, data leaving the resident span: a candidate is marked
         * and only a full TREND_AXIS_CANDIDATE_TIMEOUT_MS of persistence
         * promotes it. Rendered identity stays the resident's meanwhile. */
        assert(trend_buffer_add(&trend, 1000u, "2.50", "VDC"));
        v = production_snapshot(&trend, "VDC", 1000u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(candidate.valid && candidate.first_seen_ms == 1000u);
        assert(frame.trend_axis_top == axis_before.top);

        assert(trend_buffer_add(&trend, 1500u, "2.45", "VDC"));
        v = production_snapshot(&trend, "VDC", 1500u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(candidate.valid &&
               candidate.first_seen_ms == 1000u /* persisted, not restarted */);

        assert(trend_buffer_add(&trend, 2990u, "2.45", "VDC"));
        v = production_snapshot(&trend, "VDC", 2990u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);

        /* Promotion additionally requires a full trend window: the
         * filling-window freeze outranks the (now shorter) candidate
         * timeout, so cross both thresholds before expecting the swap. */
        assert(trend_buffer_add(&trend, 12000u, "2.45", "VDC"));
        v = production_snapshot(&trend, "VDC", 12000u, &frame, &resident,
                                &candidate);
        assert(!v.keep_resident && v.axis_rebuild);
        assert(!candidate.valid);
        assert(frame.trend_axis_top != axis_before.top);

        /* After promotion the system restabilizes without further rebuilds. */
        assert(trend_buffer_add(&trend, 22000u, "2.45", "VDC"));
        v = production_snapshot(&trend, "VDC", 22000u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);

        /* Scaled unit mVDC: stored base units, displayed millivolts; the
         * same rules must hold through trend_buffer_display_scale(). */
        trend_buffer_reset(&trend);
        trend_axis_init(&resident, &candidate);
        assert(trend_buffer_add(&trend, 20000u, "1000", "mVDC"));
        assert(trend_buffer_add(&trend, 20200u, "1300", "mVDC"));
        v = production_snapshot(&trend, "mVDC", 20400u, &f1, &resident,
                                &candidate);
        assert(v.axis_rebuild && !v.keep_resident);
        assert(strcmp(resident.unit, "mVDC") == 0);
        axis_before = resident;

        inside_text(&resident, text, sizeof(text));
        assert(trend_buffer_add(&trend, 20600u, text, "mVDC"));
        v = production_snapshot(&trend, "mVDC", 20600u, &f2, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(strcmp(f2.y_labels[0], f1.y_labels[0]) == 0);
        assert(strstr(f2.y_labels[0], "mVDC") != 0);

        assert(trend_buffer_add(&trend, 20800u, "1200", "mVDC"));
        v = production_snapshot(&trend, "mVDC", 20800u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(f1.trend_axis_step == resident.step &&
               f1.trend_axis_top == resident.top);

        /* Scaled unit kOHM: drift stays put, a transient outlier marks a
         * candidate, and recovery back inside the span clears it again. */
        trend_buffer_reset(&trend);
        trend_axis_init(&resident, &candidate);
        /* Timestamps span a full trend window so the filling-window
         * freeze does not mask (and clear) the candidate bookkeeping
         * exercised below. */
        /* Dense ramp 5->11 kOHM over >10 s (mirrors a live stream): the
         * window fills, the axis rescales onto the ramp, an outlier then
         * marks a candidate which persists across samples and finally
         * promotes once it outlives the (shortened) timeout. */
        {
            uint32_t t;
            uint8_t n = 0u;
            char b[8];
            for (t = 30000u; t <= 40200u; t += 200u)
            {
                int v = 5 + (int)n;
                if (v > 11) v = 11;
                snprintf(b, sizeof(b), "%d", v);
                assert(trend_buffer_add(&trend, t, b, "kOHM"));
                n++;
            }
        }
        v = production_snapshot(&trend, "kOHM", 40400u, &f1, &resident,
                                &candidate);
        assert(v.axis_rebuild && !v.keep_resident);
        assert(strcmp(resident.unit, "kOHM") == 0);
        axis_before = resident;

        inside_text(&resident, text, sizeof(text));
        assert(trend_buffer_add(&trend, 40600u, text, "kOHM"));
        v = production_snapshot(&trend, "kOHM", 40700u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);

        assert(trend_buffer_add(&trend, 40900u, "30", "kOHM"));
        v = production_snapshot(&trend, "kOHM", 41000u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(candidate.valid && candidate.first_seen_ms == 41000u);

        assert(trend_buffer_add(&trend, 42000u, "35", "kOHM"));
        v = production_snapshot(&trend, "kOHM", 42100u, &frame, &resident,
                                &candidate);
        assert(v.keep_resident && !v.axis_rebuild);
        assert(candidate.valid &&
               candidate.first_seen_ms == 41000u /* persisted */);

        assert(trend_buffer_add(&trend, 43000u, "35", "kOHM"));
        v = production_snapshot(&trend, "kOHM", 43100u, &frame, &resident,
                                &candidate);
        assert(!v.keep_resident && v.axis_rebuild);
        assert(!candidate.valid);
    }

    {
        trend_buffer_t trend;
        main_display_frame_t frame;
        trend_column_t columns[TREND_MAX_COLUMNS];
        uint16_t n;
        uint8_t y_hi, y_lo;

        memset(&frame, 0, sizeof(frame));
        trend_buffer_init(&trend);
        assert(trend_buffer_add(&trend, 0u, "1.00", "VDC"));
        assert(trend_buffer_add(&trend, 200u, "3.00", "VDC"));
        main_display_format_trend(&trend, 200u, "VDC", &frame);
        assert(frame.trend_has_data);
        main_display_format_linear_trend_labels(&frame);
        assert(strstr(frame.y_labels[0], "VDC") != 0);
        assert(strstr(frame.y_labels[2], "VDC") != 0);
        /* Top label tracks window maximum, bottom tracks minimum. */
        assert(frame.trend_maximum > 3.0f);
        assert(frame.trend_minimum < 1.0f);
        assert(strstr(frame.y_labels[0], "3") != 0);
        assert(strstr(frame.y_labels[2], "VDC") != 0);
        /* The bottom label is the minimum and is distinct from the top. */
        assert(strcmp(frame.y_labels[0], frame.y_labels[2]) != 0);

        y_hi = main_display_trend_plot_y(frame.trend_maximum,
                                         frame.trend_minimum,
                                         frame.trend_maximum);
        y_lo = main_display_trend_plot_y(frame.trend_minimum,
                                         frame.trend_minimum,
                                         frame.trend_maximum);
        assert(y_hi == 0u);
        assert(y_lo == (uint8_t)(MAIN_DISPLAY_PLOT_H - 1u));

        n = trend_buffer_project(&trend, 200u, columns, TREND_MAX_COLUMNS);
        assert(n == TREND_MAX_COLUMNS);
        {
            uint16_t i;
            bool any = false;
            for (i = 0u; i < n; i++)
            {
                if (!columns[i].occupied)
                    continue;
                any = true;
                assert(main_display_trend_plot_y(columns[i].maximum,
                                                 frame.trend_minimum,
                                                 frame.trend_maximum) <=
                       main_display_trend_plot_y(columns[i].minimum,
                                                 frame.trend_minimum,
                                                 frame.trend_maximum));
            }
            assert(any);
        }

        assert(trend_buffer_add(&trend, 400u, "1.00", "VAC"));
        assert(strcmp(trend_buffer_display_unit(&trend), "VAC") == 0);
        assert(trend_buffer_range(&trend, 400u, &frame.trend_minimum,
                                  &frame.trend_maximum));
        assert(frame.trend_minimum > 0.9f && frame.trend_maximum < 1.1f);
    }

    return 0;
}
