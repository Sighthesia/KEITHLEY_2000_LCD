#!/usr/bin/env python3
"""Pack the K2000 resource flash image (RIF) from the generated font sources.

Reads the archived big-digit source and active half-font C arrays and emits
one self-describing binary image for the LT7680-attached W25Q64JV resource
flash (U5). The image contains:

  * a 64-byte header (magic "K2RF", version, sizes, CRC32 fields,
    intended flash base, fill byte),
  * a directory of 48-byte entries (one per glyph tile, plus the
    diagnostic tile and reserved regions),
  * 4 KiB-aligned RGB565 glyph tiles (little-endian pixels, MSB-first
    source bit order preserved, bit set = foreground),
  * a 64x64 diagnostic color tile,
  * reserved 4 KiB-aligned regions for a future text font and UI assets.

This tool only reads generated sources and writes a file. It does not
execute firmware and does not touch hardware; see tools/README.md for the
safety/verification workflow before anything is programmed into flash.

Usage:
    python3 tools/pack_resource_flash.py [options]

Options:
    --src-dir DIR      directory with the active half-font .c/.h (default
                        firmware/src)
    --digit-src-dir DIR directory with archived font_digits .c/.h (default
                        tools/font_source)
    --output PATH      output image path (default
                       build/resource/k2000_resource_v{MAJOR}_{base}.img)
    --base-offset ADDR intended absolute flash address (hex, e.g. 0x000000;
                       default 0x000000)
    --fg COLOR         foreground color as #RRGGBB (default #00FF33)
    --bg COLOR         background color as #RRGGBB (default #000000)
    --fill BYTE        fill byte for padding and reserved regions, hex
                       (default 0xFF, the flash erased state)
    --no-verify        do not run the structural verification at the end
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rif_common
from rif_common import (ALIGN, DEFAULT_BG_RGB, DEFAULT_FG_RGB, DEFAULT_FILL,
                        DIAG_HEIGHT, DIAG_WIDTH, ENTRY_SIZE,
                         FLAG_ORIGIN_TOP_LEFT, FLAG_PIXEL_FMT_RGB565_LE,
                         FLASH_SIZE, HEADER_SIZE, KIND_DIAG, KIND_DIGIT_CHAR,
                         KIND_DIGIT_SYM, KIND_HALF_CHAR, KIND_TEXT_CHAR,
                         KIND_TEXT_SYM, KIND_RESERVED,
                        VERSION_MAJOR, align_up, build_entry, build_header,
                        crc32, load_font, make_diag_tile, render_tile,
                        rgb888_to_rgb565, verify_image)

DEFAULT_SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "firmware", "src")
DEFAULT_DIGIT_SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "font_source")

# Reserved regions: (name, byte size). Both are 4 KiB aligned.
RSVD_REGIONS = [
    ("ui_assets", 0x10000),   # future UI icons / layout assets
]


def parse_color(text):
    """Accept #RRGGBB (or RRGGBB) and return RGB565."""
    s = text.lstrip("#")
    if len(s) != 6:
        raise argparse.ArgumentTypeError(
            "color must be #RRGGBB, got %r" % text)
    r, g, b = (int(s[i:i + 2], 16) for i in (0, 2, 4))
    return rgb888_to_rgb565(r, g, b)


def parse_addr(text):
    return int(text, 16)


def parse_byte(text):
    v = int(text, 16)
    if not 0 <= v <= 0xFF:
        raise argparse.ArgumentTypeError("byte out of range: %r" % text)
    return v


def count_entries(digit, half, text=None):
    n = (len(digit[1]) + len(digit[3])) if digit else 0   # chars + symbols
    if half:
        n += len(half[1])
    if text:
        n += len(text[1]) + len(text[3])
    return n + 1 + len(RSVD_REGIONS)   # 1 diagnostic + reserved regions


def build_payloads(digit, half, fg565, bg565, fill_byte,
                   payload_start, text=None,
                   transpose=False):
    """Return (entries, chunks, total_payload_bytes). Chunk i starts at
    payload_start + cumulative 4 KiB-aligned offsets."""
    entries = []
    chunks = []
    running = 0

    def add(kind, eid, payload, width, height, code, fg, bg, name):
        nonlocal running
        slot = align_up(len(payload), ALIGN)
        entries.append(build_entry(
            kind, eid, payload_start + running, len(payload), width, height,
            width * 2, code, FLAG_PIXEL_FMT_RGB565_LE | FLAG_ORIGIN_TOP_LEFT,
            crc32(payload), fg, bg, name))
        chunks.append((payload_start + running, payload))
        running += slot

    if digit is None:
        raise SystemExit("font_digits not found; run tools/make_digit_font.py "
                         "first")
    dim, chars, glyphs, symbols = digit
    width, height, bpr = dim[0], dim[1], dim[2]
    for i, (ch, glyph) in enumerate(zip(chars, glyphs)):
        tile = render_tile(glyph, width, height, bpr, fg565, bg565)
        ew, eh = width, height
        if transpose:
            # Pure transpose (fb_x=ui_y, fb_y=ui_x): the framebuffer-
            # oriented tile lets one block DMA draw the glyph directly.
            tile = bytes(tile)  # index [ui_x][ui_y] below
            out = bytearray(len(tile))
            for uy in range(height):
                for ux in range(width):
                    src = (uy * width + ux) * 2
                    dst = (ux * height + uy) * 2
                    out[dst:dst + 2] = tile[src:src + 2]
            tile = bytes(out)
            ew, eh = height, width
        add(KIND_DIGIT_CHAR, i, tile, ew, eh, ord(ch), fg565, bg565,
            "digit_chars")
    for i, (_name, glyph) in enumerate(symbols):
        tile = render_tile(glyph, width, height, bpr, fg565, bg565)
        ew, eh = width, height
        if transpose:
            out = bytearray(len(tile))
            for uy in range(height):
                for ux in range(width):
                    src = (uy * width + ux) * 2
                    dst = (ux * height + uy) * 2
                    out[dst:dst + 2] = tile[src:src + 2]
            tile = bytes(out)
            ew, eh = height, width
        add(KIND_DIGIT_SYM, i, tile, ew, eh, i, fg565, bg565,
            "digit_syms")

    if half is not None:
        dim, chars, glyphs, _symbols = half
        width, height, bpr = dim[0], dim[1], dim[2]
        for i, (ch, glyph) in enumerate(zip(chars, glyphs)):
            tile = render_tile(glyph, width, height, bpr, fg565, bg565)
            add(KIND_HALF_CHAR, i, tile, width, height, ord(ch), fg565,
                bg565, "half_chars")

    if text is not None:
        dim, chars, glyphs, symbols = text
        width, height, bpr = dim[0], dim[1], dim[2]
        for i, (ch, glyph) in enumerate(zip(chars, glyphs)):
            tile = render_tile(glyph, width, height, bpr, fg565, bg565)
            ew, eh = width, height
            if transpose:
                out = bytearray(len(tile))
                for uy in range(height):
                    for ux in range(width):
                        src = (uy * width + ux) * 2
                        dst = (ux * height + uy) * 2
                        out[dst:dst + 2] = tile[src:src + 2]
                tile = bytes(out)
                ew, eh = height, width
            add(KIND_TEXT_CHAR, i, tile, ew, eh, ord(ch), fg565, bg565,
                "text_chars")
        for i, (_name, glyph) in enumerate(symbols):
            tile = render_tile(glyph, width, height, bpr, fg565, bg565)
            ew, eh = width, height
            if transpose:
                out = bytearray(len(tile))
                for uy in range(height):
                    for ux in range(width):
                        src = (uy * width + ux) * 2
                        dst = (ux * height + uy) * 2
                        out[dst:dst + 2] = tile[src:src + 2]
                tile = bytes(out)
                ew, eh = height, width
            add(KIND_TEXT_SYM, i, tile, ew, eh, i, fg565, bg565,
                "text_symbols")

    diag = make_diag_tile(fg565, bg565)
    add(KIND_DIAG, 0, diag, DIAG_WIDTH, DIAG_HEIGHT, 0, fg565, bg565,
        "diag_tile")

    for i, (name, size) in enumerate(RSVD_REGIONS):
        add(KIND_RESERVED, i, bytes([fill_byte]) * size, 0, 0, 0, 0, 0, name)

    return entries, chunks, running


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Pack the K2000 resource flash image from generated fonts")
    parser.add_argument("--src-dir", default=DEFAULT_SRC_DIR)
    parser.add_argument("--digit-src-dir", default=DEFAULT_DIGIT_SRC_DIR)
    parser.add_argument("--output")
    parser.add_argument("--base-offset", type=parse_addr, default=0x000000,
                        help="intended absolute flash address (hex)")
    parser.add_argument("--fg", type=parse_color,
                        default=parse_color("#%02X%02X%02X" % DEFAULT_FG_RGB))
    parser.add_argument("--bg", type=parse_color,
                        default=parse_color("#%02X%02X%02X" % DEFAULT_BG_RGB))
    parser.add_argument("--fill", type=parse_byte, default=DEFAULT_FILL)
    parser.add_argument("--transpose", action="store_true",
                        help="emit large digit tiles pre-transposed to "
                             "framebuffer orientation (fb_x=ui_y) so a "
                             "single block DMA flash->canvas can draw them")
    parser.add_argument("--no-verify", action="store_true")
    args = parser.parse_args(argv)

    if args.fg == args.bg:
        raise SystemExit("foreground and background must differ")
    if args.base_offset % ALIGN != 0:
        raise SystemExit("--base-offset must be 4 KiB aligned")

    src_dir = os.path.abspath(args.src_dir)
    digit_src_dir = os.path.abspath(args.digit_src_dir)
    digit = load_font(digit_src_dir, "font_digits")
    half = load_font(src_dir, "font_half")
    text = load_font(src_dir, "font_text")

    # Layout: header | directory | pad to 4 KiB | 4 KiB-aligned payload slots.
    dir_size = count_entries(digit, half, text) * ENTRY_SIZE
    payload_start = align_up(HEADER_SIZE + dir_size, ALIGN)

    entries, chunks, payload_bytes = build_payloads(
        digit, half, args.fg, args.bg, args.fill, payload_start, text=text,
        transpose=args.transpose)
    image_size = payload_start + payload_bytes
    if args.base_offset + image_size > FLASH_SIZE:
        raise SystemExit(
            "image [0x%X, 0x%X) exceeds W25Q64JV capacity 0x%X; "
            "choose a smaller --base-offset"
            % (args.base_offset, args.base_offset + image_size, FLASH_SIZE))

    image = bytearray(image_size)
    image[0:HEADER_SIZE] = bytes([args.fill]) * HEADER_SIZE
    image[HEADER_SIZE:HEADER_SIZE + dir_size] = b"".join(entries)
    image[HEADER_SIZE + dir_size:payload_start] = bytes([args.fill]) * (
        payload_start - HEADER_SIZE - dir_size)
    for off, chunk in chunks:
        image[off:off + len(chunk)] = chunk

    header = build_header(image_size, HEADER_SIZE, dir_size, len(entries),
                          payload_start, args.base_offset, args.fill)
    # Place the header (CRC fields zero) then compute both CRC32 values over
    # their ranges with both fields zeroed, and patch them into the image.
    image[0:HEADER_SIZE] = header
    header_crc = crc32(bytes(header))
    image_crc = crc32(bytes(image))
    struct.pack_into("<I", image, 12, header_crc)
    struct.pack_into("<I", image, 32, image_crc)

    if not args.no_verify:
        problems = verify_image(bytes(image))
        if problems:
            for p in problems:
                print("VERIFY FAIL: %s" % p, file=sys.stderr)
            raise SystemExit("packed image failed verification")

    if not args.output:
        out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "..", "build", "resource")
        os.makedirs(out_dir, exist_ok=True)
        args.output = os.path.join(
            out_dir, "k2000_resource_v%d_%s.img"
            % (VERSION_MAJOR, "0x%06X" % args.base_offset))
    with open(args.output, "wb") as fh:
        fh.write(image)

    _print_map(args.output, image, entries, args.base_offset, args.fg, args.bg)
    print("packed %s (%d bytes, %d entries, base 0x%06X)"
          % (args.output, len(image), len(entries), args.base_offset))
    if digit:
        print("  font_digits: %d chars + %d symbols (%dx%d, %d B/tile)"
               % (len(digit[1]), len(digit[3]), digit[0][0], digit[0][1],
                  digit[0][0] * digit[0][1] * 2))
    if half:
        print("  font_half  : %d chars (%dx%d, %d B/tile)"
               % (len(half[1]), half[0][0], half[0][1],
                  half[0][0] * half[0][1] * 2))
    return 0


def _print_map(path, image, entries, base, fg565, bg565):
    kind_names = {KIND_DIGIT_CHAR: "digit char",
                  KIND_DIGIT_SYM: "digit symbol",
                  KIND_HALF_CHAR: "half char",
                  KIND_TEXT_CHAR: "text char",
                  KIND_TEXT_SYM: "text symbol",
                  KIND_DIAG: "diagnostic",
                  KIND_RESERVED: "reserved"}
    print("")
    print("flash map (absolute address = base 0x%06X + file offset; "
          "image size 0x%X bytes)" % (base, len(image)))
    print("  %-12s %-12s %-9s %-14s %s" % ("flash addr", "file off",
                                           "size", "kind", "name/code"))
    for raw in entries:
        ent = rif_common.parse_entry(raw)
        code = ""
        if ent["kind"] in (KIND_DIGIT_CHAR, KIND_HALF_CHAR) and ent["code"]:
            code = " '%s'" % chr(ent["code"])
        elif ent["kind"] == KIND_DIGIT_SYM:
            code = " sym%d" % ent["id"]
        print("  0x%06X     0x%06X   0x%05X   %-14s %s%s"
              % (base + ent["offset"], ent["offset"], ent["size"],
                 kind_names.get(ent["kind"], ent["kind"].decode()),
                 ent["name"], code))
    print("  palette: fg=0x%04X bg=0x%04X (RGB565 little-endian)"
          % (fg565, bg565))


if __name__ == "__main__":
    main()
