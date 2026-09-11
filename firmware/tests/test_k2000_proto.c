#include <assert.h>
#include <string.h>

#include "k2000_proto.h"

static int s_status_evts = 0;
static int s_pos_val = -1;
static int s_last_status_tag = 0;
static int s_last_status_value = 0;
static int s_field_evts = 0;
static char s_field_value[K2000_PROTO_MAX_FIELD];
static int s_sym_evts = 0;
static int s_sym_ctrl = 0;
static int s_seg_evts = 0;
static int s_seg_ctrl = 0;
static int s_flush_evts = 0;

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
    case K2000_EVT_FIELD:
        s_field_evts++;
        memcpy(s_field_value, evt->field.value, evt->field.value_len);
        s_field_value[evt->field.value_len] = '\0';
        break;
    case K2000_EVT_SYMBOL:
        s_sym_evts++;
        s_sym_ctrl = evt->ctrl;
        break;
    case K2000_EVT_SEGMENT:
        s_seg_evts++;
        s_seg_ctrl = evt->ctrl;
        break;
    case K2000_EVT_FLUSH:
        s_flush_evts++;
        break;
    default:
        break;
    }
}

static const k2000_proto_cb_t s_cb = {on_event, 0};

int main(void)
{
    /* Frame start (0x0D) with an open unterminated field: the complete
     * value must be emitted, not discarded. Hosts that stream readings
     * without a closing tag rely on the next frame boundary to close. */
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x01u);
    k2000_proto_feed((uint8_t)'1');
    k2000_proto_feed((uint8_t)'2');
    k2000_proto_feed(0x0Du); /* next frame: flushes "12", opens new frame */
    assert(s_field_evts == 1);
    assert(strcmp(s_field_value, "12") == 0);
    /* New frame's field starts clean: tag 0x01 then value "3". */
    k2000_proto_feed(0x01u);
    k2000_proto_feed((uint8_t)'3');
    k2000_proto_feed(0x0Du);
    assert(s_field_evts == 2);
    assert(strcmp(s_field_value, "3") == 0);

    /* Incomplete POS argument at frame start stays discarded (corrupt
     * fragment, not an emittable field). */
    s_field_evts = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x04u); /* POS tag, no argument */
    k2000_proto_feed(0x0Du);
    assert(s_field_evts == 0);
    assert(s_pos_val == -1);

    /* Status TAG 0x08 (HOLD/TRIG/FAST/MED/SLOW) with value 0x80 (HOLD on). */
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

    /* POS must update the persistent canvas cursor after the event payload is
     * cleared: a subsequent field character belongs at the requested column. */
    {
        char line[24];
        k2000_proto_init(&s_cb);
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        k2000_proto_feed('A');
        k2000_proto_feed('B');
        k2000_proto_feed('C');
        k2000_proto_feed(0x04u);
        k2000_proto_feed('0');
        k2000_proto_feed('0');
        k2000_proto_feed('3');
        k2000_proto_feed('A'); /* terminates POS */
        k2000_proto_feed(0x01u); /* field tag */
        k2000_proto_feed('Z');
        assert(k2000_vfd_line(line, (uint8_t)sizeof(line)) == 4u);
        assert(line[0] == 'A');
        assert(line[1] == 'B');
        assert(line[2] == 'C');
        assert(line[3] == 'Z');
    }

    /* Reading field still works: 0x0D then ASCII "1.23". */
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed('1');
    k2000_proto_feed('.');
    k2000_proto_feed('2');
    k2000_proto_feed('3');
    /* no crash, field path exercised */

    /* Inline u symbol stays inside the reading value (ODS: 0x10=u content):
     * 0x0D 0x01 "1.234" 0x10 "V" -> FIELD "1.234" + UTF-8 u + "V". */
    s_field_evts = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x01u);   /* reading field tag */
    k2000_proto_feed('1');
    k2000_proto_feed('.');
    k2000_proto_feed('2');
    k2000_proto_feed('3');
    k2000_proto_feed('4');
    k2000_proto_feed(0x10u);   /* u symbol */
    k2000_proto_feed('V');
    k2000_proto_feed(0x06u);   /* status tag ends the stream */
    assert(s_field_evts == 1);
    assert(memcmp(s_field_value, "1.234\xC2\xB5"
                                 "V", 9) == 0);
    assert(s_field_value[9] == '\0');

    /* Degree symbol inline: "23.5" 0x13 "C" -> "23.5" + degree + "C". */
    s_field_evts = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x01u);
    k2000_proto_feed('2');
    k2000_proto_feed('3');
    k2000_proto_feed('.');
    k2000_proto_feed('5');
    k2000_proto_feed(0x13u);   /* degree symbol */
    k2000_proto_feed('C');
    k2000_proto_feed(0x09u);   /* status tag ends the stream */
    assert(s_field_evts == 1);
    assert(memcmp(s_field_value, "23.5\xC2\xB0"
                                 "C", 7) == 0);

    /* Segment tags (0x18/0x1A/0x7F) close the field and emit segment events,
     * never value chars. */
    s_field_evts = 0;
    s_seg_evts = 0;
    s_seg_ctrl = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x01u);
    k2000_proto_feed('9');
    k2000_proto_feed(0x7Fu);   /* complete digit on */
    k2000_proto_feed(0x09u);   /* status tag ends the stream */
    assert(s_field_evts == 1);
    assert(strcmp(s_field_value, "9") == 0);
    assert(s_seg_evts == 1);
    assert(s_seg_ctrl == 0x7F);

    /* Flush (0x02) emits a FLUSH event without opening a field. */
    s_flush_evts = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x02u);
    assert(s_flush_evts == 1);

    /* A lone symbol at message start (no field tag yet) is its own event. */
    s_sym_evts = 0;
    s_sym_ctrl = 0;
    k2000_proto_init(&s_cb);
    k2000_proto_feed(0x0Du);
    k2000_proto_feed(0x10u);
    assert(s_sym_evts == 1);
    assert(s_sym_ctrl == 0x10);

    /* VFD canvas readback keeps the FINAL character: a reading line whose
     * wire form ends at the unit letter must read back whole (the old
     * gap-look-ahead bound dropped it, showing "VD" for "VDC"). The first
     * text byte after 0x0D is the field tag, so feed 0x01 then the line. */
    {
        char line[24];
        uint8_t ll;
        static const char full[] = "1.23VDC";
        uint8_t i;

        k2000_proto_init(&s_cb);
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        for (i = 0u; full[i] != '\0'; i++)
            k2000_proto_feed((uint8_t)full[i]);
        ll = k2000_vfd_line(line, (uint8_t)sizeof(line));
        assert(ll == 7u);
        assert(strcmp(line, "1.23VDC") == 0);

        /* Trailing cursor dot (bus captures: "-030.4414mVDC.") is part of
         * the canvas line; reading_split strips it at parse time. */
        static const char dotted[] = "-030.4414mVDC.";
        k2000_proto_init(&s_cb);
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        for (i = 0u; dotted[i] != '\0'; i++)
            k2000_proto_feed((uint8_t)dotted[i]);
        ll = k2000_vfd_line(line, (uint8_t)sizeof(line));
        assert(ll == 14u);
        assert(strcmp(line, "-030.4414mVDC.") == 0);

        /* Segment gap: the line ends before two consecutive spaces. */
        static const char gapped[] = "1.23VDC  TRIG A";
        k2000_proto_init(&s_cb);
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        for (i = 0u; gapped[i] != '\0'; i++)
            k2000_proto_feed((uint8_t)gapped[i]);
        ll = k2000_vfd_line(line, (uint8_t)sizeof(line));
        assert(ll == 7u);
        assert(strcmp(line, "1.23VDC") == 0);
    }

    return 0;
}
