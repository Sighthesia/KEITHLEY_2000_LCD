#!/bin/sh
# Build and run every firmware logic test on the host compiler.
# The source list grows as modules are added; keep it in sync with src/.
set -e
cd "$(dirname "$0")/.."
mkdir -p build

SRCS="src/k2000_proto.c src/host_snapshot.c src/raw_reading_snapshot.c src/ui_model.c src/lt7680_gfx.c src/lt7680_bus.c src/rif_reader.c \
       src/font_digits.c src/font_half.c src/font_text.c src/main_display.c \
       src/panel_transform.c src/reading_split.c src/scene.c \
         src/status_bar.c src/trend_axis.c src/trend_buffer.c src/uart_rx_queue.c src/keypad.c \
        src/render_scheduler.c src/ui_layout.c src/sht3x.c src/spi_timeout.c"

for t in tests/test_*.c; do
    name=$(basename "$t" .c)
    gcc -std=gnu11 -Wall -Wextra -Werror -I src "$t" $SRCS \
        -lm -o "build/$name"
    ./build/"$name"
    echo "PASS $name"
done
