#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UART_RX_QUEUE_CAPACITY 512u

typedef struct {
    uint8_t data[UART_RX_QUEUE_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t overflow_count;
    volatile bool recovering;
} uart_rx_queue_t;

void uart_rx_queue_init(uart_rx_queue_t *queue);
void uart_rx_queue_push_isr(uart_rx_queue_t *queue, uint8_t byte);
int uart_rx_queue_pop(uart_rx_queue_t *queue);
uint32_t uart_rx_queue_overflow_count(const uart_rx_queue_t *queue);
bool uart_rx_queue_recovering(const uart_rx_queue_t *queue);
