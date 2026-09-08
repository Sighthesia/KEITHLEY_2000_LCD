#pragma once

#include <stdint.h>

/* 4x8 key matrix, rows PB0..PB3 (row 0..3, outputs, idle low) x cols
 * PB4..PB11 (col 0..7 = R9..R16 = PB11..PB4 reversed, inputs, 33k
 * pull-down to GND per Netlist_Schematic1_2026-09-08.tel). Logical
 * (row,col) codes follow the ODS TX table; the physical pin order lives
 * in hal_board.c. Pure logic only: no HAL, no GPIO. */
#define KEYPAD_ROWS 4u
#define KEYPAD_COLS 8u
/* Integrator debounce for silicone carbon contacts: a press/release only
 * confirms after consecutive stable scans spanning the window below.
 * Asymmetric by design: presses stay snappy (quick taps pass) while
 * releases are slow, so rocking a held carbon pill (tens of ms dropouts)
 * cannot split one hold into press/release pairs. Fix input levels first
 * (33k pull-down per BOM); no window covers a threshold-dancing input. */
#define KEYPAD_PRESS_MS 15u
#define KEYPAD_PRESS_SAMPLES 2u
#define KEYPAD_RELEASE_MS 100u
#define KEYPAD_RELEASE_SAMPLES 2u
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
    uint8_t count;
} keypad_t;

void keypad_init(keypad_t *k);
int keypad_code(uint8_t row, uint8_t col);
/* Human-readable key name for bench diagnosis ("SHIFT", "DCV", ... "EXIT").
 * Empty string for unwired/unknown cells. Table mirrors s_key_map below. */
const char *keypad_name(uint8_t row, uint8_t col);
int keypad_scan(keypad_t *k, int raw_code, uint32_t tick_ms);
