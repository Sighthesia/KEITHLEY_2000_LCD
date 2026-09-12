#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RAW_READING_SNAPSHOT_MAX 193u

typedef struct {
    char line[RAW_READING_SNAPSHOT_MAX];
    uint8_t length;
    uint32_t generation;
    bool valid;
} raw_reading_snapshot_t;

void raw_reading_snapshot_init(raw_reading_snapshot_t *snapshot);

/* Replace the complete display line. The parser deliberately does not infer
 * value, unit, function, or trigger state from the line. */
bool raw_reading_snapshot_accept(raw_reading_snapshot_t *snapshot,
                                 const char *line, uint8_t length);
