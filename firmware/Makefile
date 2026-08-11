CC = arm-none-eabi-gcc
CFLAGS = -mcpu=cortex-m3 -mthumb -std=gnu11 -Os -Wall -Wextra -Werror \
         -ffreestanding -I src
OBJ = build/hal_stub.o build/lt7680_bus.o build/lt7680_gfx.o \
      build/font_digits.o build/font_text.o build/keypad.o build/k2000_proto.o \
      build/main.o build/main_display.o build/panel_transform.o \
      build/reading_split.o build/scene.o build/status_bar.o \
      build/syscalls.o build/ui_model.o
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
