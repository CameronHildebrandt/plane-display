#!/usr/bin/env python3
"""Convert an exact 32x32 RGBA logo PNG into a compact palette sprite header."""

import argparse
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--symbol", required=True)
    args = parser.parse_args()

    image = Image.open(args.input).convert("RGBA")
    if image.size != (32, 32):
        raise ValueError(f"Expected a 32x32 logo, got {image.size}")

    palette = [(0, 0, 0)]
    indexes = []
    for red, green, blue, alpha in image.getdata():
        colour = (red, green, blue) if alpha else (0, 0, 0)
        if colour not in palette:
            palette.append(colour)
        indexes.append(palette.index(colour))

    lines = [
        "#pragma once",
        "",
        "// Generated from the supplied 32x32 nearest-neighbour PNG by",
        "// tools/png_to_palette_logo.py. Index 0 is transparent.",
        f"constexpr uint8_t {args.symbol}Palette[{len(palette)}][3] PROGMEM = {{",
    ]
    lines.extend(f"    {{{red}, {green}, {blue}}}," for red, green, blue in palette)
    lines.extend(["};", f"constexpr uint8_t {args.symbol}Pixels[32][32] PROGMEM = {{"])
    for row in range(32):
        values = ", ".join(str(index) for index in indexes[row * 32:(row + 1) * 32])
        lines.append(f"    {{{values}}},")
    lines.extend(["};", ""])
    args.output.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
