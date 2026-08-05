#include <string.h>

#include "k2000_proto.h"

#define K2000_START 0x0Du
#define K2000_TAG_POS 0x04u
#define K2000_TAG_BLINK_BEGIN 0x0Bu
#define K2000_TAG_BLINK_BEGIN_ARG 0x01u
#define K2000_TAG_BLINK_END_ARG 0x00u

typedef enum {
    K2000_STATE_IDLE,
    K2000_STATE_VALUE,
    K2000_STATE_BLINK_ARG,
} k2000_state_t;

static const k2000_proto_cb_t *s_cb;
static k2000_state_t s_state;
static k2000_event_t s_evt;
static uint8_t s_evt_tag;

void k2000_proto_init(const k2000_proto_cb_t *cb)
{
    s_cb = cb;
    k2000_proto_reset();
}

void k2000_proto_reset(void)
{
    s_state = K2000_STATE_IDLE;
    memset(&s_evt, 0, sizeof(s_evt));
    s_evt_tag = 0;
}

static void emit(const k2000_event_t *evt)
{
    if (s_cb != 0 && s_cb->on_event != 0) {
        s_cb->on_event(evt);
    }
}

static void emit_unknown(uint8_t byte)
{
    if (s_cb != 0 && s_cb->on_unknown != 0) {
        s_cb->on_unknown(byte);
    }
}

void k2000_proto_feed(uint8_t byte)
{
    if (byte == K2000_START) {
        s_state = K2000_STATE_IDLE;
        memset(&s_evt, 0, sizeof(s_evt));
        s_evt_tag = 0;
        return;
    }

    switch (s_state) {
    case K2000_STATE_IDLE:
        if (byte == K2000_TAG_POS) {
            s_evt.type = K2000_EVT_CURSOR;
            s_state = K2000_STATE_VALUE;
        } else if (byte == K2000_TAG_BLINK_BEGIN) {
            s_state = K2000_STATE_BLINK_ARG;
        } else {
            s_evt_tag = byte;
            s_evt.type = K2000_EVT_FIELD;
            s_evt.field.tag = byte;
            s_state = K2000_STATE_VALUE;
        }
        break;

    case K2000_STATE_VALUE:
        if (byte == K2000_TAG_POS) {
            s_evt.type = K2000_EVT_CURSOR;
            memset(&s_evt.field, 0, sizeof(s_evt.field));
            s_state = K2000_STATE_VALUE;
        } else if (byte == K2000_TAG_BLINK_BEGIN) {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_state = K2000_STATE_BLINK_ARG;
        } else if (byte >= 0x80u) {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_evt_tag = byte;
            s_evt.type = K2000_EVT_FIELD;
            s_evt.field.tag = byte;
        } else {
            if (s_evt.field.value_len < sizeof(s_evt.field.value) - 1u) {
                s_evt.field.value[s_evt.field.value_len++] = (char)byte;
            } else {
                emit_unknown(byte);
            }
        }
        break;

    case K2000_STATE_BLINK_ARG:
        if (byte == K2000_TAG_BLINK_BEGIN_ARG) {
            s_evt.type = K2000_EVT_BLINK_START;
        } else if (byte == K2000_TAG_BLINK_END_ARG) {
            s_evt.type = K2000_EVT_BLINK_END;
        } else {
            s_evt.type = K2000_EVT_UNKNOWN;
        }
        emit(&s_evt);
        memset(&s_evt, 0, sizeof(s_evt));
        s_state = K2000_STATE_IDLE;
        break;

    default:
        s_state = K2000_STATE_IDLE;
        break;
    }
}
