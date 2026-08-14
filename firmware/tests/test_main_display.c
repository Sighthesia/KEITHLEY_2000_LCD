#include <assert.h>
#include <string.h>

#include "main_display.h"
#include "ui_model.h"

int main(void)
{
    main_display_layout_t l;

    /* Right-aligned layout: with `slots` cells, the value ends flush at the
     * right edge of the slot area and starts slots-len cells to the left. */
    l = main_display_layout_value(6, 0u, 20u);
    assert(l.valid);
    assert(l.start_x == (20u - 6u) * FONT_DIGIT_WIDTH);
    assert(l.end_x == 20u * FONT_DIGIT_WIDTH);

    l = main_display_layout_value(3, 100u, 10u);
    assert(l.valid);
    assert(l.start_x == 100u + (10u - 3u) * FONT_DIGIT_WIDTH);
    assert(l.end_x == 100u + 10u * FONT_DIGIT_WIDTH);

    /* A value that does not fit is reported invalid and spans the slot area. */
    l = main_display_layout_value(11, 0u, 10u);
    assert(!l.valid);
    assert(l.start_x == 0u && l.end_x == 10u * FONT_DIGIT_WIDTH);

    /* Empty value: valid, nothing to draw, block collapses to the right edge. */
    l = main_display_layout_value(0, 0u, 20u);
    assert(l.valid);
    assert(l.start_x == l.end_x);

    /* Zero slots is invalid and safe. */
    l = main_display_layout_value(5, 0u, 0u);
    assert(!l.valid);

    /* Cursor x tracks the value string index within the layout and clamps. */
    {
        const uint16_t base = 100u;
        l = main_display_layout_value(5, base, 10u);
        assert(main_display_cursor_x(&l, 0) ==
               base + (10u - 5u) * FONT_DIGIT_WIDTH);
        assert(main_display_cursor_x(&l, 3) ==
               base + (10u - 5u + 3u) * FONT_DIGIT_WIDTH);
        assert(main_display_cursor_x(&l, 5) == l.end_x);
        assert(main_display_cursor_x(&l, 99) == l.end_x);   /* clamped */
        assert(main_display_cursor_x(&l, 6) == l.end_x);
        assert(main_display_cursor_x(0, 3) == 0);
    }

    /* Special-value colours. */
    assert(main_display_special_color(0) == 0xFFFFu);
    assert(main_display_special_color(1) == 0xF800u);   /* OVERFLOW red */
    assert(main_display_special_color(2) == 0xC618u);   /* "----" grey */
    assert(main_display_special_color(9) == 0xFFFFu);

    /* Full frame from a model: normal reading + HOLD + REM + TRIG. */
    {
        ui_model_t m;
        main_display_frame_t f;

        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
        ui_model_apply_status(&m, 0x08u, 0x80u | 0x40u);
        ui_model_apply_status(&m, 0x06u, 0x80u);

        main_display_format(&m, &f);
        assert(f.value_len == 6);
        assert(memcmp(f.value, "1.2345", 6) == 0 && f.value[6] == '\0');
        assert(f.unit_len == 3);
        assert(memcmp(f.unit, "VDC", 3) == 0);
        assert(f.special == 0);
        assert(f.value_color == 0xFFFFu);
        assert(f.start_x == (MAIN_DISPLAY_MAX_SLOTS - 6u) * FONT_DIGIT_WIDTH);
        assert(f.end_x == MAIN_DISPLAY_MAX_SLOTS * FONT_DIGIT_WIDTH);
        assert(f.reading_y == MAIN_DISPLAY_READING_Y);
        assert(f.unit_y == MAIN_DISPLAY_TOP_BAND_Y);
        assert(f.status_y == MAIN_DISPLAY_TOP_BAND_Y);
        /* unit "VDC" is 3 chars, left-aligned at the top band's left edge */
        assert(f.unit_x == 0u);
        assert(!f.unit_placeholder);
        /* status block right-aligned to the UI width */
        assert(f.status_x == MAIN_DISPLAY_UI_WIDTH -
                            ((4u + 3u + 4u) * FONT_TEXT_WIDTH +
                             2u * MAIN_DISPLAY_STATUS_LABEL_GAP));
        /* no blink -> cursor hidden */
        assert(!f.cursor_visible);
        assert(f.status_count == 3);
        assert(strcmp(f.status_text[0], "HOLD") == 0);
        assert(strcmp(f.status_text[1], "REM") == 0);
        assert(strcmp(f.status_text[2], "TRIG") == 0);
    }

    /* Blink + cursor visible; special reading uses the special colour. */
    {
        ui_model_t m;
        main_display_frame_t f;
        main_display_layout_t l;

        ui_model_init(&m);
        ui_model_apply_reading(&m, "----", 4, "", 0, 2);
        ui_model_apply_cursor(&m, 2);
        ui_model_apply_blink(&m, true);
        main_display_format(&m, &f);
        assert(f.special == 2);
        assert(f.value_color == 0xC618u);
        assert(f.cursor_visible);
        assert(f.cursor_y == MAIN_DISPLAY_CURSOR_Y);
        l = main_display_layout_value(4, MAIN_DISPLAY_READING_X,
                                      MAIN_DISPLAY_MAX_SLOTS);
        assert(f.cursor_x == main_display_cursor_x(&l, 2));
        assert(f.status_count == 0);
    }

    /* NULL model -> empty frame, NULL frame -> no-op. */
    {
        main_display_frame_t f;
        memset(&f, 0xAA, sizeof(f));
        main_display_format(0, &f);
        assert(f.value_len == 0 && f.value[0] == '\0');
        assert(f.status_count == 0);
        main_display_format(0, 0);
    }

    /* No-data state (startup): no value/status; unit shows the "Range ?"
     * placeholder; the reading band holds seven '?' slots right-aligned to
     * the reading position, vertically centred in the band. */
    {
        ui_model_t m;
        main_display_frame_t f;
        ui_model_init(&m);
        main_display_format(&m, &f);
        assert(f.no_data);
        assert(f.value_len == 0u);
        assert(f.value[0] == '\0');
        assert(!f.cursor_visible);
        assert(f.start_x == 0u && f.end_x == 0u);
        assert(f.unit_placeholder);
        assert(f.no_data_x == MAIN_DISPLAY_UI_WIDTH -
                              MAIN_DISPLAY_NO_DATA_SLOTS * FONT_DIGIT_WIDTH);
        assert(f.no_data_y == MAIN_DISPLAY_READING_Y +
                              (MAIN_DISPLAY_READING_H - FONT_TEXT_HEIGHT) / 2u);
        assert(f.status_count == 0u);
        assert(f.unit_len == 0u);
    }

    /* A status-only message exits the no-data state (Q4-a): the frame is no
     * longer marked no_data even though no reading is present; the unit
     * placeholder stays until a unit/range arrives. */
    {
        ui_model_t m;
        main_display_frame_t f;
        ui_model_init(&m);
        ui_model_apply_status(&m, 0x06u, 0x80u);
        main_display_format(&m, &f);
        assert(!f.no_data);
        assert(f.value_len == 0u);
        assert(f.unit_placeholder);
        assert(f.status_count == 1u);
        assert(strcmp(f.status_text[0], "REM") == 0);
        assert(f.status_x == MAIN_DISPLAY_UI_WIDTH -
                             (3u * FONT_TEXT_WIDTH));
    }

    /* Footer spec: DCV/ohm rate -> Read/s only; ACV/ACI -> bandwidth +
     * Read/s; no rate/unit -> empty. */
    {
        ui_model_t m;
        main_display_frame_t f;

        /* FAST + VDC -> "500 Read/s" */
        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
        ui_model_apply_status(&m, 0x08u, 0x04u);   /* FAST */
        main_display_format(&m, &f);
        assert(f.footer_spec_len == strlen("500 Read/s"));
        assert(memcmp(f.footer_spec, "500 Read/s", f.footer_spec_len) == 0);
        assert(f.footer_spec_x == MAIN_DISPLAY_FOOTER_SPEC_X);
        assert(f.footer_spec_y == MAIN_DISPLAY_FOOTER_SPEC_Y);

        /* MED + VDC -> "50 Read/s" */
        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
        ui_model_apply_status(&m, 0x08u, 0x02u);   /* MED */
        main_display_format(&m, &f);
        assert(f.footer_spec_len == strlen("50 Read/s"));
        assert(memcmp(f.footer_spec, "50 Read/s", f.footer_spec_len) == 0);

        /* SLOW + VDC -> "5 Read/s" */
        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
        ui_model_apply_status(&m, 0x08u, 0x01u);   /* SLOW */
        main_display_format(&m, &f);
        assert(f.footer_spec_len == strlen("5 Read/s"));
        assert(memcmp(f.footer_spec, "5 Read/s", f.footer_spec_len) == 0);

        /* FAST + ACV -> "300 Hz - 300 kHz 500 Read/s" */
        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "ACV", 3, 0);
        ui_model_apply_status(&m, 0x08u, 0x04u);
        main_display_format(&m, &f);
        assert(f.footer_spec_len == strlen("300 Hz - 300 kHz 500 Read/s"));
        assert(memcmp(f.footer_spec, "300 Hz - 300 kHz 500 Read/s",
                      f.footer_spec_len) == 0);

        /* No rate -> empty footer. */
        ui_model_init(&m);
        ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
        main_display_format(&m, &f);
        assert(f.footer_spec_len == 0u && f.footer_spec[0] == '\0');

        /* No unit -> empty footer even with a rate. */
        ui_model_init(&m);
        ui_model_apply_status(&m, 0x08u, 0x04u);
        main_display_format(&m, &f);
        assert(f.footer_spec_len == 0u && f.footer_spec[0] == '\0');
    }

    return 0;
}
