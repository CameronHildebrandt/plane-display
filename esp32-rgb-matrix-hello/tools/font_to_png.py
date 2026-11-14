#!/usr/bin/env python3
"""Render one face from FONT_REFERENCE.md as an exact 1-bit horizontal PNG.

All 26 glyph cells sit on one 7px-tall row. Each cell is exactly 7×7 pixels:
six editable glyph columns (enough for the 6px bold Y) plus one permanently
black separator column. PNG pixels are literal display pixels: black = off,
white = on. No scaling or anti-aliasing occurs.
"""

import argparse
import re
from pathlib import Path
from typing import Dict, List

from PIL import Image

ALPHABET = "abcdefghijklmnopqrstuvwxyz"
CELL_WIDTH = 7  # six glyph pixels + one guaranteed-black separator
CELL_HEIGHT = 7
COLUMNS = len(ALPHABET)


def read_face(markdown: Path, face: str) -> Dict[str, List[str]]:
    heading = f"## {face}".lower()
    lines = markdown.read_text(encoding="utf-8").splitlines()
    start = next((i for i, line in enumerate(lines) if line.strip().lower() == heading), None)
    if start is None:
        raise ValueError(f"Face {face!r} was not found in {markdown}")
    rows: Dict[str, List[str]] = {}
    in_block = False
    for line in lines[start + 1:]:
        if line.startswith("## "):
            break
        if line.strip().startswith("```"):
            in_block = not in_block
            continue
        if not in_block:
            continue
        match = re.fullmatch(r"([a-zA-Z])\s+([xo/]+)", line.strip())
        if not match:
            continue
        glyph, bitmap = match.groups()
        pixel_rows = bitmap.split("/")
        if len(pixel_rows) != CELL_HEIGHT or any(set(row) - {"x", "o"} for row in pixel_rows):
            raise ValueError(f"Invalid bitmap for {glyph}")
        if any(len(row) > CELL_WIDTH - 1 for row in pixel_rows):
            raise ValueError(f"{glyph} is wider than six pixels")
        rows[glyph.lower()] = pixel_rows
    missing = set(ALPHABET) - rows.keys()
    if missing:
        raise ValueError(f"Missing glyphs: {''.join(sorted(missing))}")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("markdown", type=Path, help="FONT_REFERENCE.md")
    parser.add_argument("face", help='Heading name, e.g. "Normal lowercase"')
    parser.add_argument("output", type=Path, help="Output PNG path")
    args = parser.parse_args()

    glyphs = read_face(args.markdown, args.face)
    image = Image.new("1", (COLUMNS * CELL_WIDTH, CELL_HEIGHT), 0)
    pixels = image.load()
    for index, glyph in enumerate(ALPHABET):
        cell_x = index * CELL_WIDTH
        cell_y = 0
        for row_index, row in enumerate(glyphs[glyph]):
            for column_index, value in enumerate(row):
                if value == "x":
                    pixels[cell_x + column_index, cell_y + row_index] = 1
    image.save(args.output)
    print(f"Wrote {image.width}×{image.height} 1-bit PNG: {args.output}")


if __name__ == "__main__":
    main()
