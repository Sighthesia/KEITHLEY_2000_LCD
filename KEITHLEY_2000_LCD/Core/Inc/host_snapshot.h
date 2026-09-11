#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_model.h"

/* Latest-host-reading snapshot: the parsing layer produces ONE complete
 * host record at a time (value + unit parsed and stored together from the
 * same merged VFD canvas line, never spliced across messages). Each accepted
 * record overwrites the previous one -- there is no queue. `generation`
 * counts accepted records so a renderer can take a boundary snapshot when
 * it starts a frame and detect newer data afterwards. */

typedef struct {
    char value[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    /* 0 = normal numeric reading, 1 = OVERFLOW, 2 = no-reading (OPEN/----).
     * Special records carry the raw line in `value` and no unit. */
    uint8_t special;
    /* Monotonic count of accepted records; 0 before the first one. */
    uint32_t generation;
    /* True once at least one record has been accepted. */
    bool valid;
} host_snapshot_t;

void host_snapshot_init(host_snapshot_t *snap);

/* Parse one merged canvas line as a complete host record. Numeric readings
 * (value + trailing unit) and special readings (OVERFLOW / OPEN / ----)
 * overwrite the snapshot atomically and return true. Non-readings -- bare
 * labels (REV banner, NEW CODE?), placeholder rotations (--.-----) and
 * other free text -- leave the snapshot untouched and return false. A
 * record identical to the current one (VFD redraws, empty field events
 * re-reading the unchanged canvas) is a no-op: content already wins, the
 * generation does not move. */
bool host_snapshot_parse(host_snapshot_t *snap, const char *line,
                         uint8_t len);
