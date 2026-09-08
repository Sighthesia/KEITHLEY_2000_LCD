#pragma once

#include <stdint.h>

/* 4x8 key matrix, rows PB0..PB3 (row 0..3, outputs, idle low) x cols
 * PB4..PB11 (col 0..7 = R9..R16 = PB11..PB4 reversed, inputs, 33k
 * pull-down to GND per Netlist_Schematic1_2026-09-08.tel). Logical
 * (row,col) codes follow the ODS TX table; the physical pin order lives
 * in hal_board.c. Pure logic only: no HAL, no GPIO. */
#define KEYPAD_ROWS 4u
#define KEYPAD_COLS 8u
#define KEYPAD_CELLS 32u
/* Simple timer debounce: a changed contact set commits only after a full
 * 100 ms of stability. No double-click / long-press / repeat handling by
 * design; combo keys (SHIFT + key, ...) are fully supported: every added
 * key emits its code, KEYPAD_RELEASE_CODE goes out once the set empties. */
#define KEYPAD_DEBOUNCE_MS 100u
#define KEYPAD_RELEASE_CODE 0x40u
/* One matrix cell in a contact-set mask. */
#define KEYPAD_CELL(row, col) (((uint32_t)1u << ((row) * KEYPAD_COLS + (col))))
#define KEYPAD_PENDING_DEPTH 8u

typedef struct {
    uint32_t stable;
    uint32_t candidate;
    uint32_t tick;
    uint8_t pend[KEYPAD_PENDING_DEPTH];
    uint8_t head;
    uint8_t tail;
} keypad_t;

void keypad_init(keypad_t *k);
int keypad_code(uint8_t row, uint8_t col);
/* Human-readable key name for bench diagnosis ("SHIFT", "DCV", ... "EXIT").
 * Empty string for unwired/unknown cells. Table mirrors s_key_map below. */
const char *keypad_name(uint8_t row, uint8_t col);
/* Feed the raw contact set (KEYPAD_CELL bits, 0 = none). Returns the next
 * queued code (one press code per newly added key, KEYPAD_RELEASE_CODE
 * once the set empties), or 0. Call repeatedly per poll to drain. */
int keypad_scan(keypad_t *k, uint32_t mask, uint32_t tick_ms);
