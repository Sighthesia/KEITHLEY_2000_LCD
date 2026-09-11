#include <assert.h>
#include <string.h>

#include "host_snapshot.h"
#include "k2000_proto.h"
#include "reading_split.h"

/* Replay plumbing mirrors main.c's host_apply_canvas_reading(): every
 * FIELD event re-parses the merged VFD canvas line into the snapshot. */
static host_snapshot_t s_replay_snap;

static void replay_on_event(const k2000_event_t *evt)
{
    char line[48];
    uint8_t line_len;

    if (evt == 0 || evt->type != K2000_EVT_FIELD) {
        return;
    }
    line_len = k2000_vfd_line(line, (uint8_t)sizeof(line));
    if (line_len != 0u) {
        (void)host_snapshot_parse(&s_replay_snap, line, line_len);
    }
}

static const k2000_proto_cb_t s_replay_cb = {replay_on_event, 0};

/* The host rewrites the whole VFD line every frame; the canvas keeps old
 * characters positionally, so shorter frames pad with trailing spaces to
 * fully cover the previous one (bus captures show padded, fixed-width
 * lines). Trailing spaces never reach the parse: k2000_vfd_line trims. */
#define REPLAY_LINE_COLS 28u

static void replay_feed_text(const char *text)
{
    const char *p;
    uint8_t col;

    k2000_proto_feed(0x0Du);
    for (p = text, col = 0u; *p != '\0'; p++, col++) {
        k2000_proto_feed((uint8_t)*p);
    }
    for (; col < REPLAY_LINE_COLS; col++) {
        k2000_proto_feed((uint8_t)' ');
    }
    k2000_proto_feed(0x0Du); /* flush the open field */
}

int main(void)
{
    char num[32], unit[16];
    uint8_t nl, ul;

    reading_split("1.234567", 8, num, &nl, unit, &ul);
    assert(nl == 8 && memcmp(num, "1.234567", 8) == 0 && num[8] == '\0');
    assert(ul == 0 && unit[0] == '\0');

    reading_split("12.3VDC", 7, num, &nl, unit, &ul);
    assert(nl == 4 && memcmp(num, "12.3", 4) == 0 && num[4] == '\0');
    assert(ul == 3 && memcmp(unit, "VDC", 3) == 0 && unit[3] == '\0');

    /* E-notation is NOT produced by the K2000 VFD (fixed digit field); the
     * backward parser stops at the sign after the exponent digits. */
    reading_split("-0.456E+3", 9, num, &nl, unit, &ul);
    assert(nl == 2 && memcmp(num, "+3", 2) == 0);
    assert(ul == 0);

    reading_split("1.5mV", 5, num, &nl, unit, &ul);
    assert(nl == 3 && memcmp(num, "1.5", 3) == 0);
    assert(ul == 2 && memcmp(unit, "mV", 2) == 0 && unit[2] == '\0');

    /* K2000 VFD host formats (right-to-left parse). */
    reading_split(" 0.011014 VDC  ", 15, num, &nl, unit, &ul);
    assert(nl == 8 && memcmp(num, "0.011014", 8) == 0);
    assert(ul == 3 && memcmp(unit, "VDC", 3) == 0);

    reading_split("-030.4414mVDC. ", 15, num, &nl, unit, &ul);
    assert(nl == 9 && memcmp(num, "-030.4414", 9) == 0);
    assert(ul == 4 && memcmp(unit, "mVDC", 4) == 0);

    reading_split("0 0.007846 VDC", 14, num, &nl, unit, &ul);
    assert(nl == 8 && memcmp(num, "0.007846", 8) == 0);
    assert(ul == 3 && memcmp(unit, "VDC", 3) == 0);

    /* Placeholders and labels: no digit in the numeric token -> empty num. */
    reading_split(" --.----- AAC", 13, num, &nl, unit, &ul);
    assert(nl == 0);
    reading_split("REV:A17   V16   ---.----mVDC", 28, num, &nl, unit, &ul);
    assert(nl == 0);
    reading_split("NEW CODE? N     --.----- ADC", 28, num, &nl, unit, &ul);
    assert(nl == 0);

    /* Length-boundary safety: a long numeric token clips to num, both
     * outputs stay NUL-terminated. */
    {
        char longstr[64];
        char longnum[32], longunit[16];
        uint8_t ln, lu;
        memset(longstr, '1', 50);
        reading_split(longstr, 50, longnum, &ln, longunit, &lu);
        assert(ln == READING_SPLIT_MAX_NUM);
        assert(lu == 0);
        assert(longnum[READING_SPLIT_MAX_NUM] == '\0');
        assert(longunit[0] == '\0');

        /* Long trailing unit clip. */
        memset(longstr, 'V', 50);
        reading_split(longstr, 50, longnum, &ln, longunit, &lu);
        assert(ln == 0); /* no digits -> dropped */
        assert(lu == READING_SPLIT_MAX_UNIT);
        assert(longunit[READING_SPLIT_MAX_UNIT] == '\0');
    }

    /* NULL inputs are handled safely. */
    reading_split(0, 5, num, &nl, unit, &ul);
    assert(nl == 0 && ul == 0 && num[0] == '\0' && unit[0] == '\0');

    uint8_t sp;
    assert(reading_is_special("OVERFLOW", 8, &sp) && sp == 1);
    assert(reading_is_special("OVRFLW", 6, &sp) && sp == 1);
    assert(reading_is_special("OVR.FLW", 7, &sp) && sp == 1);
    assert(reading_is_special("OV.RFLW  DCV", 12, &sp) && sp == 1);
    assert(reading_is_special("OPEN", 4, &sp) && sp == 2);
    assert(reading_is_special("open", 4, &sp) && sp == 2);
    assert(reading_is_special("----", 4, &sp) && sp == 2);
    assert(!reading_is_special("1.23", 4, &sp) && sp == 0);
    assert(reading_is_special("OVRFLW C.", 9, &sp) && sp == 1);
    assert(reading_is_special("OVR.FLW MOHM.", 13, &sp) && sp == 1);
    assert(!reading_is_special("1.23VDC.", 8, &sp) && sp == 0);
    {
        char canonical[8];
        assert(reading_normalize_unit("vAC", 3, canonical, sizeof(canonical)) &&
               strcmp(canonical, "VAC") == 0);
        assert(reading_normalize_unit("vDC", 3, canonical, sizeof(canonical)) &&
               strcmp(canonical, "VDC") == 0);
        assert(reading_normalize_unit("v", 1, canonical, sizeof(canonical)) &&
               strcmp(canonical, "V") == 0);
        assert(!reading_normalize_unit("VAC", 3, canonical, sizeof(canonical)));
    }

    /* Precise recognition: prefix-only or shorter matches are rejected. */
    assert(!reading_is_special("OVERFLOWING", 11, &sp));
    assert(!reading_is_special("OVERFLOW2", 9, &sp));
    assert(!reading_is_special("--", 2, &sp));
    assert(!reading_is_special("---- ", 5, &sp));
    assert(!reading_is_special(0, 4, &sp) && sp == 0);

    /* ---- Captured-stream replay: latest host snapshot ----
     * Feeds bus-shaped frames (0x0D + status/text bytes) through the real
     * parser into the snapshot, the same way main.c does: every FIELD event
     * re-parses the merged VFD canvas line. Frame shapes follow the
     * 2026-09-08 bus captures: slot counter as the field tag byte, leading
     * space before a signed reading, trailing cursor dot, REV/NEW CODE
     * banner labels, placeholder rotations during AUTO range hunting. */
    {
        host_snapshot_init(&s_replay_snap);
        assert(!s_replay_snap.valid);
        assert(s_replay_snap.generation == 0u);
        assert(!host_snapshot_parse(0, "1.2", 3));
        assert(!host_snapshot_parse(&s_replay_snap, 0, 3));
        assert(!host_snapshot_parse(&s_replay_snap, "1.2", 0));
        assert(s_replay_snap.generation == 0u);

        k2000_proto_init(&s_replay_cb);

        /* REV banner line: a label, never a reading (the bare-integer
         * tail "V16" -> "16" must not become a value). */
        {
            static const char rev[] = "REV:A17 V16";
            replay_feed_text(rev);
            assert(!s_replay_snap.valid);
            assert(s_replay_snap.generation == 0u);
            assert(!host_snapshot_parse(&s_replay_snap, rev,
                                        (uint8_t)strlen(rev)));
        }

        /* Placeholder rotation (open leads, AUTO hunting): no digits. */
        {
            replay_feed_text(" --.----- AAC");
            assert(!s_replay_snap.valid);
            assert(s_replay_snap.generation == 0u);
        }

        /* VDC reading: slot counter byte "0" is the field tag, the merged
         * canvas keeps " 0.011014 VDC". */
        {
            replay_feed_text("0 0.011014 VDC");
            assert(s_replay_snap.valid);
            assert(s_replay_snap.generation == 1u);
            assert(s_replay_snap.special == 0u);
            assert(strcmp(s_replay_snap.value, "0.011014") == 0);
            assert(strcmp(s_replay_snap.unit, "VDC") == 0);
            assert(!s_replay_snap.trigger_dot);
        }

        /* mVDC reading with leading space and trailing cursor dot: a unit
         * change must land IMMEDIATELY (no settle gate may drop it). */
        {
            replay_feed_text(" -030.4414mVDC.");
            assert(s_replay_snap.valid);
            assert(s_replay_snap.generation == 2u);
            assert(strcmp(s_replay_snap.value, "-030.4414") == 0);
            assert(strcmp(s_replay_snap.unit, "mVDC") == 0);
        }

        /* Special reading (open lead): whole record, no unit carried. */
        {
            replay_feed_text(" OPEN");
            assert(s_replay_snap.valid);
            assert(s_replay_snap.generation == 3u);
            assert(s_replay_snap.special == 2u);
            assert(strcmp(s_replay_snap.value, "OPEN") == 0);
            assert(s_replay_snap.unit[0] == '\0');
        }

        /* Special suffix dots are trigger content; ordinary numeric dots are
         * never promoted to trigger state. */
        {
            assert(host_snapshot_parse(&s_replay_snap, "OVRFLW C.", 9u));
            assert(s_replay_snap.special == 1u && s_replay_snap.trigger_dot);
            assert(host_snapshot_parse(&s_replay_snap, "OVR.FLW MOHM.", 13u));
            assert(s_replay_snap.special == 1u && s_replay_snap.trigger_dot);
            assert(host_snapshot_parse(&s_replay_snap, "1.23VDC.", 8u));
            assert(!s_replay_snap.trigger_dot);
        }
        host_snapshot_init(&s_replay_snap);
        k2000_proto_init(&s_replay_cb);

        /* Labels and placeholders are dropped whole after the special replay. */
        {
            k2000_proto_feed(0x0D);
            k2000_proto_feed(0x0B);
            k2000_proto_feed(0x01);
            k2000_proto_feed('N');
            k2000_proto_feed(0x0B);
            k2000_proto_feed(0x00);
            replay_feed_text("NEW CODE? N  --.----- ADC");
            assert(s_replay_snap.generation == 0u);
            assert(!s_replay_snap.valid);
        }

        /* Continuous burst (AUTO range hunting, ~27 records/s): every
         * complete record overwrites the snapshot, value and unit always
         * land as one pair from the same message, and the newest record
         * wins without any queueing or settle delay. */
        {
            static const char *burst[3] = {
                "0 0.011014 VDC",
                " -030.4414mVDC.",
                "0 0.011014 VDC",
            };
            uint8_t gi;

            for (gi = 0u; gi < 3u; gi++) {
                replay_feed_text(burst[gi]);
            }
            /* Newest record is the trailing VDC frame. */
            assert(s_replay_snap.generation == 3u);
            assert(s_replay_snap.special == 0u);
            assert(strcmp(s_replay_snap.value, "0.011014") == 0);
            assert(strcmp(s_replay_snap.unit, "VDC") == 0);

            /* Same-message pairing invariant replayed record by record:
             * the mVDC frame must have been applied whole (a settle gate
             * would have dropped it inside its window). */
            k2000_proto_init(&s_replay_cb);
            host_snapshot_init(&s_replay_snap);
            for (gi = 0u; gi < 3u; gi++) {
                replay_feed_text(burst[gi]);
                if (gi == 1u) {
                    assert(strcmp(s_replay_snap.value, "-030.4414") == 0);
                    assert(strcmp(s_replay_snap.unit, "mVDC") == 0);
                } else {
                    assert(strcmp(s_replay_snap.value, "0.011014") == 0);
                    assert(strcmp(s_replay_snap.unit, "VDC") == 0);
                }
                assert(s_replay_snap.generation == (uint32_t)(gi + 1u));
            }

            /* Strict units reject canvas contamination without changing the
             * last complete value/unit pair. */
            {
                static const char *const invalid[] = {
                    "1.0VVAC", "1.0VACVAC", "1.0mmVDC", "1.0XYZ"
                };
                uint8_t ii;
                uint32_t generation = s_replay_snap.generation;

                for (ii = 0u; ii < (uint8_t)(sizeof(invalid) / sizeof(invalid[0])); ii++) {
                    assert(!host_snapshot_parse(&s_replay_snap, invalid[ii],
                                                (uint8_t)strlen(invalid[ii])));
                    assert(s_replay_snap.generation == generation);
                    assert(strcmp(s_replay_snap.value, "0.011014") == 0);
                    assert(strcmp(s_replay_snap.unit, "VDC") == 0);
                }
            }
        }
    }

    return 0;
}
