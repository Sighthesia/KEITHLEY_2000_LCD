#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "status_bar.h"

#define UI_MODEL_MAX_FIELD 32u
#define UI_MODEL_MAX_UNIT 16u

/* Integration rate (0x08 FAST/MED/SLOW), derived from the status bits so the
 * footer spec (N/s or AC bandwidth) can be chosen. */
typedef enum {
    UI_RATE_NONE = 0,
    UI_RATE_FAST,
    UI_RATE_MED,
    UI_RATE_SLOW,
} ui_rate_t;

typedef enum {
    UI_FUNCTION_UNKNOWN = 0,
    UI_FUNCTION_DC_VOLTAGE,
    UI_FUNCTION_AC_VOLTAGE,
    UI_FUNCTION_DC_CURRENT,
    UI_FUNCTION_AC_CURRENT,
    UI_FUNCTION_2W_OHM,
    UI_FUNCTION_4W_OHM,
    UI_FUNCTION_FREQUENCY,
    UI_FUNCTION_PERIOD,
    UI_FUNCTION_TEMPERATURE,
} ui_function_t;

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
    /* Integration rate from the 0x08 status group (FAST=0x04, MED=0x02,
     * SLOW=0x01); UI_RATE_NONE when no rate bit is set. */
    ui_rate_t rate;
    ui_function_t function_id;
    bool auto_range;
    bool filter_on;
    bool buffer_recall;
    /* 0 = normal reading, 1 = OVERFLOW (red), 2 = "----" no reading (grey). */
    uint8_t special;
    /* VFD digit-segment control tag bits (0x18 first-only, 0x1A 2nd-only,
     * 0x7F full digit). Kept raw for a faithful log; TFT rendering may ignore.
     */
    uint8_t segment_ctrl;
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
/* Inline text-tag events: append a symbol (u=0x10 / degree=0x13) to the unit
 * as UTF-8, store a segment-control tag, or clear the current field. */
void ui_model_apply_symbol(ui_model_t *m, uint8_t ctrl);
void ui_model_apply_segment(ui_model_t *m, uint8_t ctrl);
void ui_model_apply_flush(ui_model_t *m);
void ui_model_render(const ui_model_t *m);
ui_function_t ui_model_infer_function(const char *unit);
