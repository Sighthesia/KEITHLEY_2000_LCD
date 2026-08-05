#include <string.h>

#include "ui_model.h"

void ui_model_init(ui_model_t *m)
{
    if (m == 0) {
        return;
    }
    memset(m, 0, sizeof(*m));
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
    (void)tag;
}

void ui_model_apply_cursor(ui_model_t *m, uint16_t pos)
{
    if (m == 0) {
        return;
    }
    m->cursor_pos = pos;
}

void ui_model_apply_blink(ui_model_t *m, bool on)
{
    if (m == 0) {
        return;
    }
    m->blink = on;
}

void ui_model_render(const ui_model_t *m)
{
    (void)m;
}
