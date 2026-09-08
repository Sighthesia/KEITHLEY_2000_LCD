#include <assert.h>
#include <string.h>

#include "status_bar.h"

int main(void)
{
    static const char *const labels[] = {
        "REMOTE", "TALK", "LSTN", "SRQ", "HOLD", "TRIG", "REL", "FILT",
        "AUTO", "ERR", "BUFFER", "MATH", "CONT"
    };
    static const uint8_t tags[] = {
        0x06u,0x06u,0x06u,0x06u,0x08u,0x08u,0x09u,0x09u,0x09u,0x09u,0x09u,0x07u,0x00u
    };
    static const uint8_t bits[] = {
        0x08u,0x04u,0x02u,0x01u,0x10u,0x08u,0x40u,0x20u,0x10u,0x08u,0x02u,0x20u,0x00u
    };
    status_bar_t status;
    const status_bar_indicator_t *indicator;
    uint8_t i;
    status_bar_init(&status);
    for (i = 0u; i < STATUS_BAR_CORE_COUNT; i++) {
        assert(status_bar_core_get(i, &indicator));
        assert(strcmp(indicator->label, labels[i]) == 0);
        assert(indicator->tag == tags[i] && indicator->bit == bits[i]);
        if (tags[i] != 0u) {
            status_bar_set(&status, tags[i], bits[i]);
            assert(status_bar_core_active(&status, i));
            status_bar_set(&status, tags[i], 0u);
        }
    }
    assert(!status_bar_core_get(STATUS_BAR_CORE_COUNT, &indicator));
    assert(!status_bar_core_get(0u, 0));
    return 0;
}
