#include <stdint.h>

#include "hal_stub.h"
#include "k2000_proto.h"
#include "lt7680_bus.h"
#include "lt7680_gfx.h"
#include "ui_model.h"

static ui_model_t s_model;
static k2000_event_t s_last_evt;
static volatile uint32_t s_evt_count;

static void on_proto_event(const k2000_event_t *evt)
{
    if (evt == 0) {
        return;
    }
    s_last_evt = *evt;
    s_evt_count++;

    switch (evt->type) {
    case K2000_EVT_FIELD:
        ui_model_apply_field(&s_model, evt->field.tag, evt->field.value,
                             evt->field.value_len);
        break;
    case K2000_EVT_CURSOR:
        ui_model_apply_cursor(&s_model, evt->pos);
        break;
    case K2000_EVT_BLINK_START:
        ui_model_apply_blink(&s_model, true);
        break;
    case K2000_EVT_BLINK_END:
        ui_model_apply_blink(&s_model, false);
        break;
    default:
        break;
    }
}

static void on_proto_unknown(uint8_t byte)
{
    (void)byte;
}

static const k2000_proto_cb_t s_proto_cb = {
    .on_event = on_proto_event,
    .on_unknown = on_proto_unknown,
};

int main(void)
{
    lt7680_panel_t panel;

    hal_lt7680_init();
    k2000_proto_init(&s_proto_cb);
    ui_model_init(&s_model);

    panel.width = 320;
    panel.height = 960;
    panel.bpp = 16;

    (void)lt7680_gfx_init(&panel);
    (void)lt7680_reset();
    (void)lt7680_wait_ready(500);
    (void)lt7680_gfx_clear(0x0000u);

    for (;;) {
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        k2000_proto_feed('1');
        k2000_proto_feed('2');
        k2000_proto_feed('3');
        ui_model_render(&s_model);
    }
}
