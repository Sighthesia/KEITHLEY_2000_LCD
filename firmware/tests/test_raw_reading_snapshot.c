#include <assert.h>
#include <string.h>

#include "raw_reading_snapshot.h"

static void accept(raw_reading_snapshot_t *snapshot, const char *line)
{
    assert(raw_reading_snapshot_accept(snapshot, line, (uint8_t)strlen(line)));
}

int main(void)
{
    raw_reading_snapshot_t snapshot;
    uint32_t generation;

    raw_reading_snapshot_init(&snapshot);
    accept(&snapshot, "REV");
    assert(strcmp(snapshot.line, "REV") == 0);
    assert(snapshot.generation == 1u);

    accept(&snapshot, "0 0.011933 VDC");
    assert(strcmp(snapshot.line, "0 0.011933 VDC") == 0);
    accept(&snapshot, "008.7481mVDC.");
    assert(strcmp(snapshot.line, "008.7481mVDC.") == 0);

    accept(&snapshot, "----.---");
    assert(strcmp(snapshot.line, "----.---") == 0);
    generation = snapshot.generation;
    accept(&snapshot, "OVR.FLW");
    assert(strcmp(snapshot.line, "OVR.FLW") == 0);
    assert(snapshot.generation == generation + 1u);
    accept(&snapshot, "OVRFLW");
    assert(strcmp(snapshot.line, "OVRFLW") == 0);
    accept(&snapshot, "OVRFLW C.");
    assert(strcmp(snapshot.line, "OVRFLW C.") == 0);
    accept(&snapshot, "OVERFLOW");
    assert(strcmp(snapshot.line, "OVERFLOW") == 0);

    accept(&snapshot, "ABCdef-+ .VDC");
    assert(strcmp(snapshot.line, "ABCdef-+ .VDC") == 0);
    generation = snapshot.generation;
    accept(&snapshot, "1.2 VDC");
    accept(&snapshot, "2.3 VDC");
    accept(&snapshot, "3.4 VDC");
    assert(strcmp(snapshot.line, "3.4 VDC") == 0);
    assert(snapshot.generation == generation + 3u);
    generation = snapshot.generation;
    assert(!raw_reading_snapshot_accept(&snapshot, "3.4 VDC", 7u));
    assert(snapshot.generation == generation);
    return 0;
}
