#!/bin/sh
# Host tests for the resource flash image (RIF) packer/verifier.
# Pure Python; needs no firmware toolchain and no hardware.
set -e
cd "$(dirname "$0")/.."
python3 -m unittest discover -s tests -p "test_*.py" -v