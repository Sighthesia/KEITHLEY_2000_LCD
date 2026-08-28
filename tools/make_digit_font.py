#!/usr/bin/env python3
"""Rasterize a TTF into 1bpp C arrays for the K2000 big-digit font.

Usage:
    python3 tools/make_digit_font.py TTF out_dir [name]
        [--width W] [--height H] [--charset S] [--symbols "µ,°,Ω"]

Defaults: name=font_digits, 64x128 px cells, 1bpp, row-packed MSB first
(8 bytes/row = 1024 bytes/glyph), the ASCII charset used by readings and
unit suffixes, plus the DMM unit symbols (µ ° Ω) emitted as a separate
symbol bitmap array exposed through <fn>_symbol_bitmap().

Generate the half-height DC/AC font by re-running with a second name:

    python3 tools/make_digit_font.py TTF out_dir font_half \
        --width 32 --height 64 --charset "DCA"

JetBrains Mono's advance width is 0.6 em, so at size 107 a digit is ~64 px
wide and fills the 64 px cell. The script auto-shrinks the font until every
glyph's ink box fits the cell, then draws all glyphs on one shared baseline
(short glyphs like '.' and '-' sit low, near the baseline, as in normal
typography) and horizontally centres each ink box.

Re-run after any change to CHARSET, W, H, symbols or the source font; the
generated .c/.h must not be hand-edited.

Requires Pillow:  python3 -m pip install pillow
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_CHARSET = "0123456789.+-Ee%mukKMWVOhDAC?RFLHzs"
# DMM unit symbols appended after the ASCII charset, exposed via
# <fn>_symbol_bitmap() (mirrors font_text's symbol table).
SYMBOLS = [("\u00B5", "MICRO"), ("\u00B0", "DEGREE"), ("\u03A9", "OHM")]

_SYMBOL_LABELS = {"\u00B5": "MICRO", "\u00B0": "DEGREE", "\u03A9": "OHM"}


def parse_args(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    ttf, out_dir = argv[1], argv[2]
    name = "font_digits"
    w, h = 64, 128
    charset = DEFAULT_CHARSET
    symbols = list(SYMBOLS)
    i = 3
    while i < len(argv):
        arg = argv[i]
        if arg == "--width" and i + 1 < len(argv):
            w = int(argv[i + 1]); i += 2
        elif arg == "--height" and i + 1 < len(argv):
            h = int(argv[i + 1]); i += 2
        elif arg == "--charset" and i + 1 < len(argv):
            charset = argv[i + 1]; i += 2
        elif arg == "--symbols" and i + 1 < len(argv):
            raw = argv[i + 1].strip()
            symbols = []
            if raw:
                for s in raw.split(","):
                    symbols.append((s, _SYMBOL_LABELS.get(s, "GLYPH_%04X" % ord(s))))
            i += 2
        elif arg.startswith("-"):
            raise SystemExit("unknown option: %s" % arg)
        else:
            name = arg; i += 1
    return ttf, out_dir, name, w, h, charset, symbols


def var_name(name):
    """font_digits -> digits ; font_half -> half (prefix suffix)."""
    var = name[len("font_"):]
    if var.endswith("s") and not var.endswith("ss"):
        var = var[:-1]
    return var


def prefix_name(name):
    return "FONT_" + var_name(name).upper()


def pick_font(ttf, w, h, charset):
    """Largest font size whose every glyph ink box fits the cell."""
    for size in range(h, 7, -1):
        font = ImageFont.truetype(ttf, size=size)
        if all(fits(font, w, h, ch) for ch in charset):
            return font
    raise SystemExit("no font size fits the cell for charset %r" % charset)


def fits(font, w, h, ch):
    left, top, right, bottom = font.getbbox(ch, anchor="ls")
    return (right - left) <= w and (bottom - top) <= h


def make_glyphs(font, w, h, charset):
    """Return (glyph list of (char, ink_box, PIL image), baseline_y).

    All glyphs share one baseline; the union of all ink boxes is vertically
    centred in the cell, so '.' and '-' sit low near the baseline.
    """
    ink = {ch: font.getbbox(ch, anchor="ls") for ch in charset}
    min_top = min(b[1] for b in ink.values())
    max_bottom = max(b[3] for b in ink.values())
    baseline_y = (h - (max_bottom - min_top)) // 2 - min_top

    glyphs = []
    for ch in charset:
        left, top, right, bottom = ink[ch]
        width = right - left
        img = Image.new("1", (w, h), 0)
        d = ImageDraw.Draw(img)
        anchor_x = (w - width) // 2 - left
        d.text((anchor_x, baseline_y), ch, font=font, fill=1, anchor="ls")
        glyphs.append((ch, (left, top, right, bottom), img))
    return glyphs, baseline_y


def render_symbol(font, w, h, baseline_y, sym):
    left, top, right, bottom = font.getbbox(sym, anchor="ls")
    img = Image.new("1", (w, h), 0)
    d = ImageDraw.Draw(img)
    anchor_x = (w - (right - left)) // 2 - left
    d.text((anchor_x, baseline_y), sym, font=font, fill=1, anchor="ls")
    return img


def pack(img, w, h):
    """MSB-first packed rows; mode "1" tobytes() already packs 8 px/byte."""
    data = img.tobytes()
    expected = ((w + 7) // 8) * h
    if len(data) != expected:
        raise SystemExit("packing error: %d bytes, expected %d"
                         % (len(data), expected))
    return list(data)


def array_lines(data, bpg):
    lines = ["    {"]
    for row_start in range(0, bpg, 16):
        chunk = data[row_start:row_start + 16]
        lines.append("        " + ",".join("0x%02X" % b for b in chunk) + ",")
    lines.append("    },")
    return lines


def emit_c(out_dir, name, w, h, charset, glyphs, symbols, font, baseline_y):
    prefix = prefix_name(name)
    var = var_name(name)
    fn = "font_" + var
    count = len(charset)
    bpr = (w + 7) // 8
    bpg = bpr * h
    sym_count = len(symbols)

    header = os.path.join(out_dir, name + ".h")
    source = os.path.join(out_dir, name + ".c")

    sym_defs = "\n".join(
        "#define %s_SYM_%s %du" % (prefix, label, i)
        for i, (_s, label) in enumerate(symbols))
    if sym_count:
        sym_defs += "\n#define %s_SYM_COUNT %du" % (prefix, sym_count)
    sym_proto = (
        "\n/* Bitmap for one of the %s_SYM_* symbols, or NULL for an unknown\n"
        " * symbol id. Data lives in Flash; the caller does not own it. */\n"
        "const uint8_t *%s_symbol_bitmap(uint8_t sym);\n"
        % (prefix, fn)) if sym_count else ""
    rif_proto = ""
    if var == "digit":
        rif_proto = (
            "\nbool font_digit_rif_code(char c, uint32_t *kind, "
            "uint16_t *code);\n"
            "bool font_digit_rif_symbol_code(uint8_t symbol, "
            "uint32_t *kind, uint16_t *code);\n")

    with open(header, "w") as f:
        f.write(
            "#pragma once\n"
            "\n"
            "#include <stdint.h>\n"
            "%s"
            "\n"
            "/* Generated by tools/make_digit_font.py - DO NOT EDIT.\n"
            " * JetBrains Mono big-digit font: %dx%d px cells, 1bpp, row-packed\n"
            " * MSB first (%d bytes/row, %d bytes per glyph).\n"
            " * CHARSET = \"%s\".\n"
            " */\n"
            "\n"
            "#define %s_WIDTH %du\n"
            "#define %s_HEIGHT %du\n"
            "#define %s_BYTES_PER_ROW %du\n"
            "#define %s_BYTES_PER_GLYPH %du\n"
            "#define %s_CHAR_COUNT %du\n"
            "#define %s_BASELINE %du\n"
            "%s\n"
            "\n"
            "/* Bitmap for character c (1bpp, MSB first), or NULL if c is not\n"
            " * in the charset. The data lives in Flash; the caller does not\n"
            " * own it. */\n"
            "const uint8_t *%s_bitmap(char c);\n"
            "%s%s\n"
            "uint16_t %s_width(void);\n"
            "uint16_t %s_height(void);\n"
             % ("#include <stdbool.h>\n#include \"rif_reader.h\"\n"
                if var == "digit" else "",
                w, h, bpr, bpg, charset,
               prefix, w, prefix, h, prefix, bpr, prefix, bpg, prefix, count,
               prefix, baseline_y,
               sym_defs,
                fn, sym_proto, rif_proto, fn, fn))

    c = []
    c.append("/* Generated by tools/make_digit_font.py - DO NOT EDIT. */")
    c.append('#include "%s.h"' % name)
    c.append("")
    c.append("static const uint8_t s_%s_bitmaps[%s_CHAR_COUNT]"
             "[%s_BYTES_PER_GLYPH] = {" % (var, prefix, prefix))
    for ch, _box, img in glyphs:
        c.append("    /* '%s' */" % ch)
        c.extend(array_lines(pack(img, w, h), bpg))
    c.append("};")
    c.append("")
    if sym_count:
        c.append("static const uint8_t s_%s_symbols[%s_SYM_COUNT]"
                 "[%s_BYTES_PER_GLYPH] = {" % (var, prefix, prefix))
        for sym, label in symbols:
            c.append("    /* %s */" % label)
            c.extend(array_lines(pack(render_symbol(font, w, h, baseline_y, sym), w, h), bpg))
        c.append("};")
        c.append("")
    c.append("static const char s_%s_chars[] = \"%s\";" % (var, charset))
    c.append("")
    c.append("const uint8_t *%s_bitmap(char c)" % fn)
    c.append("{")
    c.append("    uint16_t i;")
    c.append("    for (i = 0u; i < %s_CHAR_COUNT; i++) {" % prefix)
    c.append("        if (s_%s_chars[i] == c) {" % var)
    c.append("            return s_%s_bitmaps[i];" % var)
    c.append("        }")
    c.append("    }")
    c.append("    return (const uint8_t *)0;")
    c.append("}")
    c.append("")
    if sym_count:
        c.append("const uint8_t *%s_symbol_bitmap(uint8_t sym)" % fn)
        c.append("{")
        c.append("    if (sym < %s_SYM_COUNT) {" % prefix)
        c.append("        return s_%s_symbols[sym];" % var)
        c.append("    }")
        c.append("    return (const uint8_t *)0;")
        c.append("}")
        c.append("")
    c.append("uint16_t %s_width(void)" % fn)
    c.append("{")
    c.append("    return %s_WIDTH;" % prefix)
    c.append("}")
    c.append("")
    c.append("uint16_t %s_height(void)" % fn)
    c.append("{")
    c.append("    return %s_HEIGHT;" % prefix)
    c.append("}")
    c.append("")
    if var == "digit":
        c.append("static const char s_rif_chars[] = \"%s\";" % charset)
        c.append("")
        c.append("bool font_digit_rif_code(char c, uint32_t *kind, uint16_t *code)")
        c.append("{")
        c.append("    uint16_t i;")
        c.append("    if (kind == 0 || code == 0) return false;")
        c.append("    for (i = 0u; s_rif_chars[i] != '\\0'; i++)")
        c.append("        if (s_rif_chars[i] == c) {")
        c.append("            *kind = RIF_KIND_DIGIT_CHAR;")
        c.append("            *code = (uint8_t)c;")
        c.append("            return true;")
        c.append("        }")
        c.append("    return false;")
        c.append("}")
        c.append("")
        c.append("bool font_digit_rif_symbol_code(uint8_t symbol, uint32_t *kind,")
        c.append("                              uint16_t *code)")
        c.append("{")
        c.append("    if (kind == 0 || code == 0 || symbol >= FONT_DIGIT_SYM_COUNT)")
        c.append("        return false;")
        c.append("    *kind = RIF_KIND_DIGIT_SYMBOL;")
        c.append("    *code = symbol;")
        c.append("    return true;")
        c.append("}")
        c.append("")
    with open(source, "w") as f:
        f.write("\n".join(c))

    return count, bpg


def main():
    ttf, out_dir, name, w, h, charset, symbols = parse_args(sys.argv)
    if not os.path.isfile(ttf):
        sys.exit("font not found: %s" % ttf)
    if not os.path.isdir(out_dir):
        sys.exit("output dir not found: %s" % out_dir)

    font = pick_font(ttf, w, h, charset)
    glyphs, baseline_y = make_glyphs(font, w, h, charset)
    count, bpg = emit_c(out_dir, name, w, h, charset, glyphs, symbols,
                        font, baseline_y)

    print("font name     : %s" % name)
    print("font size     : %d px" % font.size)
    print("baseline_y    : %d (cell height %d)" % (baseline_y, h))
    print("glyph count   : %d (+%d symbols)" % (count, len(symbols)))
    print("bytes/glyph   : %d" % bpg)
    for ch, (left, top, right, bottom), img in glyphs:
        ink = sum(bin(b).count("1") for b in img.tobytes())
        print("  '%s' bbox=(%d,%d,%d,%d) set_bits=%d"
              % (ch, left, top, right, bottom, ink))
    print("wrote %s and %s" % (os.path.join(out_dir, name + ".c"),
                               os.path.join(out_dir, name + ".h")))


if __name__ == "__main__":
    main()
