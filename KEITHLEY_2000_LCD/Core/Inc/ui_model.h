#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UI_MODEL_MAX_FIELD 32u

typedef struct {
    char value[UI_MODEL_MAX_FIELD];
    char unit[16];
    char function[16];
    uint16_t cursor_pos;
    bool blink;
    bool hold;
    bool trig;
    bool remote;
} ui_model_t;

void ui_model_init(ui_model_t *m);
void ui_model_apply_field(ui_model_t *m, uint8_t tag, const char *value,
                          uint8_t value_len);
void ui_model_apply_cursor(ui_model_t *m, uint16_t pos);
void ui_model_apply_blink(ui_model_t *m, bool on);
void ui_model_render(const ui_model_t *m);
