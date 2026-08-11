#include "keypad.h"

/* Key codes from the ODS TX table. 0 = unknown/unwired cell (returns 0 and
 * never emits). Recorded discrepancy vs the implementation-plan draft: the
 * plan test expected (row1,col2)=0x4C, but the ODS TX table maps
 * (row1,col2)=0x4B (UP ARROW) and (row1,col3)=0x4C (AUTO); this module
 * follows the ODS. */
static const uint8_t s_key_map[KEYPAD_ROWS][KEYPAD_COLS] = {
    { 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 },
    { 0x00, 0x00, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50 },
    { 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58 },
    { 0x00, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60 },
};

typedef enum {
    KP_STATE_IDLE,
    KP_STATE_PRESS_WAIT,
    KP_STATE_PRESSED,
    KP_STATE_RELEASE_WAIT,
} keypad_state_t;

void keypad_init(keypad_t *k)
{
    if (k == 0) {
        return;
    }
    k->state = KP_STATE_IDLE;
    k->candidate_raw = 0;
    k->held_raw = 0;
    k->tick = 0;
}

int keypad_code(uint8_t row, uint8_t col)
{
    if (row >= KEYPAD_ROWS || col >= KEYPAD_COLS) {
        return 0;
    }
    return s_key_map[row][col];
}

static int code_from_raw(int raw)
{
    uint8_t row;
    uint8_t col;
    if (raw <= 0 || raw > (int)(KEYPAD_ROWS * KEYPAD_COLS)) {
        return 0;
    }
    row = (uint8_t)((raw - 1) / (int)KEYPAD_COLS);
    col = (uint8_t)((raw - 1) % (int)KEYPAD_COLS);
    return keypad_code(row, col);
}

int keypad_scan(keypad_t *k, int raw_code, uint32_t tick_ms)
{
    int out = 0;
    int code;

    if (k == 0) {
        return 0;
    }

    switch (k->state) {
    case KP_STATE_IDLE:
        if (raw_code != 0) {
            k->candidate_raw = raw_code;
            k->tick = tick_ms;
            k->state = KP_STATE_PRESS_WAIT;
        }
        break;

    case KP_STATE_PRESS_WAIT:
        if (raw_code == 0) {
            k->state = KP_STATE_IDLE;
        } else if (raw_code != k->candidate_raw) {
            k->candidate_raw = raw_code;
            k->tick = tick_ms;
        } else if ((tick_ms - k->tick) >= KEYPAD_DEBOUNCE_MS) {
            code = code_from_raw(raw_code);
            k->held_raw = raw_code;
            k->state = KP_STATE_PRESSED;
            out = code;
        }
        break;

    case KP_STATE_PRESSED:
        if (raw_code == 0) {
            k->tick = tick_ms;
            k->state = KP_STATE_RELEASE_WAIT;
        } else if (raw_code != k->held_raw) {
            k->candidate_raw = raw_code;
            k->tick = tick_ms;
            k->state = KP_STATE_PRESS_WAIT;
        }
        break;

    case KP_STATE_RELEASE_WAIT:
        if (raw_code != 0) {
            if (raw_code == k->held_raw) {
                k->state = KP_STATE_PRESSED;
            } else {
                k->candidate_raw = raw_code;
                k->tick = tick_ms;
                k->state = KP_STATE_PRESS_WAIT;
            }
        } else if ((tick_ms - k->tick) >= KEYPAD_DEBOUNCE_MS) {
            code = code_from_raw(k->held_raw);
            if (code != 0) {
                out = KEYPAD_RELEASE_CODE;
            }
            k->held_raw = 0;
            k->state = KP_STATE_IDLE;
        }
        break;

    default:
        k->state = KP_STATE_IDLE;
        break;
    }

    return out;
}
