#include <assert.h>
#include <string.h>

#include "reading_split.h"

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

    /* Precise recognition: prefix-only or shorter matches are rejected. */
    assert(!reading_is_special("OVERFLOWING", 11, &sp));
    assert(!reading_is_special("OVERFLOW2", 9, &sp));
    assert(!reading_is_special("--", 2, &sp));
    assert(!reading_is_special("---- ", 5, &sp));
    assert(!reading_is_special(0, 4, &sp) && sp == 0);

    return 0;
}
