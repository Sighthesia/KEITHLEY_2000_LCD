#pragma once

#include <stdbool.h>
#include <stdint.h>

#define K2000_PROTO_MAX_FIELD 32u

/* Status TAGs encode indicator bitmaps (one byte, bit7 = first indicator of
 * the group). 0x06=REM/TALK/LSTN/SRQ, 0x07=SHIFT/TIMER/MATH/REAR/...,
 * 0x08=HOLD/TRIG/FAST/MED/SLOW, 0x09=REL/FILT/AUTO/ERR/...,
 * 0x0A=STEP/SCAN/CH1/CH2, 0x0E=CH3..CH10. */
#define K2000_TAG_STATUS_REM 0x06u
#define K2000_TAG_STATUS_SHIFT 0x07u
#define K2000_TAG_STATUS_HOLD 0x08u
#define K2000_TAG_STATUS_REL 0x09u
#define K2000_TAG_STATUS_STEP 0x0Au
#define K2000_TAG_STATUS_CH_HIGH 0x0Eu

typedef struct {
    uint8_t tag;
    char value[K2000_PROTO_MAX_FIELD];
    uint8_t value_len;
} k2000_field_t;

typedef enum {
    K2000_EVT_FIELD = 0,
    K2000_EVT_CURSOR,
    K2000_EVT_BLINK_START,
    K2000_EVT_BLINK_END,
    K2000_EVT_STATUS,
    K2000_EVT_UNKNOWN,
} k2000_evt_type_t;

typedef struct {
    k2000_evt_type_t type;
    k2000_field_t field;
    uint16_t pos;
    uint8_t status_tag;
    uint8_t status_value;
} k2000_event_t;

typedef struct {
    void (*on_event)(const k2000_event_t *evt);
    void (*on_unknown)(uint8_t byte);
} k2000_proto_cb_t;

static inline bool k2000_is_status_tag(uint8_t tag)
{
    return (tag == K2000_TAG_STATUS_REM) || (tag == K2000_TAG_STATUS_SHIFT) ||
           (tag == K2000_TAG_STATUS_HOLD) || (tag == K2000_TAG_STATUS_REL) ||
           (tag == K2000_TAG_STATUS_STEP) || (tag == K2000_TAG_STATUS_CH_HIGH);
}

void k2000_proto_init(const k2000_proto_cb_t *cb);
void k2000_proto_reset(void);
void k2000_proto_feed(uint8_t byte);
