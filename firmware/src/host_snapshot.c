#include <string.h>

#include "host_snapshot.h"
#include "reading_split.h"

void host_snapshot_init(host_snapshot_t *snap)
{
    if (snap == 0) {
        return;
    }
    memset(snap, 0, sizeof(*snap));
}

/* The K2000 streams multiple display fields (reading, function label,
 * right-column info, lamps). Only numeric readings may enter the reading
 * snapshot: a label like "2W Ohm" would overwrite the value, mangle the
 * unit inference and trip the digit-charset path. Valid readings have a
 * digit-bearing value; a unit must be one of the complete protocol spellings
 * (V/A/Ohm/Hz/s/C/dB with supported prefixes). A unit-less record is accepted only with
 * a decimal point in the value: K2000 numeric fields always carry one,
 * while unit-less bare integers are label fragments (the "16" tail of the
 * REV banner or a lone slot counter). */
static bool valid_unit(const char *unit)
{
    static const char *const units[] = {
        "V", "VDC", "VAC", "mV", "mVDC", "mVAC", "uV", "uVDC", "uVAC",
        "\xC2\xB5V", "\xC2\xB5VDC", "\xC2\xB5VAC",
        "A", "ADC", "AAC", "mA", "mADC", "mAAC", "uA", "uADC", "uAAC",
        "\xC2\xB5" "A", "\xC2\xB5" "ADC", "\xC2\xB5" "AAC",
        "MVDC", "MVAC", "MADC", "MAAC",
        "Hz", "kHz", "MHz", "s", "ms", "C", "DEGC", "\xC2\xB0" "C", "dB",
        "OHM", "kOHM", "MOHM", "\xCE\xA9", "k\xCE\xA9", "M\xCE\xA9"
    };
    uint8_t i;

    if (unit == 0 || unit[0] == '\0') return false;
    for (i = 0u; i < (uint8_t)(sizeof(units) / sizeof(units[0])); i++) {
        if (strcmp(unit, units[i]) == 0) return true;
    }
    return false;
}

static bool record_is_reading(const char *num, uint8_t num_len,
                              const char *unit, uint8_t unit_len)
{
    uint8_t i;

    if (num == 0 || num_len == 0u) {
        return false;
    }
    if (unit == 0 || unit_len == 0u) {
        bool has_decimal_point = false;

        for (i = 0u; i < num_len; i++) {
            if (num[i] == '.') {
                has_decimal_point = true;
                break;
            }
        }
        return has_decimal_point;
    }
    (void)unit_len;
    return valid_unit(unit);
}

static void store_record(host_snapshot_t *snap, const char *value,
                         uint8_t value_len, const char *unit,
                         uint8_t unit_len, uint8_t special)
{
    uint8_t n;

    n = value_len;
    if (n >= sizeof(snap->value)) {
        n = (uint8_t)(sizeof(snap->value) - 1u);
    }
    if (n != 0u) {
        memcpy(snap->value, value, n);
    }
    snap->value[n] = '\0';

    n = unit_len;
    if (n >= sizeof(snap->unit)) {
        n = (uint8_t)(sizeof(snap->unit) - 1u);
    }
    if (n != 0u) {
        memcpy(snap->unit, unit, n);
    }
    snap->unit[n] = '\0';

    snap->special = special;
    snap->generation++;
    snap->valid = true;
}

static bool record_matches(const host_snapshot_t *snap, const char *value,
                           const char *unit, uint8_t special)
{
    return snap->valid && snap->special == special &&
           strcmp(snap->value, value) == 0 &&
           strcmp(snap->unit, unit) == 0;
}

bool host_snapshot_parse(host_snapshot_t *snap, const char *line,
                         uint8_t len)
{
    char num[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    uint8_t num_len;
    uint8_t unit_len;
    uint8_t special;

    if (snap == 0 || line == 0 || len == 0u) {
        return false;
    }
    if (reading_is_special(line, len, &special)) {
        /* Special states (OPEN / OVRFLW / ----) are complete records
         * without a unit: overwrite the snapshot whole. Dedup compares
         * against the stored copy bounded by len (the caller's buffer is
         * not assumed NUL-terminated). */
        if (snap->valid && snap->special == special &&
            (uint8_t)strlen(snap->value) == len &&
            memcmp(snap->value, line, len) == 0) {
            return false;
        }
        store_record(snap, line, len, "", 0u, special);
        return true;
    }
    reading_split(line, len, num, &num_len, unit, &unit_len);
    if (!record_is_reading(num, num_len, unit, unit_len)) {
        return false;
    }
    num[num_len] = '\0';
    unit[unit_len] = '\0';
    if (record_matches(snap, num, unit, 0u)) {
        return false;
    }
    /* One atomic record: value and unit always come from this line. */
    store_record(snap, num, num_len, unit, unit_len, 0u);
    return true;
}
