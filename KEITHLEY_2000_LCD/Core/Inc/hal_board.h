#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Host-UART text silence: the panel shares one TX line with the host, and
 * the host speaks strict binary (frames in, key codes out). ANY text line
 * (boot banner, STATUS/PASS/FAIL, STALL) is protocol garbage that can wedge
 * the host handshake (silent host = no beep, no frames). 0 = text dropped,
 * key bytes (hal_uart_send) unaffected. Bench diagnosis sets 1. */
#define K2000_UART_LOG 0U

/* 0x0F-poll identity reply (V16 ROM has it, but bus captures prove the
 * real host never polls -- V16 boots silent and the host blind-streams).
 * Replying derailed the host into the cal-prompt state. Keep 0. */
#define K2000_IDENTITY_REPLY 1U

/* Keep the verified RGB timing orientation.  The panel's MADCTL command is
 * not used here because this RGB path did not accept the swapped 960x320
 * timing; coordinate rotation must be solved in the LT7680 framebuffer path. */
#define PANEL_LANDSCAPE 0u

void hal_board_init(void);
void hal_panel_init(void);
/* Boot cosmetics: park the shared LT7680/panel reset line LOW before any
 * slow boot work runs, so the LT7680 cannot stream its default colour-bar
 * test pattern (or stale warm-reboot SDRAM pixels) to the backlit panel. */
void hal_display_early_reset_hold(void);
/* Pulsed reset + earliest-accepted display blank (REG[12h]=0x08 verified
 * by readback). Replaces lt7680_reset() + late blank in the normal boot;
 * false when the blank never confirmed (boot still proceeds via
 * lt7680_wait_ready and re-asserts the blank afterwards). */
bool hal_display_boot_blank(void);
void hal_uart_send(const uint8_t *data, uint16_t len);
void hal_uart_send_text(const char *text);
void hal_uart_send_hex8(uint8_t value);
int hal_uart_receive_byte(void);
void hal_uart_rx_irq(void);
uint32_t hal_uart_rx_overflow_count(void);
bool hal_uart_rx_recovering(void);

/* Key-matrix bench diagnosis: 1 = replace the single-byte host codes with
 * "KEYS ..." lines listing EVERY contacted cell by key name ("KEYS FREQ",
 * unwired cells as "r3c0"; "-" when idle; change-reported, so a shorted
 * pair shows as e.g. "KEYS OHM OHM4W"). For matrix bring-up only;
 * normal/host builds keep 0. */
#define K2000_KEY_DEBUG 0U

/* Scan the 4x8 key matrix (rows PB0..PB3 x cols PB4..PB11, col0=R9=PB11 ...
 * col7=R16=PB4) and report the contact set as KEYPAD_CELL bits in *mask
 * (0 = none). Drives each row high in turn and reads the column inputs
 * (33k external pull-down to GND). Returns the active cell count, or -1
 * when mask is null. Feeds keypad_scan() for debounce. */
int hal_keypad_read_mask(uint32_t *mask);
#if K2000_KEY_DEBUG
/* Bench diagnosis: poll the whole matrix and print every contacted cell.
 * Replaces the single-byte host send while enabled (see K2000_KEY_DEBUG). */
void hal_keypad_debug_poll(void);
#endif

/* SHT3x temperature/humidity over soft-I2C (PB15=SCL/PB14=SDA). Millidegrees C / milli-pct RH; false on NACK/CRC. */
bool hal_sht3x_read_milli(int32_t *temp_milli_c, int32_t *rh_milli_pct);
