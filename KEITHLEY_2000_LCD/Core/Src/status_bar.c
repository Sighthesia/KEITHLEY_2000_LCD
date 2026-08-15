#include "status_bar.h"

static const uint8_t s_status_tags[STATUS_BAR_NUM_TAGS] = {
    0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Eu,
};

const status_bar_indicator_t status_bar_core_table[STATUS_BAR_CORE_COUNT] = {
    { "REM",    0x06u, 0x08u },
    { "TALK",   0x06u, 0x04u },
    { "LSTN",   0x06u, 0x02u },
    { "SRQ",    0x06u, 0x01u },
    { "HOLD",   0x08u, 0x10u },
    { "TRIG",   0x08u, 0x08u },
    { "REL",    0x09u, 0x40u },
    { "FILT",   0x09u, 0x20u },
    { "AUTO",   0x09u, 0x10u },
    { "ERR",    0x09u, 0x08u },
    { "BUFFER", 0x09u, 0x02u },
    { "MATH",   0x07u, 0x20u },
    { "CONT",   0x00u, 0x00u },
};

void status_bar_init(status_bar_t *sb)
{
    uint8_t i;
    if (sb == 0) {
        return;
    }
    for (i = 0; i < STATUS_BAR_NUM_TAGS; i++) {
        sb->tags[i].tag = s_status_tags[i];
        sb->tags[i].value = 0;
    }
}

static uint8_t find_tag(const status_bar_t *sb, uint8_t tag)
{
    uint8_t i;
    if (sb == 0) {
        return STATUS_BAR_NUM_TAGS;
    }
    for (i = 0; i < STATUS_BAR_NUM_TAGS; i++) {
        if (sb->tags[i].tag == tag) {
            return i;
        }
    }
    return STATUS_BAR_NUM_TAGS;
}

void status_bar_set(status_bar_t *sb, uint8_t tag, uint8_t value)
{
    uint8_t i = find_tag(sb, tag);
    if (i < STATUS_BAR_NUM_TAGS) {
        sb->tags[i].value = value;
    }
}

bool status_bar_active(const status_bar_t *sb, uint8_t tag, uint8_t bit)
{
    uint8_t i = find_tag(sb, tag);
    if (i >= STATUS_BAR_NUM_TAGS) {
        return false;
    }
    return (sb->tags[i].value & bit) != 0u;
}

bool status_bar_core_get(uint8_t index, const status_bar_indicator_t **out)
{
    if (out == 0) {
        return false;
    }
    *out = 0;
    if (index >= STATUS_BAR_CORE_COUNT) {
        return false;
    }
    *out = &status_bar_core_table[index];
    return true;
}

bool status_bar_core_active(const status_bar_t *sb, uint8_t index)
{
    const status_bar_indicator_t *ind;
    if (!status_bar_core_get(index, &ind)) {
        return false;
    }
    return status_bar_active(sb, ind->tag, ind->bit);
}
