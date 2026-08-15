#include <assert.h>

#include "uart_rx_queue.h"

int main(void)
{
    uart_rx_queue_t queue;
    uint16_t i;
    uart_rx_queue_init(&queue);
    uart_rx_queue_push_isr(&queue, 1u);
    uart_rx_queue_push_isr(&queue, 2u);
    assert(uart_rx_queue_pop(&queue) == 1);
    assert(uart_rx_queue_pop(&queue) == 2);
    assert(uart_rx_queue_pop(&queue) == -1);
    for (i = 0u; i < UART_RX_QUEUE_CAPACITY; i++) uart_rx_queue_push_isr(&queue, (uint8_t)i);
    assert(uart_rx_queue_overflow_count(&queue) == 0u);
    uart_rx_queue_push_isr(&queue, 0xAAu);
    assert(uart_rx_queue_overflow_count(&queue) == 1u);
    assert(uart_rx_queue_recovering(&queue));
    assert(uart_rx_queue_pop(&queue) == -1);
    uart_rx_queue_push_isr(&queue, 0x55u);
    assert(uart_rx_queue_pop(&queue) == -1);
    uart_rx_queue_push_isr(&queue, 0x0Du);
    assert(!uart_rx_queue_recovering(&queue));
    assert(uart_rx_queue_pop(&queue) == 0x0D);
    assert(uart_rx_queue_pop(&queue) == -1);
    uart_rx_queue_push_isr(&queue, 0x22u);
    assert(uart_rx_queue_pop(&queue) == 0x22);
    return 0;
}
