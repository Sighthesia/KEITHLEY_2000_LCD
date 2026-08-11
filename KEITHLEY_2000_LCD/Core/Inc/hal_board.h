#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Keep the verified RGB timing orientation.  The panel's MADCTL command is
 * not used here because this RGB path did not accept the swapped 960x320
 * timing; coordinate rotation must be solved in the LT7680 framebuffer path. */
#define PANEL_LANDSCAPE 0u

void hal_board_init(void);
void hal_panel_init(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
void hal_uart_send_text(const char *text);
void hal_uart_send_hex8(uint8_t value);
int hal_uart_receive_byte(void);
