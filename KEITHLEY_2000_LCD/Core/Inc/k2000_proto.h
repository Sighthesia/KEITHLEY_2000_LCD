#pragma once

#include <stdbool.h>
#include <stdint.h>

#define K2000_PROTO_MAX_FIELD 32u

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
    K2000_EVT_UNKNOWN,
} k2000_evt_type_t;

typedef struct {
    k2000_evt_type_t type;
    k2000_field_t field;
    uint16_t pos;
} k2000_event_t;

typedef struct {
    void (*on_event)(const k2000_event_t *evt);
    void (*on_unknown)(uint8_t byte);
} k2000_proto_cb_t;

void k2000_proto_init(const k2000_proto_cb_t *cb);
void k2000_proto_reset(void);
void k2000_proto_feed(uint8_t byte);
