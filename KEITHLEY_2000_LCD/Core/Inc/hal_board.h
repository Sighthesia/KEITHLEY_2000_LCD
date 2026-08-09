#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Landscape presentation: drive the physically 320x960 (portrait) panel
 * rotated so the UI renders 960 wide x 320 tall. Real-device verification
 * pending (Task 4 Step 5). When 1: hal_panel_init() sends ST7701S MADCTL
 * (0x36) and the LT7680 panel timing is swapped to 960x320 in main.c.
 * Default 0 keeps the verified 320x960 color-bars milestone intact. */
#define PANEL_LANDSCAPE 0u

void hal_board_init(void);
void hal_panel_init(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
void hal_uart_send_text(const char *text);
void hal_uart_send_hex8(uint8_t value);
int hal_uart_receive_byte(void);
