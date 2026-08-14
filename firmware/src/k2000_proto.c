#include <stdbool.h>
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
    K2000_STATE_STATUS_ARG,
    K2000_STATE_POS_ARG,
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

/* Inline symbol tags (u / degree) are content: they belong inside the field
 * value as UTF-8 (so reading_split keeps "1.234" + "V" together and the unit
 * becomes "uV"). Returns true when byte is a symbol tag. */
static bool append_symbol(uint8_t byte)
{
    static const char s_micro[] = "\xC2\xB5";
    static const char s_degree[] = "\xC2\xB0";
    const char *utf8;
    uint8_t len;
    uint8_t i;

    if (byte == K2000_TAG_SYM_MICRO) {
        utf8 = s_micro;
        len = (uint8_t)(sizeof(s_micro) - 1u);
    } else if (byte == K2000_TAG_SYM_DEGREE) {
        utf8 = s_degree;
        len = (uint8_t)(sizeof(s_degree) - 1u);
    } else {
        return false;
    }
    for (i = 0; i < len; i++) {
        if (s_evt.field.value_len >= sizeof(s_evt.field.value) - 1u) {
            emit_unknown((uint8_t)utf8[i]);
            break;
        }
        s_evt.field.value[s_evt.field.value_len++] = utf8[i];
    }
    return true;
}

/* Event type for a segment/flush control byte, or K2000_EVT_UNKNOWN. */
static k2000_evt_type_t control_tag_type(uint8_t byte)
{
    switch (byte) {
    case K2000_TAG_SEG_FIRST:
    case K2000_TAG_SEG_SECOND:
    case K2000_TAG_SEG_FULL:
        return K2000_EVT_SEGMENT;
    case K2000_TAG_FLUSH:
        return K2000_EVT_FLUSH;
    default:
        return K2000_EVT_UNKNOWN;
    }
}

/* IDLE-state handler for a lone text tag (no field open yet). */
static bool feed_lone_text_tag(uint8_t byte)
{
    k2000_evt_type_t type = control_tag_type(byte);
    if (byte == K2000_TAG_SYM_MICRO || byte == K2000_TAG_SYM_DEGREE) {
        type = K2000_EVT_SYMBOL;
    }
    if (type == K2000_EVT_UNKNOWN) {
        return false;
    }
    s_evt.type = type;
    s_evt.ctrl = byte;
    emit(&s_evt);
    memset(&s_evt, 0, sizeof(s_evt));
    return true;
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
            s_state = K2000_STATE_POS_ARG;
        } else if (byte == K2000_TAG_BLINK_BEGIN) {
            s_state = K2000_STATE_BLINK_ARG;
        } else if (k2000_is_status_tag(byte)) {
            s_evt.type = K2000_EVT_STATUS;
            s_evt.status_tag = byte;
            s_state = K2000_STATE_STATUS_ARG;
        } else if (feed_lone_text_tag(byte)) {
            /* lone symbol / segment / flush consumed as its own event */
        } else {
            s_evt_tag = byte;
            s_evt.type = K2000_EVT_FIELD;
            s_evt.field.tag = byte;
            s_state = K2000_STATE_VALUE;
        }
        break;

    case K2000_STATE_VALUE:
        if (byte == K2000_TAG_POS) {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_evt.type = K2000_EVT_CURSOR;
            s_state = K2000_STATE_POS_ARG;
        } else if (byte == K2000_TAG_BLINK_BEGIN) {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_state = K2000_STATE_BLINK_ARG;
        } else if (k2000_is_status_tag(byte)) {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_evt.type = K2000_EVT_STATUS;
            s_evt.status_tag = byte;
            s_state = K2000_STATE_STATUS_ARG;
        } else if (append_symbol(byte)) {
            /* u / degree stay inside the current field value as UTF-8; the
             * field is NOT closed, so a trailing "V" stays the same unit. */
        } else if (control_tag_type(byte) != K2000_EVT_UNKNOWN) {
            /* segment / flush close the current field first, then emit their
             * own event and return to IDLE. Must run before the >=0x80
             * new-field-tag check since segment tags are below 0x80. */
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            (void)feed_lone_text_tag(byte);
            s_state = K2000_STATE_IDLE;
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

    case K2000_STATE_STATUS_ARG:
        s_evt.status_value = byte;
        emit(&s_evt);
        memset(&s_evt, 0, sizeof(s_evt));
        s_state = K2000_STATE_IDLE;
        break;

    case K2000_STATE_POS_ARG:
        if (byte >= '0' && byte <= '9') {
            s_evt.pos = (uint16_t)(s_evt.pos * 10u + (uint8_t)(byte - '0'));
        } else {
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_state = K2000_STATE_IDLE;
        }
        break;

    default:
        s_state = K2000_STATE_IDLE;
        break;
    }
}
