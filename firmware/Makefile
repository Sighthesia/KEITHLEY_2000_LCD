CC = arm-none-eabi-gcc
CFLAGS = -mcpu=cortex-m3 -mthumb -std=gnu11 -Os -Wall -Wextra -Werror \
         -ffreestanding -I src
OBJ = build/hal_stub.o build/lt7680_bus.o build/lt7680_gfx.o \
      build/font_digits.o \
      build/k2000_proto.o build/ui_model.o build/main.o build/syscalls.o
BIN = build/firmware.elf

all: $(BIN)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

clean:
	rm -rf build

.PHONY: all clean
