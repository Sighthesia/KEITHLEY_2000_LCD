#include <assert.h>
#include <string.h>

#include "ui_model.h"

int main(void)
{
    ui_model_t m;

    ui_model_init(&m);
    assert(m.value[0] == '\0');
    assert(m.unit[0] == '\0');
    assert(m.special == 0);
    assert(m.cursor_pos == 0);
    assert(!m.blink);
    assert(!m.hold && !m.trig && !m.remote);
    /* status_bar embedded store is initialised with all six status tags. */
    assert(!status_bar_active(&m.status, 0x08u, 0x80u));

    /* Legacy API still works (backward compatibility). */
    ui_model_apply_field(&m, 0x01u, "123", 3);
    assert(memcmp(m.value, "123", 3) == 0 && m.value[3] == '\0');
    assert(m.unit[0] == '\0');

    /* apply_reading stores the split numeric prefix + unit + special. */
    ui_model_apply_reading(&m, "1.2345", 6, "VDC", 3, 0);
    assert(memcmp(m.value, "1.2345", 6) == 0 && m.value[6] == '\0');
    assert(memcmp(m.unit, "VDC", 3) == 0 && m.unit[3] == '\0');
    assert(m.special == 0);

    ui_model_apply_reading(&m, "OVERFLOW", 8, "", 0, 1);
    assert(memcmp(m.value, "OVERFLOW", 8) == 0);
    assert(m.unit[0] == '\0');
    assert(m.special == 1);

    ui_model_apply_reading(&m, "----", 4, "", 0, 2);
    assert(m.special == 2);
    assert(memcmp(m.value, "----", 4) == 0);

    /* Empty/out-of-range input is bounded and never overflows. */
    ui_model_apply_reading(&m, 0, 0, 0, 0, 0);
    assert(m.value[0] == '\0');
    assert(m.unit[0] == '\0');
    {
        char big[40];
        memset(big, '1', 39);
        ui_model_apply_reading(&m, big, 39, big, 39, 0);
        assert(strlen(m.value) == UI_MODEL_MAX_FIELD - 1u);
        assert(strlen(m.unit) == UI_MODEL_MAX_UNIT - 1u);
        assert(m.value[UI_MODEL_MAX_FIELD - 1u] == '\0');
        assert(m.unit[UI_MODEL_MAX_UNIT - 1u] == '\0');
    }

    /* Cursor + blink events. */
    ui_model_apply_cursor(&m, 3);
    assert(m.cursor_pos == 3);
    ui_model_apply_blink(&m, true);
    assert(m.blink);
    ui_model_apply_blink(&m, false);
    assert(!m.blink);

    /* apply_status forwards into the embedded status_bar_t... */
    ui_model_apply_status(&m, 0x08u, 0x80u);
    assert(status_bar_active(&m.status, 0x08u, 0x80u));
    assert(status_bar_core_active(&m.status, 0));   /* HOLD */
    assert(!status_bar_core_active(&m.status, 3));  /* TRIG off */

    /* ...and updates the matching legacy mirrors only. */
    assert(m.hold);
    assert(!m.trig);
    assert(!m.remote);

    ui_model_apply_status(&m, 0x06u, 0x80u);   /* REM on */
    assert(m.remote);
    assert(m.hold);                            /* HOLD untouched */
    assert(status_bar_active(&m.status, 0x06u, 0x80u));

    /* A REM update must not clear HOLD; a HOLD clear clears its mirrors. */
    ui_model_apply_status(&m, 0x08u, 0x00u);
    assert(!m.hold && !m.trig);
    assert(status_bar_core_active(&m.status, 1));   /* REM still on */

    /* NULL safety. */
    ui_model_init(0);
    ui_model_apply_field(0, 0, 0, 0);
    ui_model_apply_cursor(0, 0);
    ui_model_apply_blink(0, false);
    ui_model_apply_reading(0, 0, 0, 0, 0, 0);
    ui_model_apply_status(0, 0, 0);

    return 0;
}
