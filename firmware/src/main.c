#include <stdint.h>
#include <string.h>

#include "hal_stub.h"
#include "k2000_proto.h"
#include "lt7680_bus.h"
#include "lt7680_gfx.h"
#include "main_display.h"
#include "reading_split.h"
#include "scene.h"
#include "ui_model.h"

static ui_model_t s_model;
static k2000_event_t s_last_evt;
static volatile uint32_t s_evt_count;

static void on_proto_event(const k2000_event_t *evt)
{
    char num[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    uint8_t num_len;
    uint8_t unit_len;
    uint8_t special;

    if (evt == 0) {
        return;
    }
    s_last_evt = *evt;
    s_evt_count++;

    switch (evt->type) {
    case K2000_EVT_FIELD:
        if (reading_is_special(evt->field.value, evt->field.value_len,
                               &special)) {
            num_len = evt->field.value_len;
            if (num_len >= sizeof(num)) num_len = (uint8_t)(sizeof(num) - 1u);
            memcpy(num, evt->field.value, num_len);
            num[num_len] = '\0';
            unit_len = 0u;
            unit[0] = '\0';
        } else {
            reading_split(evt->field.value, evt->field.value_len, num, &num_len,
                          unit, &unit_len);
            special = 0u;
        }
        ui_model_apply_reading(&s_model, num, num_len, unit, unit_len,
                               special);
        break;
    case K2000_EVT_STATUS:
        ui_model_apply_status(&s_model, evt->status_tag, evt->status_value);
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
    case K2000_EVT_SYMBOL:
        ui_model_apply_symbol(&s_model, evt->ctrl);
        break;
    case K2000_EVT_SEGMENT:
        ui_model_apply_segment(&s_model, evt->ctrl);
        break;
    case K2000_EVT_FLUSH:
        ui_model_apply_flush(&s_model);
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

/* Reading scene (scene id 0). The host skeleton has no panel, so render only
 * formats the model; the CubeMX build draws the produced frame on the panel. */
static void reading_scene_enter(void)
{
}

static void reading_scene_exit(void)
{
}

static void reading_scene_render(void)
{
    main_display_frame_t frame;
    main_display_format(&s_model, &frame);
    (void)frame;
}

static const scene_t s_reading_scene = {
    .enter = reading_scene_enter,
    .exit = reading_scene_exit,
    .render = reading_scene_render,
};

int main(void)
{
    lt7680_panel_t panel;

    hal_lt7680_init();
    k2000_proto_init(&s_proto_cb);
    ui_model_init(&s_model);
    scene_mgr_init();
    scene_mgr_register(0, &s_reading_scene);
    scene_mgr_enter(0);

    panel.width = 320;
    panel.height = 960;
    panel.bpp = 16;

    (void)lt7680_reset();
    (void)lt7680_wait_ready(500);
    (void)lt7680_gfx_init(&panel);
    (void)lt7680_gfx_clear(0x0000u);

    for (;;) {
        /* Fixed protocol demo: a reading field followed by a status TAG, so
         * FIELD -> reading_split -> apply_reading and STATUS -> apply_status
         * are both exercised on every pass. */
        k2000_proto_feed(0x0Du);
        k2000_proto_feed(0x01u);
        k2000_proto_feed('1');
        k2000_proto_feed('.');
        k2000_proto_feed('2');
        k2000_proto_feed('3');
        k2000_proto_feed('4');
        k2000_proto_feed('5');
        k2000_proto_feed('V');
        k2000_proto_feed('D');
        k2000_proto_feed('C');
        k2000_proto_feed(0x08u);   /* status tag 0x08: HOLD/TRIG group */
        k2000_proto_feed(0x80u);   /* HOLD on */
        scene_mgr_render();
    }
}
