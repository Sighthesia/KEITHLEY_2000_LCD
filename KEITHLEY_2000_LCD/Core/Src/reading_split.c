#include <string.h>

#include "reading_split.h"

static bool is_num_char(char c)
{
    return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
           c == 'E' || c == 'e';
}

static bool is_unit_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           ((uint8_t)c >= 0x80u);
}

static bool has_digit(const char *s, uint8_t n)
{
    uint8_t i;
    for (i = 0u; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            return true;
        }
    }
    return false;
}

/* The K2000 VFD stream is free-form text: readings arrive with leading
 * spaces and slot counters ("0 0.011014 VDC", " -030.4414mVDC. "), label
 * fields mix in ("REV:A17 V16", "NEW CODE?"). Parsing therefore runs
 * RIGHT-TO-LEFT: strip trailing cursor dots/spaces, take the trailing
 * letter run as the unit, the preceding token as the number. Tokens
 * without a digit (placeholder "---.-----", labels) yield an empty num so
 * the caller's reading filter drops them. */
void reading_split(const char *ascii, uint8_t len, char *num, uint8_t *num_len,
                   char *unit, uint8_t *unit_len)
{
    uint8_t end;
    uint8_t u = 0u;
    uint8_t t_end;
    uint8_t t_start;
    uint8_t n = 0u;

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

    end = len;
    while (end > 0u && (ascii[end - 1u] == ' ' || ascii[end - 1u] == '.')) {
        end--;
    }
    if (end == 0u) {
        return;
    }

    /* Trailing unit run (letters + UTF-8). */
    t_end = end;
    while (t_end > 0u && is_unit_char(ascii[t_end - 1u]) && u < READING_SPLIT_MAX_UNIT) {
        t_end--;
        u++;
    }
    if (u > 0u) {
        uint8_t k;
        for (k = 0u; k < u; k++) {
            unit[k] = ascii[t_end + k];
        }
        unit[u] = '\0';
        *unit_len = u;
        end = t_end;
    }

    /* Skip spaces before the number token. */
    while (end > 0u && ascii[end - 1u] == ' ') {
        end--;
    }

    /* The number token: run of numeric chars ending at 'end'. */
    t_end = end;
    t_start = t_end;
    while (t_start > 0u && is_num_char(ascii[t_start - 1u])) {
        t_start--;
    }
    if (t_start < t_end && has_digit(ascii + t_start,
                                     (uint8_t)(t_end - t_start))) {
        n = t_end - t_start;
        if (n > READING_SPLIT_MAX_NUM) {
            n = READING_SPLIT_MAX_NUM;
        }
        memcpy(num, ascii + t_end - n, n);
        num[n] = '\0';
        *num_len = n;
    }
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
