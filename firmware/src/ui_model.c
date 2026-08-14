#include <string.h>

#include "k2000_proto.h"
#include "ui_model.h"

void ui_model_init(ui_model_t *m)
{
    if (m == 0) {
        return;
    }
    memset(m, 0, sizeof(*m));
    status_bar_init(&m->status);
}

void ui_model_apply_field(ui_model_t *m, uint8_t tag, const char *value,
                          uint8_t value_len)
{
    uint8_t n;
    if (m == 0 || value == 0) {
        return;
    }
    n = value_len;
    if (n >= sizeof(m->value)) {
        n = (uint8_t)(sizeof(m->value) - 1u);
    }
    memcpy(m->value, value, n);
    m->value[n] = '\0';
    m->any_message = true;
    (void)tag;
}

void ui_model_apply_cursor(ui_model_t *m, uint16_t pos)
{
    if (m == 0) {
        return;
    }
    m->cursor_pos = pos;
    m->any_message = true;
}

void ui_model_apply_blink(ui_model_t *m, bool on)
{
    if (m == 0) {
        return;
    }
    m->blink = on;
    m->any_message = true;
}

void ui_model_apply_reading(ui_model_t *m, const char *num, uint8_t num_len,
                            const char *unit, uint8_t unit_len, uint8_t special)
{
    uint8_t n;
    if (m == 0) {
        return;
    }
    m->special = special;
    m->any_message = true;
    if (num != 0 && num_len > 0u) {
        n = num_len;
        if (n >= sizeof(m->value)) {
            n = (uint8_t)(sizeof(m->value) - 1u);
        }
        memcpy(m->value, num, n);
        m->value[n] = '\0';
    } else {
        m->value[0] = '\0';
    }
    if (unit != 0 && unit_len > 0u) {
        n = unit_len;
        if (n >= sizeof(m->unit)) {
            n = (uint8_t)(sizeof(m->unit) - 1u);
        }
        memcpy(m->unit, unit, n);
        m->unit[n] = '\0';
    } else {
        m->unit[0] = '\0';
    }
}

void ui_model_apply_status(ui_model_t *m, uint8_t tag, uint8_t value)
{
    if (m == 0) {
        return;
    }
    status_bar_set(&m->status, tag, value);
    m->any_message = true;
    /* Update only the legacy mirrors owned by this tag; other status groups
     * are independent and must not be reset by an unrelated message. */
    if (tag == K2000_TAG_STATUS_HOLD) {
        m->hold = (value & 0x80u) != 0u;
        m->trig = (value & 0x40u) != 0u;
        if ((value & 0x04u) != 0u) {
            m->rate = UI_RATE_FAST;
        } else if ((value & 0x02u) != 0u) {
            m->rate = UI_RATE_MED;
        } else if ((value & 0x01u) != 0u) {
            m->rate = UI_RATE_SLOW;
        } else {
            m->rate = UI_RATE_NONE;
        }
    } else if (tag == K2000_TAG_STATUS_REM) {
        m->remote = (value & 0x80u) != 0u;
    }
}

/* UTF-8 for the two ODS inline symbols. The TFT text font exposes matching
 * glyphs via font_text_symbol_bitmap(); storing UTF-8 keeps the unit string
 * valid C text and lets the renderer map it to the symbol glyphs. */
static const char s_sym_micro_utf8[] = "\xC2\xB5";
static const char s_sym_degree_utf8[] = "\xC2\xB0";

static void unit_append(ui_model_t *m, const char *utf8, uint8_t len)
{
    uint8_t n = (uint8_t)strlen(m->unit);
    if (n + len >= sizeof(m->unit)) {
        return;
    }
    memcpy(&m->unit[n], utf8, len);
    m->unit[n + len] = '\0';
}

void ui_model_apply_symbol(ui_model_t *m, uint8_t ctrl)
{
    if (m == 0) {
        return;
    }
    if (ctrl == K2000_TAG_SYM_MICRO) {
        unit_append(m, s_sym_micro_utf8, (uint8_t)(sizeof(s_sym_micro_utf8) - 1u));
    } else if (ctrl == K2000_TAG_SYM_DEGREE) {
        unit_append(m, s_sym_degree_utf8, (uint8_t)(sizeof(s_sym_degree_utf8) - 1u));
    }
    m->any_message = true;
}

void ui_model_apply_segment(ui_model_t *m, uint8_t ctrl)
{
    if (m == 0) {
        return;
    }
    m->segment_ctrl = ctrl;
    m->any_message = true;
}

void ui_model_apply_flush(ui_model_t *m)
{
    if (m == 0) {
        return;
    }
    m->value[0] = '\0';
    m->unit[0] = '\0';
    m->any_message = true;
}

void ui_model_render(const ui_model_t *m)
{
    (void)m;
}
