#!/usr/bin/env python3
"""Host tests for the K2000 resource flash image (RIF) packer/verifier.

Run with:  python3 -m unittest discover -s tools/tests -p "test_*.py"
or:        tools/tests/run_tests.sh

The tests exercise the pure host-side pipeline: parsing the generated C font
arrays, 1bpp -> RGB565 rendering (bit and row order), image assembly, CRC32
fields, directory bounds, reserved-region alignment/fill, diagnostic band
colors, tamper detection and the source cross-check round trip. No firmware
is executed and no hardware is touched.
"""
import os
import struct
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC_DIR = os.path.join(TOOLS_DIR, "..", "firmware", "src")
DIGIT_SRC_DIR = os.path.join(TOOLS_DIR, "font_source")
sys.path.insert(0, TOOLS_DIR)

import rif_common
from rif_common import (ALIGN, DEFAULT_FILL, DIAG_BANDS, DIAG_HEIGHT,
                        DIAG_WIDTH, ENTRY_SIZE, HEADER_SIZE, KIND_DIAG,
                         KIND_DIGIT_CHAR, KIND_DIGIT_SYM, KIND_HALF_CHAR,
                         KIND_TEXT_CHAR, KIND_TEXT_SYM,
                        KIND_RESERVED, MAGIC, VERSION_MAJOR, VERSION_MINOR,
                        build_entry, build_header, crc32, load_font,
                        make_diag_tile, parse_directory, parse_header,
                        render_tile, rgb888_to_rgb565, tile_to_1bpp,
                        verify_image)


def _load_packer():
    """Load pack_resource_flash.py as a module for CLI-level tests."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "pack_resource_flash",
        os.path.join(TOOLS_DIR, "pack_resource_flash.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_sample_image(**kw):
    """Build a RIF through the packer's public pipeline and return bytes."""
    mod = _load_packer()
    digit = load_font(DIGIT_SRC_DIR, "font_digits")
    half = load_font(SRC_DIR, "font_half")
    text = load_font(SRC_DIR, "font_text")
    fg = kw.pop("fg", rgb888_to_rgb565(0x00, 0xFF, 0x33))
    bg = kw.pop("bg", 0)
    fill = kw.pop("fill", DEFAULT_FILL)
    base = kw.pop("base", 0)
    if kw:
        raise TypeError("unexpected kwarg %r" % kw)

    dir_size = mod.count_entries(digit, half, text) * ENTRY_SIZE
    payload_start = rif_common.align_up(HEADER_SIZE + dir_size, ALIGN)
    entries, chunks, payload_bytes = mod.build_payloads(
        digit, half, fg, bg, fill, payload_start, text=text,
        transpose=True)
    image_size = payload_start + payload_bytes
    image = bytearray(image_size)
    image[0:HEADER_SIZE] = bytes([fill]) * HEADER_SIZE
    image[HEADER_SIZE:HEADER_SIZE + dir_size] = b"".join(entries)
    image[HEADER_SIZE + dir_size:payload_start] = bytes([fill]) * (
        payload_start - HEADER_SIZE - dir_size)
    for off, chunk in chunks:
        image[off:off + len(chunk)] = chunk
    header = build_header(image_size, HEADER_SIZE, dir_size, len(entries),
                          payload_start, base, fill)
    image[0:HEADER_SIZE] = header
    # Both CRC fields are still zero here; patch only after computing both.
    header_crc = crc32(bytes(header))
    image_crc = crc32(bytes(image))
    struct.pack_into("<I", image, 12, header_crc)
    struct.pack_into("<I", image, 32, image_crc)
    return bytes(image)


class TestCParse(unittest.TestCase):

    def test_digit_font_parsed(self):
        font = load_font(DIGIT_SRC_DIR, "font_digits")
        self.assertIsNotNone(font)
        dim, chars, glyphs, symbols = font
        self.assertEqual(dim[0], 56)
        self.assertEqual(dim[1], 104)
        self.assertEqual(dim[2], 7)
        self.assertEqual(len(chars), 35)
        self.assertEqual(len(glyphs), 35)
        self.assertEqual(len(symbols), 3)
        self.assertTrue(all(len(g) == 728 for g in glyphs))
        self.assertTrue(all(len(s[1]) == 728 for s in symbols))
        self.assertEqual(chars, "0123456789.+-Ee%mukKMWVOhDAC?RFLHzs")
        self.assertEqual([s[0] for s in symbols], ["MICRO", "DEGREE", "OHM"])

    def test_half_font_parsed(self):
        font = load_font(SRC_DIR, "font_half")
        self.assertIsNotNone(font)
        dim, chars, glyphs, _symbols = font
        self.assertEqual((dim[0], dim[1], dim[2]), (32, 56, 4))
        self.assertEqual(chars, "DCA")
        self.assertEqual(len(glyphs), 3)
        self.assertTrue(all(len(g) == 224 for g in glyphs))

    def test_missing_font_returns_none(self):
        self.assertIsNone(load_font(SRC_DIR, "font_nope"))


class TestRender(unittest.TestCase):

    def test_bit_order_and_rows(self):
        # 2 px wide, 2 rows; row 0 = 0b10, row 1 = 0b01 (MSB first).
        bpr = 1
        bm = bytes([0b10000000, 0b01000000])
        fg, bg = 0xFFFF, 0x0000
        tile = render_tile(bm, 2, 2, bpr, fg, bg)
        self.assertEqual(tile, struct.pack("<HHHH", fg, bg, bg, fg))

    def test_round_trip_1bpp(self):
        # Use the first digit glyph as a real sample.
        _dim, _chars, glyphs, _symbols = load_font(DIGIT_SRC_DIR, "font_digits")
        fg, bg = 0x07E6, 0x0000
        tile = render_tile(glyphs[0], 56, 104, 7, fg, bg)
        self.assertEqual(len(tile), 56 * 104 * 2)
        back = tile_to_1bpp(tile, 56, 104, 112, fg, bg)
        self.assertEqual(back, glyphs[0])

    def test_unexpected_pixel_rejected(self):
        fg, bg = 0x07E6, 0x0000
        tile = render_tile(bytes(1024), 64, 128, 8, fg, bg)
        bad = bytearray(tile)
        bad[0] = 0xFF                      # neither fg nor bg
        with self.assertRaises(ValueError):
            tile_to_1bpp(bytes(bad), 64, 128, 128, fg, bg)

    def test_fg_bg_must_differ(self):
        with self.assertRaises(ValueError):
            render_tile(bytes(8), 8, 8, 1, 0xFFFF, 0xFFFF)

    def test_diag_band_colors(self):
        fg, bg = 0x07E6, 0x0000
        tile = make_diag_tile(fg, bg)
        self.assertEqual(len(tile), DIAG_WIDTH * DIAG_HEIGHT * 2)
        band_w = DIAG_WIDTH // len(DIAG_BANDS)
        for i, (name, color) in enumerate(DIAG_BANDS):
            if color is None:
                color = fg if name == "FG" else bg
            off = (DIAG_HEIGHT // 2) * (DIAG_WIDTH * 2) + i * band_w * 2
            self.assertEqual(struct.unpack_from("<H", tile, off)[0], color,
                             "band %s" % name)


class TestImage(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.image = build_sample_image()
        cls.header = parse_header(cls.image)
        cls.entries = parse_directory(cls.image, cls.header)

    def test_header_fields(self):
        self.assertEqual(self.header["magic"], MAGIC)
        self.assertEqual(self.header["version"],
                         (VERSION_MAJOR, VERSION_MINOR))
        self.assertEqual(self.header["image_size"], len(self.image))
        self.assertEqual(self.header["flash_base"], 0)
        self.assertEqual(self.header["fill_byte"], DEFAULT_FILL)
        self.assertEqual(self.header["entry_size"], ENTRY_SIZE)
        self.assertEqual(self.header["payload_start"] % ALIGN, 0)

    def test_directory_count(self):
        counts = {}
        for ent in self.entries:
            counts[ent["kind"]] = counts.get(ent["kind"], 0) + 1
        self.assertEqual(counts[KIND_DIGIT_CHAR], 35)
        self.assertEqual(counts[KIND_DIGIT_SYM], 3)
        self.assertEqual(counts[KIND_HALF_CHAR], 3)
        self.assertEqual(counts[KIND_TEXT_CHAR], 95)
        self.assertEqual(counts[KIND_TEXT_SYM], 4)
        self.assertEqual(counts[KIND_DIAG], 1)
        self.assertEqual(counts[KIND_RESERVED], 1)

    def test_expected_payload_sizes(self):
        by_kind = {}
        for ent in self.entries:
            by_kind.setdefault(ent["kind"], []).append(ent)
        for ent in by_kind[KIND_DIGIT_CHAR]:
            self.assertEqual(ent["size"], 56 * 104 * 2)
        for ent in by_kind[KIND_HALF_CHAR]:
            self.assertEqual(ent["size"], 32 * 56 * 2)
        for ent in by_kind[KIND_TEXT_CHAR] + by_kind[KIND_TEXT_SYM]:
            self.assertEqual(ent["size"], 12 * 24 * 2)
        for ent in by_kind[KIND_DIAG]:
            self.assertEqual(ent["size"], DIAG_WIDTH * DIAG_HEIGHT * 2)

    def test_payloads_aligned_and_in_bounds(self):
        for ent in self.entries:
            self.assertEqual(ent["offset"] % ALIGN, 0)
            self.assertGreaterEqual(ent["offset"], self.header["payload_start"])
            self.assertLessEqual(ent["offset"] + ent["size"], len(self.image))

    def test_reserved_regions(self):
        rsvd = [e for e in self.entries if e["kind"] == KIND_RESERVED]
        self.assertEqual([e["name"] for e in rsvd], ["ui_assets"])
        for ent in rsvd:
            self.assertEqual(ent["size"] % ALIGN, 0)
            payload = self.image[ent["offset"]:ent["offset"] + ent["size"]]
            self.assertTrue(all(b == DEFAULT_FILL for b in payload))

    def test_no_overlap(self):
        ranges = sorted((e["offset"], e["offset"] + e["size"]) for e in self.entries)
        for (a0, b0), (a1, b1) in zip(ranges, ranges[1:]):
            self.assertLessEqual(b0, a1)

    def test_entry_crcs(self):
        for ent in self.entries:
            payload = self.image[ent["offset"]:ent["offset"] + ent["size"]]
            self.assertEqual(crc32(payload), ent["crc"], ent["name"])

    def test_verify_clean(self):
        self.assertEqual(verify_image(self.image), [])

    def test_cross_check_clean(self):
        expect = rif_common.collect_expectations(SRC_DIR, DIGIT_SRC_DIR)
        self.assertEqual(verify_image(self.image, expect=expect), [])

    def test_verify_detects_tamper(self):
        bad = bytearray(self.image)
        bad[0x21000 + 100] ^= 0xFF          # inside '8' glyph payload
        problems = verify_image(bytes(bad))
        self.assertTrue(any("image_crc32" in p for p in problems))
        self.assertTrue(any("payload CRC" in p for p in problems))

    def test_verify_rejects_bad_magic(self):
        bad = bytearray(self.image)
        bad[0:4] = b"NOPE"
        problems = verify_image(bytes(bad))
        self.assertTrue(any("bad magic" in p for p in problems))

    def test_verify_rejects_size_mismatch(self):
        bad = bytearray(self.image)
        struct.pack_into("<I", bad, 8, len(bad) + 1)
        problems = verify_image(bytes(bad))
        self.assertTrue(any("image_size" in p for p in problems))

    def test_base_offset_recorded(self):
        img = build_sample_image(base=0x200000)
        self.assertEqual(parse_header(img)["flash_base"], 0x200000)
        self.assertEqual(verify_image(img), [])

    def test_verify_rejects_misaligned_base(self):
        img = build_sample_image(base=0x1234)
        problems = verify_image(img)
        self.assertTrue(any("flash_base" in p for p in problems))

    def test_verify_accepts_full_flash_dump(self):
        # A whole-chip dump whose image starts at the recorded base (0 here)
        # must verify as if it were the standalone image file.
        dump = bytearray([DEFAULT_FILL]) * (8 * 1024 * 1024)
        dump[0:len(self.image)] = self.image
        self.assertEqual(verify_image(bytes(dump)), [])

    def test_verify_rejects_overflowing_base(self):
        # base 0x780000 + 0xBE000 image > 8 MB W25Q64JV capacity.
        img = build_sample_image(base=0x780000)
        problems = verify_image(img)
        self.assertTrue(any("exceeds flash capacity" in p for p in problems))

    def test_packer_rejects_overflowing_base(self):
        mod = _load_packer()
        with self.assertRaises(SystemExit):
            mod.main(["--base-offset", "0x780000", "--output",
                      os.path.join(tempfile.gettempdir(),
                                   "rif_overflow_test.img")])


class TestFormat(unittest.TestCase):

    def test_entry_round_trip(self):
        raw = build_entry(KIND_DIGIT_CHAR, 7, 0x1000, 16384, 64, 128, 128,
                          0x38, 0, 0xDEADBEEF, 0x07E6, 0x0000,
                          "digit_chars")
        self.assertEqual(len(raw), ENTRY_SIZE)
        ent = rif_common.parse_entry(raw)
        self.assertEqual(ent["kind"], KIND_DIGIT_CHAR)
        self.assertEqual(ent["id"], 7)
        self.assertEqual(ent["offset"], 0x1000)
        self.assertEqual(ent["width"], 64)
        self.assertEqual(ent["code"], 0x38)      # '8'
        self.assertEqual(ent["crc"], 0xDEADBEEF)
        self.assertEqual(ent["name"], "digit_chars")

    def test_header_fill_byte(self):
        raw = build_header(0x1000, 64, 0, 0, 0x1000, 0, 0xAA)
        h = parse_header(raw)
        self.assertEqual(h["fill_byte"], 0xAA)


if __name__ == "__main__":
    unittest.main()
