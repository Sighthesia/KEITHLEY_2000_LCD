#pragma once

#include <stdbool.h>
#include <stdint.h>

void hal_board_init(void);
void hal_panel_init(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
void hal_uart_send_text(const char *text);
void hal_uart_send_hex8(uint8_t value);
int hal_uart_receive_byte(void);
