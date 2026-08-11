#include "panel_transform.h"

typedef struct {
    uint16_t ui_w;
    uint16_t ui_h;
    uint16_t fb_w;
    uint16_t fb_h;
    uint8_t valid;
    uint8_t transpose;
} panel_transform_t;

static panel_transform_t s_pt;

void panel_transform_init(uint16_t ui_w, uint16_t ui_h, uint16_t fb_w,
                          uint16_t fb_h)
{
    if (ui_w == 0u || ui_h == 0u || fb_w == 0u || fb_h == 0u ||
        ((fb_w != ui_w || fb_h != ui_h) &&
         (fb_w != ui_h || fb_h != ui_w))) {
        s_pt.valid = 0;
        s_pt.ui_w = 0;
        s_pt.ui_h = 0;
        s_pt.fb_w = 0;
        s_pt.fb_h = 0;
        return;
    }
    s_pt.ui_w = ui_w;
    s_pt.ui_h = ui_h;
    s_pt.fb_w = fb_w;
    s_pt.fb_h = fb_h;
    s_pt.transpose = (uint8_t)(fb_w == ui_h && fb_h == ui_w);
    s_pt.valid = 1;
}

void panel_transform_ui_to_fb(uint16_t ui_x, uint16_t ui_y, uint16_t *fb_x,
                              uint16_t *fb_y)
{
    if (fb_x != 0) {
        *fb_x = 0;
    }
    if (fb_y != 0) {
        *fb_y = 0;
    }
    if (!s_pt.valid || ui_x >= s_pt.ui_w || ui_y >= s_pt.ui_h ||
        fb_x == 0 || fb_y == 0) {
        return;
    }
    if (s_pt.transpose != 0u) {
        *fb_x = ui_y;
        *fb_y = ui_x;
    } else {
        *fb_x = ui_x;
        *fb_y = ui_y;
    }
}

void panel_transform_fb_to_ui(uint16_t fb_x, uint16_t fb_y, uint16_t *ui_x,
                              uint16_t *ui_y)
{
    if (ui_x != 0) {
        *ui_x = 0;
    }
    if (ui_y != 0) {
        *ui_y = 0;
    }
    if (!s_pt.valid || fb_x >= s_pt.fb_w || fb_y >= s_pt.fb_h ||
        ui_x == 0 || ui_y == 0) {
        return;
    }
    if (s_pt.transpose != 0u) {
        *ui_x = fb_y;
        *ui_y = fb_x;
    } else {
        *ui_x = fb_x;
        *ui_y = fb_y;
    }
}

uint16_t panel_transform_ui_width(void)
{
    return s_pt.ui_w;
}

uint16_t panel_transform_ui_height(void)
{
    return s_pt.ui_h;
}

uint16_t panel_transform_fb_width(void)
{
    return s_pt.fb_w;
}

uint16_t panel_transform_fb_height(void)
{
    return s_pt.fb_h;
}

void panel_transform_ui_rect_to_fb(uint16_t ui_x, uint16_t ui_y,
                                   uint16_t ui_w, uint16_t ui_h,
                                   uint16_t *fb_x, uint16_t *fb_y,
                                   uint16_t *fb_w, uint16_t *fb_h)
{
    if (fb_x != 0) *fb_x = 0u;
    if (fb_y != 0) *fb_y = 0u;
    if (fb_w != 0) *fb_w = 0u;
    if (fb_h != 0) *fb_h = 0u;
    if (!s_pt.valid || fb_x == 0 || fb_y == 0 || fb_w == 0 || fb_h == 0 ||
        ui_w == 0u || ui_h == 0u || ui_x >= s_pt.ui_w || ui_y >= s_pt.ui_h ||
        ui_w > (uint16_t)(s_pt.ui_w - ui_x) ||
        ui_h > (uint16_t)(s_pt.ui_h - ui_y)) {
        return;
    }
    if (s_pt.transpose != 0u) {
        *fb_x = ui_y;
        *fb_y = ui_x;
        *fb_w = ui_h;
        *fb_h = ui_w;
    } else {
        *fb_x = ui_x;
        *fb_y = ui_y;
        *fb_w = ui_w;
        *fb_h = ui_h;
    }
}
