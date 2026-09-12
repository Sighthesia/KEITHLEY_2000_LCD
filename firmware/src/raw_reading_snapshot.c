#include <string.h>

#include "raw_reading_snapshot.h"

void raw_reading_snapshot_init(raw_reading_snapshot_t *snapshot)
{
    if (snapshot != 0)
        memset(snapshot, 0, sizeof(*snapshot));
}

bool raw_reading_snapshot_accept(raw_reading_snapshot_t *snapshot,
                                 const char *line, uint8_t length)
{
    if (snapshot == 0 || line == 0 || length == 0u)
        return false;
    if ((uint16_t)length >= sizeof(snapshot->line))
        return false;
    if (snapshot->valid && snapshot->length == length &&
        memcmp(snapshot->line, line, length) == 0)
        return false;
    memcpy(snapshot->line, line, length);
    snapshot->line[length] = '\0';
    snapshot->length = length;
    snapshot->generation++;
    snapshot->valid = true;
    return true;
}
