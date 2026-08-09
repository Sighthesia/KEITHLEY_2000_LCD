#include <assert.h>
#include <string.h>

#include "k2000_proto.h"

static int s_status_evts = 0;
static int s_pos_val = -1;
static int s_last_status_tag = 0;
static int s_last_status_value = 0;

static void on_event(const k2000_event_t *evt)
{
    switch (evt->type) {
    case K2000_EVT_STATUS:
        s_status_evts++;
        s_last_status_tag = evt->status_tag;
        s_last_status_value = evt->status_value;
        break;
    case K2000_EVT_CURSOR:
        s_pos_val = evt->pos;
        break;
    default:
        break;
    }
}

static const k2000_proto_cb_t s_cb = {on_event, 0};

int main(void)
{
    /* Status TAG 0x08 (HOLD/TRIG/FAST/MED/SLOW) with value 0x80 (HOLD on). */
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x08u);
    k2000_proto_feed(0x80u);
    assert(s_status_evts == 1);
    assert(s_last_status_tag == 0x08);
    assert(s_last_status_value == 0x80);

    /* Status TAG arriving mid-reading (0x0D value 0x06 0x05):
     * 0x06 with value 0x05 = REM + SRQ. */
    s_status_evts = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x06u);
    k2000_proto_feed(0x05u);
    assert(s_status_evts == 1);
    assert(s_last_status_tag == 0x06);
    assert(s_last_status_value == 0x05);

    /* POS: 0x0D 0x04 then ASCII digits "003"; a non-digit text byte
     * terminates the position and emits the CURSOR event. */
    k2000_proto_init(&s_cb);
    s_pos_val = -1;
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x04u);
    k2000_proto_feed('0');
    k2000_proto_feed('0');
    k2000_proto_feed('3');
    k2000_proto_feed('A');
    assert(s_pos_val == 3);

    /* Reading field still works: 0x0D then ASCII "1.23". */
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed('1');
    k2000_proto_feed('.');
    k2000_proto_feed('2');
    k2000_proto_feed('3');
    /* no crash, field path exercised */

    return 0;
}
