#include <string.h>

#include "reading_split.h"

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
    while (t_end > 0u && u < READING_SPLIT_MAX_UNIT &&
           (((ascii[t_end - 1u] >= 'A') && (ascii[t_end - 1u] <= 'Z')) ||
            ((ascii[t_end - 1u] >= 'a') && (ascii[t_end - 1u] <= 'z')) ||
            ((uint8_t)ascii[t_end - 1u] >= 0x80u))) {
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

    /* The number token: scan BACKWARD from the end of the text run --
     * trailing digits, optional single '.', leading digits, then at most
     * one sign. Anything before that (the host packs single-char VFD
     * segments tightly: "0-043.7338mVDC") is prefix junk and is dropped. */
    t_end = end;
    {
        uint8_t p = t_end;
        uint8_t digits = 0u;
        while (p > 0u && ascii[p - 1u] >= '0' && ascii[p - 1u] <= '9') {
            p--;
            digits++;
        }
        if (p > 0u && ascii[p - 1u] == '.' && digits > 0u) {
            p--;
            digits = 0u;
            while (p > 0u && ascii[p - 1u] >= '0' && ascii[p - 1u] <= '9') {
                p--;
                digits++;
            }
        }
        if (digits == 0u) {
            return;
        }
        if (p > 0u && (ascii[p - 1u] == '+' || ascii[p - 1u] == '-')) {
            p--;
        }
        t_start = p;
    }
    {
        uint8_t raw = (uint8_t)(t_end - t_start);
        n = raw;
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
    uint8_t start = 0u;
    uint8_t end = len;
    uint8_t special_len = 0u;
    uint8_t span;
    const char *special_text = 0;

    if (special != 0) {
        *special = 0;
    }
    if (num == 0) {
        return false;
    }
    while (start < end && num[start] == ' ') start++;
    while (end > start && num[end - 1u] == '.') end--;
    span = (uint8_t)(end - start);
    if (span >= 6u && memcmp(num + start, "OVRFLW", 6u) == 0) {
        special_text = "OVRFLW";
        special_len = 6u;
    } else if (span >= 7u &&
               memcmp(num + start, "OVR.FLW", 7u) == 0) {
        special_text = "OVR.FLW";
        special_len = 7u;
    } else if (span >= 7u &&
               memcmp(num + start, "OV.RFLW", 7u) == 0) {
        special_text = "OV.RFLW";
        special_len = 7u;
    }
    if (special_text != 0 && (span == special_len ||
                              num[start + special_len] == ' ')) {
        if (special != 0) {
            *special = 1u;
        }
        return true;
    }
    if (span >= 2u && (memcmp(num + start, "OP", 2u) == 0 ||
                              memcmp(num + start, "op", 2u) == 0)) {
        if (special != 0) *special = 2u;
        return true;
    }
    if (span == 4u && (memcmp(num + start, "OPEN", 4u) == 0 ||
                              memcmp(num + start, "open", 4u) == 0)) {
        if (special != 0) {
            *special = 2u;
        }
        return true;
    }
    if (span == 8u && memcmp(num + start, "OVERFLOW", 8u) == 0) {
        if (special != 0) {
            *special = 1u;
        }
        return true;
    }
    if (span == 4u && memcmp(num + start, "----", 4u) == 0) {
        if (special != 0) {
            *special = 2u;
        }
        return true;
    }
    return false;
}

bool reading_normalize_unit(const char *unit, uint8_t len, char *out,
                            uint8_t out_size)
{
    const char *canonical = 0;
    uint8_t n;

    if (out == 0 || out_size == 0u) return false;
    out[0] = '\0';
    if (unit == 0) return false;
    if (len == 1u && unit[0] == 'v') canonical = "V";
    else if (len == 3u && memcmp(unit, "vAC", 3u) == 0) canonical = "VAC";
    else if (len == 3u && memcmp(unit, "vDC", 3u) == 0) canonical = "VDC";
    if (canonical == 0) return false;
    n = (uint8_t)strlen(canonical);
    if (n >= out_size) return false;
    memcpy(out, canonical, n + 1u);
    return true;
}
