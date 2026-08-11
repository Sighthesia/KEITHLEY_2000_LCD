#pragma once

#include <stdint.h>

/* 4x8 key matrix, rows PC13/PC14/PC15/PB10 (row 0..3) x cols PB0..PB7
 * (col 0..7). Pure logic only: no HAL, no GPIO. */
#define KEYPAD_ROWS 4u
#define KEYPAD_COLS 8u
#define KEYPAD_DEBOUNCE_MS 20u
#define KEYPAD_RELEASE_CODE 0x40u

/* Encode a matrix position into the raw code passed to keypad_scan().
 * 0 means "no key pressed"; the +1 offset keeps position (0,0) distinct
 * from the no-key sentinel. */
#define KEYPAD_RAW(row, col) ((int)((row) * KEYPAD_COLS + (col) + 1))

typedef struct {
    uint8_t state;
    int candidate_raw;
    int held_raw;
    uint32_t tick;
} keypad_t;

void keypad_init(keypad_t *k);
int keypad_code(uint8_t row, uint8_t col);
int keypad_scan(keypad_t *k, int raw_code, uint32_t tick_ms);
