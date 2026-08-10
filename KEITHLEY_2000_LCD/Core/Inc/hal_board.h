#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Landscape presentation: drive the physically 320x960 panel as a 960x320
 * UI. This real-device validation uses ST7701S MADCTL 0x36=0x60 (MX|MV) and
 * swaps the LT7680 timings in main.c. */
#define PANEL_LANDSCAPE 1u

void hal_board_init(void);
void hal_panel_init(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
void hal_uart_send_text(const char *text);
void hal_uart_send_hex8(uint8_t value);
int hal_uart_receive_byte(void);
