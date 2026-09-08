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
void hal_uart_rx_irq(void);
uint32_t hal_uart_rx_overflow_count(void);
bool hal_uart_rx_recovering(void);

/* Key-matrix bench diagnosis: 1 = replace the single-byte host codes with
 * "KEYS rXcY ..." lines listing EVERY contacted cell (change-reported, so a
 * R6/R7 short shows as "KEYS r0c5 r0c6" and a dead row/column shows
 * nothing). For matrix bring-up only; normal/host builds keep 0. */
#define K2000_KEY_DEBUG 1U

/* Scan the 4x8 key matrix (rows PB0..PB3 x cols PB4..PB11, col0=R9=PB11 ...
 * col7=R16=PB4) and return the raw position code for one pressed key
 * (KEYPAD_RAW(row,col)), or 0 when no key is pressed. Drives each row high
 * in turn and reads the column inputs (33k external pull-down to GND).
 * Position codes feed keypad_scan() for debounce. */
int hal_keypad_read_code(void);
#if K2000_KEY_DEBUG
/* Bench diagnosis: poll the whole matrix and print every contacted cell.
 * Replaces the single-byte host send while enabled (see K2000_KEY_DEBUG). */
void hal_keypad_debug_poll(void);
#endif

/* SHT3x temperature/humidity over soft-I2C (PB15=SCL/PB14=SDA). Millidegrees C / milli-pct RH; false on NACK/CRC. */
bool hal_sht3x_read_milli(int32_t *temp_milli_c, int32_t *rh_milli_pct);
