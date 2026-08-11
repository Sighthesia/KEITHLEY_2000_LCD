#pragma once

#include <stdint.h>

/* Adapter between the logical UI view space (960x320 landscape) and the
 * LT7680 native framebuffer (currently a portrait 320x960 panel). View code
 * only speaks logical UI coordinates; this module is the single place that
 * knows the physical panel orientation (ADR-0001).
 *
 * Default/target setup is ui 960x320 -> fb 320x960. The mapping is a pure
 * transpose with no axis inversion: fb_x = ui_y, fb_y = ui_x. A future
 * physical-landscape panel only changes the mapping here. */

void panel_transform_init(uint16_t ui_w, uint16_t ui_h, uint16_t fb_w,
                          uint16_t fb_h);
void panel_transform_ui_to_fb(uint16_t ui_x, uint16_t ui_y, uint16_t *fb_x,
                              uint16_t *fb_y);
void panel_transform_fb_to_ui(uint16_t fb_x, uint16_t fb_y, uint16_t *ui_x,
                              uint16_t *ui_y);
uint16_t panel_transform_ui_width(void);
uint16_t panel_transform_ui_height(void);
uint16_t panel_transform_fb_width(void);
uint16_t panel_transform_fb_height(void);
void panel_transform_ui_rect_to_fb(uint16_t ui_x, uint16_t ui_y,
                                   uint16_t ui_w, uint16_t ui_h,
                                   uint16_t *fb_x, uint16_t *fb_y,
                                   uint16_t *fb_w, uint16_t *fb_h);
