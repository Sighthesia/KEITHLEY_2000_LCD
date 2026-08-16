#!/usr/bin/env python3
"""Verify a K2000 resource flash image (RIF).

Checks, in order:

  * file size against the header image_size field; a whole-chip flash dump
    whose image starts at the recorded flash_base is accepted as well
    (header/image CRCs, directory bounds, per-entry CRC, reserved fill,
    diagnostic colors, and glyph round-trip are then checked on that slice)
  * header magic "K2RF" and version 1.0
  * header CRC32 and whole-image CRC32 (each computed with both CRC fields
    zeroed, exactly as the packer wrote them)
   * flash capacity: flash_base + image_size must fit inside the 8 MB chip
  * directory bounds (offset/size within the file, entry size and count
    consistent, payload_start 4 KiB aligned)
  * every entry: known kind, 4 KiB-aligned payload offset, payload inside
    the file, non-overlapping payloads, size == stride*height,
    stride == width*2 (RGB565), payload CRC32
  * glyph count against expected bounds; with --src-dir, exact counts,
    char codes, payload sizes and a full glyph round-trip (RGB565 back to
    1bpp compared with the generated C source bitmaps)
  * reserved regions: 4 KiB-aligned size, filled with the header fill byte
  * diagnostic tile band colors

Exit code is 0 when the image is valid, 1 otherwise. The script never
touches hardware; it validates a file (or a flash dump) on the host.

Usage:
    python3 tools/verify_resource_flash.py [--src-dir firmware/src]
        [--digit-src-dir tools/font_source] IMAGE
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rif_common
from rif_common import verify_image, collect_expectations

DEFAULT_SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "firmware", "src")
DEFAULT_DIGIT_SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "font_source")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Verify a K2000 resource flash image")
    parser.add_argument("image", help="path to the .img file (or flash dump)")
    parser.add_argument("--src-dir", default=None,
                        help="firmware/src with generated fonts; enables "
                             "exact glyph-count, char-code and round-trip "
                              "checks (default: none)")
    parser.add_argument("--digit-src-dir", default=None,
                        help="archived font_digits source; used with --src-dir")
    args = parser.parse_args(argv)

    if not os.path.isfile(args.image):
        print("FAIL: image not found: %s" % args.image, file=sys.stderr)
        return 1
    with open(args.image, "rb") as fh:
        data = fh.read()

    expect = None
    if args.src_dir:
        src_dir = os.path.abspath(args.src_dir)
        expect = collect_expectations(
            src_dir, os.path.abspath(args.digit_src_dir)
            if args.digit_src_dir else DEFAULT_DIGIT_SRC_DIR)
        if expect:
            print("cross-check source: %s" % src_dir)

    problems = verify_image(data, expect=expect)
    if problems:
        for p in problems:
            print("FAIL: %s" % p)
        print("verify %s: FAILED (%d problem(s))" % (args.image, len(problems)))
        return 1

    header = rif_common.parse_header(data)
    entries = rif_common.parse_directory(data, header)
    counts = {}
    for ent in entries:
        counts[ent["kind"]] = counts.get(ent["kind"], 0) + 1
    print("verify %s: OK (%d bytes, %d entries, base 0x%06X)"
          % (args.image, len(data), len(entries), header["flash_base"]))
    for kind in sorted(counts, key=lambda k: k.decode()):
        print("  %s: %d" % (kind.decode(), counts[kind]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
