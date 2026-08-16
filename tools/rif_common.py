#!/usr/bin/env python3
"""Shared host-side definitions for the K2000 resource flash image (RIF).

The RIF is the proposed content layout for the LT7680-attached W25Q128JV
resource flash (U5 on the K2000 display board; connected to the LT7680 SPI,
not to the STM32). It is a self-describing binary image:

    0x0000  64-byte header (magic, version, sizes, CRC32 fields)
    ...     directory of fixed 48-byte entries (one per glyph tile/resource)
    ...     4 KiB-aligned payloads
              - font_digits  RGB565 glyph tiles (35 chars + 3 symbols)
              - font_half    RGB565 glyph tiles (D/C/A)
              - diagnostic RGB565 color tile
              - reserved 4 KiB-aligned regions (future text font, UI assets)

Design rules (see tools/README.md for the full flash map):

* Everything is host-side and deterministic. The packer parses the generated
  C arrays in firmware/src; it never executes firmware and never touches
  hardware.
* 1bpp source bitmaps are row-packed MSB-first (bit set = ink). RGB565 tiles
  keep the same orientation: row 0 = top, MSB = left pixel. Each 16-bit pixel
  is stored little-endian (low byte first), matching the LT7680 MRWDP pixel
  byte order, so firmware can stream tiles straight to display RAM.
* ASCII only in this file: unit symbols are referred to by name (MICRO,
  DEGREE, OHM), never by their UTF-8 glyph.
* Offsets inside the image are file-relative. The intended absolute flash
  address is flash_base + offset; flash_base is recorded in the header.
* CRC32 uses zlib.crc32 (CRC-32/ISO-HDLC, reflected, final XOR) and every
  32-bit field is stored little-endian.
"""

import os
import re
import struct
import zlib

# ---------------------------------------------------------------------------
# File format constants
# ---------------------------------------------------------------------------

MAGIC = b"K2RF"              # "K2RF" = K2000 Resource Flash
VERSION_MAJOR = 1
VERSION_MINOR = 0

HEADER_SIZE = 64
ENTRY_SIZE = 48
ALIGN = 4096                 # W25Q128JV sector size; every payload is aligned
FLASH_SIZE = 16 * 1024 * 1024  # W25Q128JV capacity (128 Mbit)

# Directory entry kinds (4 ASCII chars each)
KIND_DIGIT_CHAR = b"DGTC"    # font_digits ASCII character glyph
KIND_DIGIT_SYM = b"DGTS"     # font_digits unit symbol glyph (MICRO/DEGREE/OHM)
KIND_HALF_CHAR = b"HLFC"     # font_half ASCII character glyph (D/C/A)
KIND_DIAG = b"DIAG"          # diagnostic RGB565 color tile
KIND_RESERVED = b"RSVD"      # reserved region (filled with the fill byte)

# flags byte (entry byte 23)
FLAG_PIXEL_FMT_RGB565_LE = 0x00   # bit0: pixel format (only RGB565 LE defined)
FLAG_ORIGIN_TOP_LEFT = 0x00       # bit1: tile origin (only top-left defined)

# Default palette (design: neon green reading on black)
DEFAULT_FG_RGB = (0x00, 0xFF, 0x33)   # neon green #00FF33
DEFAULT_BG_RGB = (0x00, 0x00, 0x00)   # black
DEFAULT_FILL = 0xFF                   # erased-flash fill for padding/reserved

# Diagnostic tile: 64x64, 8 vertical bands of 8 px. Band 5/6 use the image
# foreground/background so a single tile proves both the fixed palette and
# the configured colors.
DIAG_WIDTH = 64
DIAG_HEIGHT = 64
# (name, rgb565 or None for fg/bg placeholders)
DIAG_BANDS = [
    ("RED", 0xF800),
    ("GREEN", 0x07E0),
    ("BLUE", 0x001F),
    ("WHITE", 0xFFFF),
    ("BLACK", 0x0000),
    ("FG", None),    # image foreground
    ("BG", None),    # image background
    ("CYAN", 0x07FF),
]

# Expected directory content (min, max) per kind. A standalone verification
# treats these as bounds (font_half may be absent); with --src-dir the
# expectation is exact.
EXPECTED_COUNTS = {
    KIND_DIGIT_CHAR: (1, 35),
    KIND_DIGIT_SYM: (0, 3),
    KIND_HALF_CHAR: (0, 3),
    KIND_DIAG: (1, 1),
    KIND_RESERVED: (2, 2),
}
EXPECTED_RSVD_NAMES = ["font_text", "ui_assets"]


def crc32(data):
    """CRC-32/ISO-HDLC as used by zlib; returned as unsigned 32-bit int."""
    return zlib.crc32(data) & 0xFFFFFFFF


def rgb888_to_rgb565(r, g, b):
    return (int(r >> 3) << 11) | (int(g >> 2) << 5) | (int(b >> 3))


def align_up(value, align=ALIGN):
    return (value + align - 1) & ~(align - 1)


# ---------------------------------------------------------------------------
# Parsing the generated C font arrays (same extraction as tools/make_sim_font.py)
# ---------------------------------------------------------------------------


def parse_bitmaps(path):
    """Return list of glyph byte-lists from a C
    `static const uint8_t name[N][M] = { ... };` block."""
    with open(path, "r", encoding="utf-8") as fh:
        src = fh.read()
    arrays = re.findall(r"=\s*\{(.*?)\n\};", src, re.S)
    if not arrays:
        raise ValueError("no bitmap array found in %s" % path)
    glyphs = []
    for body in arrays:
        for glyph in re.finditer(r"\{\s*(.*?)\s*\}", body, re.S):
            bytes_ = [int(x, 16) for x in
                      re.findall(r"0x([0-9A-Fa-f]{2})", glyph.group(1))]
            if bytes_:
                glyphs.append(bytes_)
    return glyphs


def read_header_dim(prefix, header_path):
    """Pull WIDTH/HEIGHT/BYTES_PER_ROW/BASELINE/CHAR_COUNT from a generated .h."""
    with open(header_path, "r", encoding="utf-8") as fh:
        src = fh.read()

    def macro(name):
        m = re.search(r"#define %s_%s (\d+)u" % (prefix, name), src)
        if not m:
            raise ValueError("missing %s_%s in %s" % (prefix, name, header_path))
        return int(m.group(1))

    return (macro("WIDTH"), macro("HEIGHT"), macro("BYTES_PER_ROW"),
            macro("BASELINE"), macro("CHAR_COUNT"))


def parse_font_c(path, var):
    """Parse a generated digit/half font .c into a FontDef namedtuple.

    Returns a FontDef(chars, glyphs, symbols).  glyphs are the character
    bitmaps (bytes); symbols is a list of (name, bytes) for the DMM unit
    symbols (empty when the font has no symbol table).  The extraction is
    byte-for-byte identical to tools/make_sim_font.py.
    """
    with open(path, "r", encoding="utf-8") as fh:
        src = fh.read()
    arrays = re.findall(r"=\s*\{(.*?)\n\};", src, re.S)
    if not arrays:
        raise ValueError("no bitmap array found in %s" % path)
    chars_m = re.search(r's_%s_chars\[\] = "([^"]*)"' % var, src)
    chars = chars_m.group(1) if chars_m else ""

    char_glyphs = [bytes(g) for g in _glyph_bytes(arrays[0])]
    if len(chars) != len(char_glyphs):
        raise ValueError(
            "%s: %d chars but %d glyphs" % (path, len(chars), len(char_glyphs)))

    symbols = []
    if len(arrays) > 1:
        # Second array holds the unit symbols; the generator emits a
        # `/* NAME */` comment before each glyph.
        for name_m, glyph_m in re.findall(
                r"/\*\s*([^*]+?)\s*\*/\s*\{\s*(.*?)\s*\}", arrays[1], re.S):
            name = name_m.strip()
            bytes_ = [int(x, 16) for x in
                      re.findall(r"0x([0-9A-Fa-f]{2})", glyph_m)]
            symbols.append((name, bytes(bytes_)))
    return (chars, char_glyphs, symbols)


def _glyph_bytes(body):
    out = []
    for glyph in re.finditer(r"\{\s*(.*?)\s*\}", body, re.S):
        bytes_ = [int(x, 16) for x in
                  re.findall(r"0x([0-9A-Fa-f]{2})", glyph.group(1))]
        if bytes_:
            out.append(bytes_)
    return out


def load_font(src_dir, name):
    """Load one generated font family by base name (font_digits/font_half).

    Returns (header_dim, chars, glyphs, symbols) or None when the font is
    not present in src_dir.  header_dim is read_header_dim's tuple.
    """
    c_path = os.path.join(src_dir, name + ".c")
    h_path = os.path.join(src_dir, name + ".h")
    if not (os.path.isfile(c_path) and os.path.isfile(h_path)):
        return None
    var = name[len("font_"):]
    if var.endswith("s") and not var.endswith("ss"):
        var = var[:-1]          # font_digits -> digit (mirrors make_digit_font)
    dim = read_header_dim("FONT_" + var.upper(), h_path)
    chars, glyphs, symbols = parse_font_c(c_path, var)
    return (dim, chars, glyphs, symbols)


# ---------------------------------------------------------------------------
# RGB565 tile rendering
# ---------------------------------------------------------------------------


def render_tile(bitmap, width, height, bpr, fg565, bg565):
    """Convert a 1bpp MSB-first row-packed bitmap into RGB565 LE tile bytes.

    Bit set (1) -> fg565, bit clear (0) -> bg565.  Row 0 stays the top row,
    MSB stays the left pixel.  Each pixel is little-endian (low byte first),
    matching the LT7680 MRWDP pixel byte order.
    """
    if fg565 == bg565:
        raise ValueError("foreground and background must differ")
    fg_le = struct.pack("<H", fg565)
    bg_le = struct.pack("<H", bg565)
    out = bytearray()
    for y in range(height):
        row = bitmap[y * bpr:(y + 1) * bpr]
        for x in range(width):
            byte = row[x >> 3]
            bit = (byte >> (7 - (x & 7))) & 1
            out += fg_le if bit else bg_le
    return bytes(out)


def tile_to_1bpp(payload, width, height, stride, fg565, bg565):
    """Reverse of render_tile: RGB565 LE payload back to 1bpp packed bytes.

    Raises ValueError on an unexpected pixel value; every pixel must be
    exactly fg565 or bg565.
    """
    expected = stride * height
    if len(payload) < expected:
        raise ValueError("payload too small: %d < %d" % (len(payload), expected))
    out = bytearray()
    for y in range(height):
        acc = 0
        for x in range(width):
            off = y * stride + x * 2
            px = struct.unpack_from("<H", payload, off)[0]
            if px == fg565:
                bit = 1
            elif px == bg565:
                bit = 0
            else:
                raise ValueError("unexpected pixel 0x%04X at (%d,%d); "
                                 "fg=0x%04X bg=0x%04X"
                                 % (px, x, y, fg565, bg565))
            acc |= bit << (7 - (x & 7))
            if (x & 7) == 7:
                out.append(acc)
                acc = 0
        if width & 7:
            out.append(acc)
    return bytes(out)


def make_diag_tile(fg565, bg565):
    """Build the 64x64 diagnostic tile payload (8 vertical color bands)."""
    bands = []
    for _name, color in DIAG_BANDS:
        bands.append(fg565 if color is None and _name == "FG"
                     else bg565 if color is None and _name == "BG"
                     else color)
    band_w = DIAG_WIDTH // len(DIAG_BANDS)
    out = bytearray()
    for _y in range(DIAG_HEIGHT):
        for band_i, color in enumerate(bands):
            out += struct.pack("<H", color) * band_w
    return bytes(out)


# ---------------------------------------------------------------------------
# Image assembly / parsing
# ---------------------------------------------------------------------------


def build_header(image_size, dir_offset, dir_size, dir_count, payload_start,
                 flash_base, fill_byte):
    """Return the 64-byte header with CRC fields zeroed (caller patches them)."""
    return struct.pack(
        "<4sHHIIIIHHIIIB23s",
        MAGIC,
        VERSION_MAJOR,
        VERSION_MINOR,
        image_size,
        0,                    # header_crc32, patched by finalize_header
        dir_offset,
        dir_size,
        dir_count,
        ENTRY_SIZE,
        payload_start,
        0,                    # image_crc32, patched by finalize_header
        flash_base,
        fill_byte,
        b"\x00" * 23,
    )


def patch_crcs(header, whole_image, header_crc, image_crc):
    """Rewrite the two CRC fields inside a header byte string."""
    h = bytearray(header)
    struct.pack_into("<I", h, 12, header_crc)
    struct.pack_into("<I", h, 32, image_crc)
    return bytes(h)


def finalize_header(header, whole_image):
    """Compute and patch both CRC32 fields.

    header_crc32 covers the 64 header bytes; image_crc32 covers the entire
    file.  In both cases the two CRC fields themselves read as zero during
    the computation (this is the definition the verifier reproduces).
    """
    h = bytearray(header)
    h[12:16] = b"\x00\x00\x00\x00"
    h[32:36] = b"\x00\x00\x00\x00"
    header_crc = crc32(bytes(h))
    image_crc = crc32(bytes(h) + whole_image[len(header):])
    struct.pack_into("<I", h, 12, header_crc)
    struct.pack_into("<I", h, 32, image_crc)
    return bytes(h)


def parse_header(data):
    """Unpack the header; raises ValueError when the magic/version is wrong."""
    if len(data) < HEADER_SIZE:
        raise ValueError("file too small for header")
    (magic, v_major, v_minor, image_size, header_crc, dir_offset, dir_size,
     dir_count, entry_size, payload_start, image_crc, flash_base, fill_byte,
     _res) = struct.unpack("<4sHHIIIIHHIIIB23s", data[:HEADER_SIZE])
    if magic != MAGIC:
        raise ValueError("bad magic %r (expected %r)" % (magic, MAGIC))
    if (v_major, v_minor) != (VERSION_MAJOR, VERSION_MINOR):
        raise ValueError("unsupported version %d.%d" % (v_major, v_minor))
    return {
        "magic": magic,
        "version": (v_major, v_minor),
        "image_size": image_size,
        "header_crc": header_crc,
        "dir_offset": dir_offset,
        "dir_size": dir_size,
        "dir_count": dir_count,
        "entry_size": entry_size,
        "payload_start": payload_start,
        "image_crc": image_crc,
        "flash_base": flash_base,
        "fill_byte": fill_byte,
    }


def build_entry(kind, eid, offset, size, width, height, stride, code, flags,
                payload_crc, fg565, bg565, name):
    """Build one 48-byte directory entry."""
    if len(kind) != 4:
        raise ValueError("entry kind must be 4 ASCII chars")
    name_b = name.encode("ascii")[:16].ljust(16, b"\x00")
    return struct.pack("<4sIIIHHHBBIHH16s", kind, eid, offset, size,
                       width, height, stride, code, flags, payload_crc,
                       fg565, bg565, name_b)


def parse_entry(raw):
    (kind, eid, offset, size, width, height, stride, code, flags,
     payload_crc, fg565, bg565, name) = struct.unpack(
        "<4sIIIHHHBBIHH16s", raw)
    return {
        "kind": kind,
        "id": eid,
        "offset": offset,
        "size": size,
        "width": width,
        "height": height,
        "stride": stride,
        "code": code,
        "flags": flags,
        "crc": payload_crc,
        "fg": fg565,
        "bg": bg565,
        "name": name.split(b"\x00", 1)[0].decode("ascii", "replace"),
    }


def parse_directory(data, header):
    """Unpack all directory entries; validates bounds and entry size."""
    if header["entry_size"] != ENTRY_SIZE:
        raise ValueError("unsupported entry size %d" % header["entry_size"])
    if header["dir_size"] != header["dir_count"] * ENTRY_SIZE:
        raise ValueError("dir_size does not match dir_count * entry_size")
    if header["dir_offset"] + header["dir_size"] > len(data):
        raise ValueError("directory exceeds file")
    raw = data[header["dir_offset"]:header["dir_offset"] + header["dir_size"]]
    entries = [parse_entry(raw[i * ENTRY_SIZE:(i + 1) * ENTRY_SIZE])
               for i in range(header["dir_count"])]
    return entries


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------


def verify_image(data, expect=None):
    """Return a list of problem strings (empty == valid).

    Structural checks always run.  `expect` (from collect_expectations) adds
    exact glyph-count, char-map, payload-size and glyph round-trip checks.
    """
    problems = []

    def fail(msg):
        problems.append(msg)

    if len(data) < HEADER_SIZE:
        return ["file too small for header"]
    try:
        header = parse_header(data)
    except ValueError as exc:
        return ["header: %s" % exc]

    if len(data) != header["image_size"]:
        # Accept a whole-flash dump whose image starts at the recorded base.
        # (Only reachable when the header at file offset 0 parsed, i.e. the
        # dump starts with the image, normally base 0x000000; for a non-zero
        # base verify the programmed-range dump instead.)
        if (len(data) > header["image_size"]
                and header["flash_base"] + header["image_size"] <= len(data)):
            data = data[header["flash_base"]:
                        header["flash_base"] + header["image_size"]]
            try:
                header = parse_header(data)
            except ValueError as exc:
                return ["header: %s" % exc]
        else:
            fail("image_size %d does not match file size %d"
                 % (header["image_size"], len(data)))

    if header["flash_base"] + header["image_size"] > FLASH_SIZE:
        fail("image [0x%X, 0x%X) exceeds flash capacity 0x%X"
             % (header["flash_base"],
                header["flash_base"] + header["image_size"], FLASH_SIZE))

    # Header CRC: zero both CRC fields, CRC the header bytes.
    h = bytearray(data[:HEADER_SIZE])
    h[12:16] = b"\x00\x00\x00\x00"
    h[32:36] = b"\x00\x00\x00\x00"
    if crc32(bytes(h)) != header["header_crc"]:
        fail("header_crc32 mismatch")
    # Image CRC: zero both CRC fields, CRC the whole file.
    whole = bytearray(data)
    whole[12:16] = b"\x00\x00\x00\x00"
    whole[32:36] = b"\x00\x00\x00\x00"
    if crc32(bytes(whole)) != header["image_crc"]:
        fail("image_crc32 mismatch")

    if header["flash_base"] % ALIGN != 0:
        fail("flash_base 0x%08X is not 4 KiB aligned" % header["flash_base"])
    if header["payload_start"] % ALIGN != 0:
        fail("payload_start 0x%08X is not 4 KiB aligned"
             % header["payload_start"])

    try:
        entries = parse_directory(data, header)
    except ValueError as exc:
        return problems + ["directory: %s" % exc]

    ranges = []
    counts = {}
    for i, ent in enumerate(entries):
        counts[ent["kind"]] = counts.get(ent["kind"], 0) + 1
        if ent["kind"] not in (KIND_DIGIT_CHAR, KIND_DIGIT_SYM,
                               KIND_HALF_CHAR, KIND_DIAG, KIND_RESERVED):
            fail("entry %d: unknown kind %r" % (i, ent["kind"]))
        if ent["offset"] % ALIGN != 0:
            fail("entry %d (%s %s): offset 0x%08X not 4 KiB aligned"
                 % (i, ent["kind"].decode(), ent["name"], ent["offset"]))
        if ent["offset"] < header["payload_start"]:
            fail("entry %d (%s %s): payload overlaps metadata"
                 % (i, ent["kind"].decode(), ent["name"]))
        end = ent["offset"] + ent["size"]
        if end > len(data):
            fail("entry %d (%s %s): payload exceeds file"
                 % (i, ent["kind"].decode(), ent["name"]))
        ranges.append((ent["offset"], end, i))

        if ent["kind"] == KIND_RESERVED:
            if ent["size"] % ALIGN != 0:
                fail("entry %d (%s): reserved size %d not 4 KiB aligned"
                     % (i, ent["name"], ent["size"]))
            payload = data[ent["offset"]:end]
            if payload and any(b != header["fill_byte"] for b in payload):
                fail("entry %d (%s): reserved region not filled with 0x%02X"
                     % (i, ent["name"], header["fill_byte"]))
            continue

        # Non-reserved payload: geometry self-consistency + CRC.
        if ent["width"] == 0 or ent["height"] == 0:
            fail("entry %d (%s %s): zero tile geometry"
                 % (i, ent["kind"].decode(), ent["name"]))
        expected = ent["stride"] * ent["height"]
        if ent["size"] != expected:
            fail("entry %d (%s %s): size %d != stride*height %d"
                 % (i, ent["kind"].decode(), ent["name"],
                    ent["size"], expected))
        if ent["stride"] != ent["width"] * 2:
            fail("entry %d (%s %s): stride %d != width*2 %d (RGB565)"
                 % (i, ent["kind"].decode(), ent["name"],
                    ent["stride"], ent["width"] * 2))
        payload = data[ent["offset"]:end]
        if crc32(payload) != ent["crc"]:
            fail("entry %d (%s %s): payload CRC mismatch"
                 % (i, ent["kind"].decode(), ent["name"]))

    ranges.sort()
    for (a0, b0, i0), (a1, b1, i1) in zip(ranges, ranges[1:]):
        if b0 > a1:
            fail("entry %d and %d payloads overlap" % (i0, i1))

    for kind, (lo, hi) in EXPECTED_COUNTS.items():
        n = counts.get(kind, 0)
        if n < lo or n > hi:
            fail("expected %d..%d entries of kind %r, found %d"
                 % (lo, hi, kind, n))

    rsvd_by_id = {e["id"]: e["name"] for e in entries
                  if e["kind"] == KIND_RESERVED}
    for i, name in enumerate(EXPECTED_RSVD_NAMES):
        if rsvd_by_id.get(i) != name:
            fail("reserved region %d: expected name %r, got %r"
                 % (i, name, rsvd_by_id.get(i)))

    if expect:
        problems += _verify_expectations(data, header, entries, expect)
    return problems


def _verify_expectations(data, header, entries, expect):
    """Cross-check the image against parsed C font sources."""
    problems = []
    by_kind = {}
    for ent in entries:
        by_kind.setdefault(ent["kind"], []).append(ent)
    by_kind = {k: sorted(v, key=lambda e: e["id"]) for k, v in by_kind.items()}

    if expect.get("digit"):
        dim, chars, glyphs, symbols = expect["digit"]
        dg = by_kind.get(KIND_DIGIT_CHAR, [])
        if len(dg) != len(chars):
            problems.append("digit char count: image %d != source %d"
                            % (len(dg), len(chars)))
        for ent in dg:
            if ent["id"] >= len(chars):
                problems.append("digit char id %d out of range" % ent["id"])
                continue
            code = ord(chars[ent["id"]])
            if ent["code"] != code:
                problems.append("digit char %d code 0x%02X != source 0x%02X"
                                % (ent["id"], ent["code"], code))
            if ent["size"] != dim[0] * dim[1] * 2:
                problems.append("digit char %d size %d != bpg*2 %d"
                                % (ent["id"], ent["size"], dim[0] * dim[1] * 2))
            payload = data[ent["offset"]:ent["offset"] + ent["size"]]
            src = glyphs[ent["id"]]
            try:
                back = tile_to_1bpp(payload, dim[0], dim[1], ent["stride"],
                                    ent["fg"], ent["bg"])
                if back != src:
                    problems.append("digit char %d round-trip mismatch"
                                    % ent["id"])
            except ValueError as exc:
                problems.append("digit char %d: %s" % (ent["id"], exc))

        ds = by_kind.get(KIND_DIGIT_SYM, [])
        if len(ds) != len(symbols):
            problems.append("digit symbol count: image %d != source %d"
                            % (len(ds), len(symbols)))
        for ent in ds:
            if ent["id"] >= len(symbols):
                problems.append("digit symbol id %d out of range" % ent["id"])
                continue
            if ent["size"] != dim[0] * dim[1] * 2:
                problems.append("digit symbol %d size %d != bpg*2 %d"
                                % (ent["id"], ent["size"], dim[0] * dim[1] * 2))
            payload = data[ent["offset"]:ent["offset"] + ent["size"]]
            src = symbols[ent["id"]][1]
            try:
                back = tile_to_1bpp(payload, dim[0], dim[1], ent["stride"],
                                    ent["fg"], ent["bg"])
                if back != src:
                    problems.append("digit symbol %d round-trip mismatch"
                                    % ent["id"])
            except ValueError as exc:
                problems.append("digit symbol %d: %s" % (ent["id"], exc))

    if expect.get("half"):
        dim, chars, glyphs, _symbols = expect["half"]
        hg = by_kind.get(KIND_HALF_CHAR, [])
        if len(hg) != len(chars):
            problems.append("half char count: image %d != source %d"
                            % (len(hg), len(chars)))
        for ent in hg:
            if ent["id"] >= len(chars):
                problems.append("half char id %d out of range" % ent["id"])
                continue
            code = ord(chars[ent["id"]])
            if ent["code"] != code:
                problems.append("half char %d code 0x%02X != source 0x%02X"
                                % (ent["id"], ent["code"], code))
            if ent["size"] != dim[0] * dim[1] * 2:
                problems.append("half char %d size %d != bpg*2 %d"
                                % (ent["id"], ent["size"], dim[0] * dim[1] * 2))
            payload = data[ent["offset"]:ent["offset"] + ent["size"]]
            src = glyphs[ent["id"]]
            try:
                back = tile_to_1bpp(payload, dim[0], dim[1], ent["stride"],
                                    ent["fg"], ent["bg"])
                if back != src:
                    problems.append("half char %d round-trip mismatch"
                                    % ent["id"])
            except ValueError as exc:
                problems.append("half char %d: %s" % (ent["id"], exc))

    if expect.get("diag"):
        dg_entries = by_kind.get(KIND_DIAG, [])
        if len(dg_entries) != 1:
            problems.append("expected exactly 1 diagnostic tile")
        else:
            ent = dg_entries[0]
            band_w = DIAG_WIDTH // len(DIAG_BANDS)
            for i, (name, color) in enumerate(DIAG_BANDS):
                if color is None:
                    color = (ent["fg"] if name == "FG" else ent["bg"])
                off = ent["offset"] + (DIAG_HEIGHT // 2) * ent["stride"] \
                    + i * band_w * 2
                px = struct.unpack_from("<H", data, off)[0]
                if px != color:
                    problems.append("diag band %s: pixel 0x%04X != 0x%04X"
                                    % (name, px, color))

    return problems


def collect_expectations(src_dir):
    """Parse firmware/src and return the expected-content dictionary used by
    verify_image, or None when no font sources are present."""
    expect = {}
    digit = load_font(src_dir, "font_digits")
    if digit:
        expect["digit"] = digit
    half = load_font(src_dir, "font_half")
    if half:
        expect["half"] = half
    expect["diag"] = True
    return expect or None
