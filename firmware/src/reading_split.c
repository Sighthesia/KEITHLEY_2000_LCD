#include <string.h>

#include "reading_split.h"

static bool is_num_char(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
           c == 'E' || c == 'e';
}

void reading_split(const char *ascii, uint8_t len, char *num, uint8_t *num_len,
                   char *unit, uint8_t *unit_len)
{
    uint8_t i = 0u;
    uint8_t n = 0u;
    uint8_t u = 0u;

    if (num_len != 0) {
        *num_len = 0;
    }
    if (unit_len != 0) {
        *unit_len = 0;
    }
    if (num != 0) {
        num[0] = '\0';
    }
    if (unit != 0) {
        unit[0] = '\0';
    }
    if (ascii == 0 || num == 0 || unit == 0 || num_len == 0 || unit_len == 0) {
        return;
    }

    while (i < len && n < READING_SPLIT_MAX_NUM && is_num_char(ascii[i])) {
        num[n++] = ascii[i++];
    }
    num[n] = '\0';

    while (i < len && u < READING_SPLIT_MAX_UNIT) {
        unit[u++] = ascii[i++];
    }
    unit[u] = '\0';

    *num_len = n;
    *unit_len = u;
}

bool reading_is_special(const char *num, uint8_t len, uint8_t *special)
{
    if (special != 0) {
        *special = 0;
    }
    if (num == 0) {
        return false;
    }
    /* K2000 VFD spellings (V16 ROM): overflow variants and open-lead. The
     * display may append the function after a gap ("OV.RFLW  DCV"), so the
     * overflow check is a prefix match. */
    if (len >= 6u && memcmp(num, "OVRFLW", 6u) == 0) {
        if (special != 0) {
            *special = 1u;
        }
        return true;
    }
    if (len >= 6u && (memcmp(num, "OVR.FL", 6u) == 0 ||
                      memcmp(num, "OV.RFL", 6u) == 0)) {
        if (special != 0) {
            *special = 1u;
        }
        return true;
    }
    if (len >= 2u && (memcmp(num, "OP", 2u) == 0 ||
                      memcmp(num, "op", 2u) == 0)) {
        if (special != 0) {
            *special = 2u;
        }
        return true;
    }
    if (len == 8u && memcmp(num, "OVERFLOW", 8u) == 0) {
        if (special != 0) {
            *special = 1u;
        }
        return true;
    }
    if (len == 4u && memcmp(num, "----", 4u) == 0) {
        if (special != 0) {
            *special = 2u;
        }
        return true;
    }
    return false;
}
