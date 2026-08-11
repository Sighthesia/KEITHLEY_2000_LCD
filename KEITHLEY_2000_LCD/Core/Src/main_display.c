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
    if (n == 0u) {
        n = MAIN_DISPLAY_PLACEHOLDER_SLOTS;
        memset(f->value, '_', n);
        f->value[n] = '\0';
        f->value_len = n;
        f->placeholder = true;
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
    f->unit_y = MAIN_DISPLAY_UNIT_ROW_Y;
    f->status_y = MAIN_DISPLAY_STATUS_BAR_Y;
    f->unit_x = (uint16_t)(MAIN_DISPLAY_UI_WIDTH -
                           (uint16_t)f->unit_len * FONT_TEXT_WIDTH);

    layout = main_display_layout_value(
        f->value_len,
        f->placeholder ? (uint16_t)((MAIN_DISPLAY_MAX_SLOTS -
                                     MAIN_DISPLAY_PLACEHOLDER_SLOTS) *
                                    FONT_DIGIT_WIDTH)
                       : MAIN_DISPLAY_READING_X,
        f->placeholder ? MAIN_DISPLAY_PLACEHOLDER_SLOTS : MAIN_DISPLAY_MAX_SLOTS);
    if (f->placeholder) {
        layout.start_x = (uint16_t)((MAIN_DISPLAY_UI_WIDTH -
                                     MAIN_DISPLAY_PLACEHOLDER_SLOTS *
                                     FONT_DIGIT_WIDTH) / 2u);
        layout.end_x = (uint16_t)(layout.start_x +
                                  MAIN_DISPLAY_PLACEHOLDER_SLOTS *
                                  FONT_DIGIT_WIDTH);
    }
    f->start_x = layout.start_x;
    f->end_x = layout.end_x;

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
}
