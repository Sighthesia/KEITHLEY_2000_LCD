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

/* Display names for the same cells (ODS TX "Pressed button" + schematic
 * labels). "" marks the unwired cells, mirroring the 0x00 codes above. */
static const char *const s_key_names[KEYPAD_ROWS][KEYPAD_COLS] = {
    { "SHIFT", "DCV", "ACV", "DCI", "ACI", "OHM", "OHM4W", "FREQ" },
    { "", "", "UP", "AUTO", "DOWN", "ENTER", "RIGHT", "TEMP" },
    { "LOCAL", "EXTTRIG", "TRIG", "STORE", "RECALL", "FILTER", "REL", "LEFT" },
    { "", "OPEN", "CLOSE", "STEP", "SCAN", "DIGITS", "RATE", "EXIT" },
};

const char *keypad_name(uint8_t row, uint8_t col)
{
    if (row >= KEYPAD_ROWS || col >= KEYPAD_COLS) {
        return "";
    }
    return s_key_names[row][col];
}

void keypad_init(keypad_t *k)
{
    uint8_t i;
    if (k == 0) {
        return;
    }
    k->stable = 0u;
    k->candidate = 0u;
    k->tick = 0u;
    for (i = 0u; i < KEYPAD_PENDING_DEPTH; i++) {
        k->pend[i] = 0u;
    }
    k->head = 0u;
    k->tail = 0u;
}

int keypad_code(uint8_t row, uint8_t col)
{
    if (row >= KEYPAD_ROWS || col >= KEYPAD_COLS) {
        return 0;
    }
    return s_key_map[row][col];
}

/* Drop unwired cells: they can never emit. */
static uint32_t sanitize(uint32_t mask)
{
    uint32_t out = 0u;
    uint8_t i;
    for (i = 0u; i < KEYPAD_CELLS; i++) {
        if (((mask >> i) & 1u) != 0u &&
            keypad_code((uint8_t)(i / KEYPAD_COLS),
                        (uint8_t)(i % KEYPAD_COLS)) != 0) {
            out |= ((uint32_t)1u << i);
        }
    }
    return out;
}

static void push_code(keypad_t *k, int code)
{
    uint8_t next;
    if (code <= 0 || code > 0xFF) {
        return;
    }
    next = (uint8_t)((k->tail + 1u) % KEYPAD_PENDING_DEPTH);
    if (next == k->head) {
        return; /* full: drop (a panel never holds that many at once) */
    }
    k->pend[k->tail] = (uint8_t)code;
    k->tail = next;
}

static int pop_code(keypad_t *k)
{
    int code;
    if (k->head == k->tail) {
        return 0;
    }
    code = k->pend[k->head];
    k->head = (uint8_t)((k->head + 1u) % KEYPAD_PENDING_DEPTH);
    return code;
}

int keypad_scan(keypad_t *k, uint32_t mask, uint32_t tick_ms)
{
    uint32_t m;
    if (k == 0) {
        return 0;
    }
    m = sanitize(mask);
    if (m != k->stable) {
        if (m != k->candidate) {
            k->candidate = m;
            k->tick = tick_ms;
        } else if ((tick_ms - k->tick) >= KEYPAD_DEBOUNCE_MS) {
            uint32_t added = (uint32_t)(m & ~k->stable);
            uint8_t i;
            for (i = 0u; i < KEYPAD_CELLS; i++) {
                if (((added >> i) & 1u) != 0u) {
                    push_code(k, keypad_code((uint8_t)(i / KEYPAD_COLS),
                                             (uint8_t)(i % KEYPAD_COLS)));
                }
            }
            if (m == 0u && k->stable != 0u) {
                push_code(k, (int)KEYPAD_RELEASE_CODE);
            }
            k->stable = m;
        }
    } else {
        k->candidate = m;
    }
    return pop_code(k);
}
