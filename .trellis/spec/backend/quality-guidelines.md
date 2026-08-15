# Embedded Runtime Quality Guidelines

## Scenario: Interrupt RX, Trend Projection, and Cooperative Display

### 1. Scope / Trigger

Apply this contract when firmware receives protocol bytes in an ISR while the
main loop performs LT7680 drawing, or when readings are retained in a rolling
trend buffer. It prevents byte loss during slow graphics operations, stale
ring slots reappearing after wrap, and large automatic objects exhausting the
STM32F103C8 1KB reserved stack.

### 2. Signatures

```c
#define UART_RX_QUEUE_CAPACITY 512u
void uart_rx_queue_push_isr(uart_rx_queue_t *queue, uint8_t byte);
int uart_rx_queue_pop(uart_rx_queue_t *queue);
uint32_t uart_rx_queue_overflow_count(const uart_rx_queue_t *queue);

#define TREND_BUCKET_MS 20u
#define TREND_BUCKET_COUNT 500u
#define TREND_MAX_COLUMNS 240u
bool trend_buffer_add(trend_buffer_t *trend, uint32_t now_ms,
                      const char *text, const char *unit);
uint16_t trend_buffer_project(const trend_buffer_t *trend, uint32_t now_ms,
                              trend_column_t *columns, uint16_t capacity);
```

`hal_uart_rx_irq()` may capture bytes and error status only. Protocol parsing,
model mutation, trend conversion, and drawing belong to the main loop.

### 3. Contracts

- UART queue indices are monotonic 16-bit counters. Mask only when addressing
  the 512-byte array; `head - tail == 512` means full capacity is occupied.
- ISR reads USART status then data to clear RXNE/ORE/NE/FE in STM32-defined
  order. The main loop briefly masks USART1 IRQ when atomically changing queue
  indices; it does not globally disable SysTick or unrelated interrupts.
- Overflow increments a counter, discards queued/arriving bytes, and publishes
  the first subsequent `0x0D` as the parser resynchronization byte.
- Trend buckets store min/max in normalized base units and include occupancy
  plus enough generation/time information to reject wrapped slots outside the
  current 10-second window.
- Decimal parsing is bounded, locale-independent, rejects overflow/Inf, and
  does not use heap allocation, `strtof`, or formatted I/O.
- Trend history and projection arrays are static or caller-owned. Never place
  500 buckets or 240 columns in an automatic target stack frame.
- Rendering is resumable. Each step emits bounded bitmap runs or graph columns,
  then returns so the main loop can drain RX. The target acceptance limit is a
  measured worst-case slice below 10ms.

### 4. Validation & Error Matrix

| Condition | Required behavior |
| --- | --- |
| Queue contains 512 bytes | Accept all 512; overflow only on byte 513 |
| UART ORE/NE/FE | Read SR then DR, count/drop affected input, keep ISR bounded |
| Queue overflow | Enter recovery, ignore bytes until `0x0D`, emit that delimiter |
| Numeric exponent/value exceeds `FLT_MAX` | Reject sample; never store Inf/NaN |
| Tick moves backward or cannot map safely | Reset trend history |
| Ring slot generation is outside visible window | Treat slot as unoccupied |
| Measurement dimension changes | Clear history before adding the new sample |
| `OVERFLOW`, `----`, malformed number/unit | Do not insert trend sample |
| Render frame changes mid-paint | Finish or explicitly supersede at a safe state boundary |

### 5. Good/Base/Bad Cases

- Good: 500 readings/s collapse into 50Hz min/max buckets and preserve a short
  spike in the 240-column projection.
- Base: no samples leaves an empty projection and visible static grid.
- Bad: masked head/tail indices use one empty slot and silently reduce a
  documented 512-byte queue to 511 bytes.
- Bad: projection checks only array index and lets an old wrapped slot reappear
  as a future sample.

### 6. Tests Required

- Push exactly 512 UART bytes, assert no overflow, push one more, assert one
  overflow and recovery mode; then assert `0x0D` is the first recovered byte.
- Assert FIFO ordering and empty return `-1`.
- Reject malformed numbers, invalid UTF-8 unit prefixes, huge mantissas, and
  huge exponents.
- Assert same-bucket min/max preservation, dimension reset, timeout reset,
  backward-tick reset, wrap generation filtering, and 500-to-240 projection.
- Build target with `-Werror`, run `arm-none-eabi-size`, and inspect that trend
  arrays reside in static RAM rather than an automatic stack frame.
- Measure cooperative renderer slices on hardware; host tests can verify work
  budgets/state progression but cannot prove elapsed microcontroller time.

### 7. Wrong vs Correct

#### Wrong

```c
next = (head + 1u) & 511u;
if (next == tail) overflow(); /* capacity is only 511 */

void USART1_IRQHandler(void) {
    k2000_proto_feed((uint8_t)USART1->DR); /* parsing inside ISR */
}
```

#### Correct

```c
if ((uint16_t)(head - tail) >= UART_RX_QUEUE_CAPACITY) {
    enter_recovery();
} else {
    data[head & (UART_RX_QUEUE_CAPACITY - 1u)] = byte;
    head++;
}

void USART1_IRQHandler(void) {
    hal_uart_rx_irq(); /* status/data capture only */
}
```

## Review Checklist

- [ ] Shared pure-logic files are byte-identical between `firmware/src` and
      `KEITHLEY_2000_LCD/Core/{Src,Inc}`.
- [ ] ISR contains no parser, trend, display, delay, or blocking operation.
- [ ] Renderer returns to RX draining between bounded steps.
- [ ] Flash remains within 64KB and RAM within 20KB, including heap/stack
      reservations.
