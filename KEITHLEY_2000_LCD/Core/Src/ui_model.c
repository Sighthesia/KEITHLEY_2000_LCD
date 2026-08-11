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
    } else if (tag == K2000_TAG_STATUS_REM) {
        m->remote = (value & 0x80u) != 0u;
    }
}

void ui_model_render(const ui_model_t *m)
{
    (void)m;
}
