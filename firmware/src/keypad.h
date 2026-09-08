#pragma once

#include <stdint.h>

/* 4x8 key matrix, rows PB0..PB3 (row 0..3, outputs, idle low) x cols
 * PB4..PB11 (col 0..7 = R9..R16 = PB11..PB4 reversed, inputs, 33k
 * pull-down to GND per Netlist_Schematic1_2026-09-08.tel). Logical
 * (row,col) codes follow the ODS TX table; the physical pin order lives
 * in hal_board.c. Pure logic only: no HAL, no GPIO. */
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
/* Human-readable key name for bench diagnosis ("SHIFT", "DCV", ... "EXIT").
 * Empty string for unwired/unknown cells. Table mirrors s_key_map below. */
const char *keypad_name(uint8_t row, uint8_t col);
int keypad_scan(keypad_t *k, int raw_code, uint32_t tick_ms);
