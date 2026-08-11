#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "status_bar.h"

#define UI_MODEL_MAX_FIELD 32u
#define UI_MODEL_MAX_UNIT 16u

/* UI model for the reading scene (milestone-1). Pure logic, host-testable.
 * The reading is kept as a split numeric prefix (value) plus unit suffix; the
 * special code flags OVERFLOW / no-reading; the status TAG values live in an
 * embedded status_bar_t. cursor_pos / blink mirror the host POS / 0x0B events. */
typedef struct {
    char value[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    char function[16];
    uint16_t cursor_pos;
    bool blink;
    /* True once any host message (field/status/cursor/blink) has been
     * applied. Drives the no-data state (Q4-a): before the first message
     * the reading page shows the "NO DATA" hint instead of a value. */
    bool any_message;
    /* Legacy convenience mirrors of the status tags (HOLD/TRIG from 0x08,
     * REM from 0x06). status_bar_t is the authoritative store. */
    bool hold;
    bool trig;
    bool remote;
    /* 0 = normal reading, 1 = OVERFLOW (red), 2 = "----" no reading (grey). */
    uint8_t special;
    status_bar_t status;
} ui_model_t;

void ui_model_init(ui_model_t *m);
void ui_model_apply_field(ui_model_t *m, uint8_t tag, const char *value,
                          uint8_t value_len);
void ui_model_apply_cursor(ui_model_t *m, uint16_t pos);
void ui_model_apply_blink(ui_model_t *m, bool on);
void ui_model_apply_reading(ui_model_t *m, const char *num, uint8_t num_len,
                            const char *unit, uint8_t unit_len, uint8_t special);
void ui_model_apply_status(ui_model_t *m, uint8_t tag, uint8_t value);
void ui_model_render(const ui_model_t *m);
