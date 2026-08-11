#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Status bar model: one value per protocol status TAG. The six protocol
 * status tags are 0x06/0x07/0x08/0x09/0x0A/0x0E (see k2000_proto.h). */
#define STATUS_BAR_NUM_TAGS 6u
#define STATUS_BAR_CORE_COUNT 6u

typedef struct {
    uint8_t tag;
    uint8_t value;
} status_bar_tag_t;

typedef struct {
    status_bar_tag_t tags[STATUS_BAR_NUM_TAGS];
} status_bar_t;

typedef struct {
    const char *label;
    uint8_t tag;
    uint8_t bit;
} status_bar_indicator_t;

/* Milestone-1 core indicator subset (HOLD/REM/REL/TRIG/AUTO/ERR). Bit
 * mapping follows the ODS RX table (each TAG row, bit7 = first indicator):
 *   0x08 0x80 HOLD, 0x08 0x40 TRIG, 0x06 0x80 REM,
 *   0x09 0x80 REL,  0x09 0x20 AUTO, 0x09 0x10 ERR.
 * Recorded discrepancy: the implementation-plan draft
 * (docs/superpowers/plans/2026-08-09-dmm-ui.md) once listed AUTO as 0x09
 * bit6 (0x40) and ERR as 0x09 bit5 (0x20); the ODS RX table maps 0x09 0x20
 * to AUTO and 0x09 0x10 to ERR, and this module follows the ODS mapping. */
extern const status_bar_indicator_t
    status_bar_core_table[STATUS_BAR_CORE_COUNT];

void status_bar_init(status_bar_t *sb);
void status_bar_set(status_bar_t *sb, uint8_t tag, uint8_t value);
bool status_bar_active(const status_bar_t *sb, uint8_t tag, uint8_t bit);
bool status_bar_core_get(uint8_t index, const status_bar_indicator_t **out);
bool status_bar_core_active(const status_bar_t *sb, uint8_t index);
