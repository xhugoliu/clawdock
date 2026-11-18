#!/usr/bin/env python3
"""Rasterize the device's three type roles from a Noto Sans variable font."""

import argparse
from pathlib import Path
import struct

from PIL import ImageFont

GLYPH = struct.Struct("<BbbBBH")


def render_face(source, size, weight):
    font = ImageFont.truetype(source, size, layout_engine=ImageFont.Layout.BASIC)
    font.set_variation_by_axes([weight, 100])
    first, limit = 32, 127
    records = bytearray()
    coverage = bytearray()
    for codepoint in range(first, limit):
        glyph = chr(codepoint)
        advance = round(font.getlength(glyph))
        mask, (left, top) = font.getmask2(glyph, mode="L", anchor="ls")
        width, height = mask.size
        offset = (limit - first) * GLYPH.size + len(coverage)
        records.extend(GLYPH.pack(advance, left, top, width, height, offset))
        pixels = bytes(mask)
        if len(pixels) % 2:
            pixels += b"\0"
        coverage.extend((high & 0xF0) | (low >> 4) for high, low in zip(pixels[::2], pixels[1::2]))
    return records + coverage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    faces = [render_face(args.source, size, weight) for size, weight in [(22, 400), (27, 450), (36, 600)]]
    offset = 4 * len(faces)
    directory = bytearray()
    for face in faces:
        directory.extend(struct.pack("<I", offset))
        offset += len(face)
    args.output.write_bytes(directory + b"".join(faces))


if __name__ == "__main__":
    main()
