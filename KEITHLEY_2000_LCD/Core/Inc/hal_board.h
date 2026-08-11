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

/* Scan the 4x8 key matrix (rows PC13/PC14/PC15/PB10 x cols PB0..PB7) and
 * return the raw position code for one pressed key (KEYPAD_RAW(row,col)),
 * or 0 when no key is pressed. Drives each column low in turn and reads the
 * row inputs (pull-up). Position codes feed keypad_scan() for debounce. */
int hal_keypad_read_code(void);
