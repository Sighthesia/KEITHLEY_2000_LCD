#!/bin/sh
# Build and run every firmware logic test on the host compiler.
# The source list grows as modules are added; keep it in sync with src/.
set -e
cd "$(dirname "$0")/.."
mkdir -p build

SRCS="src/k2000_proto.c src/ui_model.c src/lt7680_gfx.c src/lt7680_bus.c src/font_digits.c"

for t in tests/test_*.c; do
    name=$(basename "$t" .c)
    gcc -std=gnu11 -Wall -Wextra -Werror -I src "$t" $SRCS \
        -lm -o "build/$name"
    ./build/"$name"
    echo "PASS $name"
done
