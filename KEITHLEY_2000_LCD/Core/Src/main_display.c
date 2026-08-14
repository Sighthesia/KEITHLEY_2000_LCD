#include <string.h>

#include "main_display.h"

main_display_layout_t main_display_layout_value(uint8_t value_len,
                                                uint16_t start_x,
                                                uint8_t slots)
{
    main_display_layout_t l;
    uint16_t right = (uint16_t)(start_x + (uint16_t)slots * FONT_DIGIT_WIDTH);

    l.start_x = right;
    l.end_x = right;
    l.valid = 0;
    if (slots == 0u) {
        return l;
    }
    if (value_len == 0u) {
        l.valid = 1;
        return l;
    }
    if (value_len > slots) {
        l.start_x = start_x;
        l.end_x = right;
        return l;
    }
    l.start_x = (uint16_t)(right - (uint16_t)value_len * FONT_DIGIT_WIDTH);
    l.end_x = right;
    l.valid = 1;
    return l;
}

uint16_t main_display_cursor_x(const main_display_layout_t *layout,
                               uint16_t cursor_pos)
{
    uint16_t maxi;
    if (layout == 0 || !layout->valid) {
        return (layout != 0) ? layout->end_x : 0u;
    }
    maxi = (uint16_t)((layout->end_x - layout->start_x) / FONT_DIGIT_WIDTH);
    if (cursor_pos > maxi) {
        cursor_pos = maxi;
    }
    return (uint16_t)(layout->start_x + (uint16_t)cursor_pos * FONT_DIGIT_WIDTH);
}

uint16_t main_display_special_color(uint8_t special)
{
    if (special == 1u) {
        return 0xF800u;   /* OVERFLOW: red */
    }
    if (special == 2u) {
        return 0xC618u;   /* no reading "----": grey */
    }
    return 0xFFFFu;       /* normal: white */
}

/* Does the unit indicate AC (ACV/ACI)? "AC" anywhere in the unit string. */
static bool unit_is_ac(const char *unit)
{
    if (unit == 0) {
        return false;
    }
    for (; *unit != '\0'; unit++) {
        if (unit[0] == 'A' && unit[1] == 'C') {
            return true;
        }
    }
    return false;
}

/* Compose the footer spec line for a rate + unit:
 *   DCV/ohm   -> "500 Read/s" / "50 Read/s" / "5 Read/s"
 *   ACV/ACI   -> "300 Hz - 300 kHz  500 Read/s" (bandwidth + Read/s)
 * Empty when the rate is NONE or the unit is empty. */
static void main_display_footer_spec(const char *unit, ui_rate_t rate,
                                     char *out, uint8_t out_size,
                                     uint8_t *out_len)
{
    static const char *const k_reads[] = { "", "500 Read/s", "50 Read/s",
                                            "5 Read/s" };
    static const char *const k_bw[] = { "", "300 Hz - 300 kHz",
                                         "30 Hz - 300 kHz", "3 Hz - 300 kHz" };
    const char *reads;
    uint8_t n = 0u;

    if (out != 0) {
        out[0] = '\0';
    }
    if (out_len != 0) {
        *out_len = 0u;
    }
    if (out == 0 || out_len == 0 || out_size == 0u) {
        return;
    }
    if (unit == 0 || unit[0] == '\0' || rate == UI_RATE_NONE) {
        return;
    }
    if (unit_is_ac(unit)) {
        const char *bw = k_bw[rate];
        while (*bw != '\0' && n + 1u < out_size) {
            out[n++] = *bw++;
        }
        if (n + 1u < out_size) {
            out[n++] = ' ';   /* separator before the Read/s */
        }
    }
    reads = k_reads[rate];
    while (*reads != '\0' && n + 1u < out_size) {
        out[n++] = *reads++;
    }
    out[n] = '\0';
    *out_len = n;
}

void main_display_format(const ui_model_t *m, main_display_frame_t *f)
{
    const status_bar_indicator_t *ind;
    main_display_layout_t layout;
    uint8_t i;
    uint8_t n;

    if (f == 0) {
        return;
    }
    memset(f, 0, sizeof(*f));
    if (m == 0) {
        return;
    }

    n = (uint8_t)strlen(m->value);
    if (n > 0u) {
        if (n >= sizeof(f->value)) {
            n = (uint8_t)(sizeof(f->value) - 1u);
        }
        memcpy(f->value, m->value, n);
    }
    f->value[n] = '\0';
    f->value_len = n;
    if (!m->any_message) {
        f->no_data = true;
    }
    if (m->unit[0] != '\0') {
        n = (uint8_t)strlen(m->unit);
        if (n >= sizeof(f->unit)) {
            n = (uint8_t)(sizeof(f->unit) - 1u);
        }
        memcpy(f->unit, m->unit, n);
    } else {
        n = 0u;
    }
    f->unit[n] = '\0';
    f->unit_len = n;

    f->special = m->special;
    f->value_color = main_display_special_color(m->special);
    f->reading_y = MAIN_DISPLAY_READING_Y;
    f->unit_y = MAIN_DISPLAY_TOP_BAND_Y;
    f->status_y = MAIN_DISPLAY_TOP_BAND_Y;
    /* Unit/range sits at the left edge of the top band; while empty it is
     * replaced by the "Range ?" placeholder (grey). */
    f->unit_x = 0u;
    f->unit_placeholder = (f->unit_len == 0u);

    /* No-data hint: seven '?' big-glyph slots right-aligned to the reading
     * position, filling the reading band like normal big digits. */
    f->no_data_x = (uint16_t)(MAIN_DISPLAY_UI_WIDTH -
                              MAIN_DISPLAY_NO_DATA_SLOTS * FONT_DIGIT_WIDTH);
    f->no_data_y = MAIN_DISPLAY_READING_Y;

    if (!f->no_data) {
        layout = main_display_layout_value(f->value_len, MAIN_DISPLAY_READING_X,
                                           MAIN_DISPLAY_MAX_SLOTS);
        f->start_x = layout.start_x;
        f->end_x = layout.end_x;
    } else {
        /* No digits in the no-data state; never leave stale frame values. */
        f->start_x = 0u;
        f->end_x = 0u;
    }

    /* Cursor: POS indexes into the value string, clamped to its length;
     * only shown while blink (edit mode) is active and a value exists. */
    if (m->blink && f->value_len > 0u) {
        f->cursor_visible = true;
        f->cursor_x = main_display_cursor_x(&layout, m->cursor_pos);
        f->cursor_y = MAIN_DISPLAY_CURSOR_Y;
    }

    /* Status: milestone-1 core subset, active indicators only. */
    for (i = 0u; i < STATUS_BAR_CORE_COUNT; i++) {
        if (status_bar_core_get(i, &ind) &&
            status_bar_core_active(&m->status, i)) {
            n = (uint8_t)strlen(ind->label);
            if (n >= MAIN_DISPLAY_STATUS_LABEL_MAX) {
                n = MAIN_DISPLAY_STATUS_LABEL_MAX - 1u;
            }
            memcpy(f->status_text[f->status_count], ind->label, n);
            f->status_text[f->status_count][n] = '\0';
            f->status_count++;
        }
    }
    /* Right-align the whole status block to the top band's right edge. */
    f->status_x = 0u;
    if (f->status_count > 0u) {
        uint16_t w = 0u;
        for (i = 0u; i < f->status_count; i++) {
            n = (uint8_t)strlen(f->status_text[i]);
            w = (uint16_t)(w + (uint16_t)n * FONT_TEXT_WIDTH);
            if (i + 1u < f->status_count) {
                w = (uint16_t)(w + MAIN_DISPLAY_STATUS_LABEL_GAP);
            }
        }
        f->status_x = (uint16_t)(MAIN_DISPLAY_UI_WIDTH - w);
    }

    /* Footer spec line: depends on integration rate + measured unit. */
    main_display_footer_spec(m->unit, m->rate, f->footer_spec,
                             sizeof(f->footer_spec), &f->footer_spec_len);
    f->footer_spec_x = MAIN_DISPLAY_FOOTER_SPEC_X;
    f->footer_spec_y = MAIN_DISPLAY_FOOTER_SPEC_Y;
}
