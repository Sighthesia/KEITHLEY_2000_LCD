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

/* VFD canvas (see k2000_proto.h). The host maintains the cursor across
 * writes: text advances it, 0x04+ASCII-digits repositions, 0x02 clears.
 * Content is persistent like the real tube -- partial updates merge. */
static char s_vfd[K2000_VFD_COLS][K2000_VFD_CELL_MAX];
static uint8_t s_vfd_len[K2000_VFD_COLS];
static uint8_t s_vfd_end;
static uint8_t s_vfd_cursor;

static void vfd_put(char c);

static void vfd_clear(void)
{
    memset(s_vfd, 0, sizeof(s_vfd));
    memset(s_vfd_len, 0, sizeof(s_vfd_len));
    s_vfd_end = 0u;
    s_vfd_cursor = 0u;
}

static void vfd_put_bytes(const char *bytes, uint8_t length)
{
    if (s_vfd_cursor < K2000_VFD_COLS) {
        if (length > K2000_VFD_CELL_MAX)
            length = K2000_VFD_CELL_MAX;
        memcpy(s_vfd[s_vfd_cursor], bytes, length);
        s_vfd_len[s_vfd_cursor] = length;
        s_vfd_cursor++;
        if (s_vfd_cursor > s_vfd_end)
            s_vfd_end = s_vfd_cursor;
    }
}

static void vfd_put(char c)
{
    vfd_put_bytes(&c, 1u);
}

uint8_t k2000_vfd_line(char *out, uint8_t size)
{
    uint8_t column;
    uint8_t n = 0u;
    if (out == 0 || size == 0u) {
        return 0u;
    }
    for (column = 0u; column < s_vfd_end; column++) {
        uint8_t i;
        uint8_t cell_len = s_vfd_len[column] == 0u ? 1u : s_vfd_len[column];
        if ((uint16_t)n + cell_len >= size)
            break;
        if (s_vfd_len[column] == 0u)
            out[n++] = ' ';
        else
            for (i = 0u; i < s_vfd_len[column]; i++)
                out[n++] = s_vfd[column][i];
    }
    out[n] = '\0';
    return n;
}

void k2000_vfd_clear(void) { vfd_clear(); }

void k2000_vfd_set_cursor(uint8_t column)
{
    s_vfd_cursor = column < K2000_VFD_COLS ? column : K2000_VFD_COLS;
}

static uint8_t utf8_token_width(const uint8_t *text, uint8_t length)
{
    uint8_t b0, b1, b2, b3;
    if (text == 0 || length == 0u) return 0u;
    b0 = text[0];
    if (b0 <= 0x7Fu) return 1u;
    if (b0 >= 0xC2u && b0 <= 0xDFu)
        return length >= 2u && text[1] >= 0x80u && text[1] <= 0xBFu ? 2u : 0u;
    if (length < 3u) return 0u;
    b1 = text[1]; b2 = text[2];
    if (b0 == 0xE0u && (b1 < 0xA0u || b1 > 0xBFu)) return 0u;
    if (b0 >= 0xE1u && b0 <= 0xECu && (b1 < 0x80u || b1 > 0xBFu)) return 0u;
    if (b0 == 0xEDu && (b1 < 0x80u || b1 > 0x9Fu)) return 0u;
    if (b0 >= 0xEEu && b0 <= 0xEFu && (b1 < 0x80u || b1 > 0xBFu)) return 0u;
    if (b2 < 0x80u || b2 > 0xBFu) return 0u;
    if (b0 >= 0xE0u && b0 <= 0xEFu) return 3u;
    if (length < 4u) return 0u;
    b3 = text[3];
    if (b0 == 0xF0u && (b1 < 0x90u || b1 > 0xBFu)) return 0u;
    if (b0 >= 0xF1u && b0 <= 0xF3u && (b1 < 0x80u || b1 > 0xBFu)) return 0u;
    if (b0 == 0xF4u && (b1 < 0x80u || b1 > 0x8Fu)) return 0u;
    if (b2 < 0x80u || b2 > 0xBFu || b3 < 0x80u || b3 > 0xBFu) return 0u;
    return b0 <= 0xF4u ? 4u : 0u;
}

bool k2000_vfd_write_utf8(const char *text, uint8_t length)
{
    uint8_t i = 0u, width;
    if (text == 0) return false;
    while (i < length) {
        width = utf8_token_width((const uint8_t *)&text[i], (uint8_t)(length - i));
        if (width == 0u) return false;
        i = (uint8_t)(i + width);
    }
    i = 0u;
    while (i < length) {
        width = utf8_token_width((const uint8_t *)&text[i], (uint8_t)(length - i));
        vfd_put_bytes(&text[i], width);
        i = (uint8_t)(i + width);
    }
    return true;
}

void k2000_proto_init(const k2000_proto_cb_t *cb)
{
    s_cb = cb;
    k2000_proto_reset();
    vfd_clear();
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
    if ((uint16_t)s_evt.field.value_len + len >= sizeof(s_evt.field.value)) {
        emit_unknown(byte);
        return true;
    }
    for (i = 0; i < len; i++) {
        s_evt.field.value[s_evt.field.value_len++] = utf8[i];
    }
    vfd_put_bytes(utf8, len);
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

/* POS arguments are terminated by the next protocol byte. Text terminators
 * are not replayed, but a TAG/control byte must remain visible to the parser. */
static bool is_protocol_boundary(uint8_t byte)
{
    return byte < 0x20u || byte >= 0x80u;
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
    if (byte == K2000_TAG_FLUSH) {
        vfd_clear();
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
        /* A frame start must not swallow an open field: hosts that stream
         * readings without a closing tag (or terminate the field implicitly
         * at the next frame) still delivered a complete value by now. Emit
         * it before resetting; incomplete POS/BLINK/STATUS arguments are
         * corrupt fragments and stay discarded. */
        if (s_state == K2000_STATE_VALUE && s_evt.type == K2000_EVT_FIELD) {
            emit(&s_evt);
        }
        s_state = K2000_STATE_IDLE;
        memset(&s_evt, 0, sizeof(s_evt));
        s_evt_tag = 0;
        /* 0x0D is a parser/cursor boundary, not an event. */
        s_vfd_cursor = 0u;
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
            vfd_put((char)byte);
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
            uint16_t pos = s_evt.pos;
            emit(&s_evt);
            memset(&s_evt, 0, sizeof(s_evt));
            s_state = K2000_STATE_IDLE;
            /* VFD cursor reposition. */
            s_vfd_cursor = pos < K2000_VFD_COLS
                               ? (uint8_t)pos
                               : (uint8_t)(K2000_VFD_COLS - 1u);
            if (is_protocol_boundary(byte)) {
                k2000_proto_feed(byte);
            }
        }
        break;

    default:
        s_state = K2000_STATE_IDLE;
        break;
    }
}
