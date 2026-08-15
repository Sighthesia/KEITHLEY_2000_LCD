#include <string.h>

#include "uart_rx_queue.h"

#define UART_RX_QUEUE_MASK (UART_RX_QUEUE_CAPACITY - 1u)

void uart_rx_queue_init(uart_rx_queue_t *queue)
{
    if (queue != 0) memset(queue, 0, sizeof(*queue));
}

void uart_rx_queue_push_isr(uart_rx_queue_t *queue, uint8_t byte)
{
    uint16_t next;
    if (queue == 0) return;
    if (queue->recovering) {
        if (byte != 0x0Du) return;
        /* Drop the pre-overflow payload and publish only the next message
         * delimiter. Clear recovering last so the consumer cannot observe
         * partially-reset indices. */
        queue->head = queue->tail;
        queue->data[queue->head & UART_RX_QUEUE_MASK] = byte;
        queue->head++;
        queue->recovering = false;
        return;
    }
    next = (uint16_t)(queue->head + 1u);
    if ((uint16_t)(queue->head - queue->tail) >= UART_RX_QUEUE_CAPACITY) {
        queue->overflow_count++;
        queue->recovering = true;
        return;
    }
    queue->data[queue->head & UART_RX_QUEUE_MASK] = byte;
    queue->head = next;
}

int uart_rx_queue_pop(uart_rx_queue_t *queue)
{
    uint16_t tail;
    int value;
    if (queue == 0 || queue->recovering || queue->tail == queue->head) return -1;
    tail = queue->tail;
    value = queue->data[tail & UART_RX_QUEUE_MASK];
    queue->tail = (uint16_t)(tail + 1u);
    return value;
}

uint32_t uart_rx_queue_overflow_count(const uart_rx_queue_t *queue)
{
    return queue != 0 ? queue->overflow_count : 0u;
}

bool uart_rx_queue_recovering(const uart_rx_queue_t *queue)
{
    return queue != 0 && queue->recovering;
}
