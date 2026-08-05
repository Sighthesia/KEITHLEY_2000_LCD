#pragma once

#include <stdbool.h>
#include <stdint.h>

void hal_board_init(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
int hal_uart_receive_byte(void);
