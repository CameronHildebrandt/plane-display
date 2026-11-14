#!/usr/bin/env python3
"""Convert a one-row black/white glyph PNG back to editable x/o text.

Every 7×7 cell is read left-to-right as a..z. The seventh column is a
black separator. The remaining six columns are scanned from the right: any
fully black trailing columns are omitted, which is how 5px glyphs remain 5px
wide while a 6px glyph such as bold Y stays 6px wide.
"""

import argparse
from pathlib import Path

from PIL import Image

ALPHABET = "abcdefghijklmnopqrstuvwxyz"
CELL_WIDTH = 7
CELL_HEIGHT = 7
COLUMNS = len(ALPHABET)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("png", type=Path, help="Edited black/white glyph-sheet PNG")
    parser.add_argument("face", help='Label printed above the returned grids, e.g. "Bold capitals"')
    args = parser.parse_args()

    image = Image.open(args.png).convert("1")
    expected_width = COLUMNS * CELL_WIDTH
    expected_height = CELL_HEIGHT
    if image.size != (expected_width, expected_height):
        raise ValueError(f"Expected an exact {expected_width}×{expected_height} PNG, got {image.width}×{image.height}")

    pixels = image.load()
    use_capital_labels = "capital" in args.face.lower()
    print(f"## {args.face}\n\n```text")
    for index, glyph in enumerate(ALPHABET):
        cell_x = index * CELL_WIDTH
        cell_y = 0
        # Column 6 is the separator; trim black columns only inside 0..5.
        width = 1
        for column in range(CELL_WIDTH - 2, -1, -1):
            if any(pixels[cell_x + column, cell_y + row] for row in range(CELL_HEIGHT)):
                width = column + 1
                break
        bitmap = []
        for row in range(CELL_HEIGHT):
            bitmap.append("".join(
                "x" if pixels[cell_x + column, cell_y + row] else "o"
                for column in range(width)
            ))
        label = glyph.upper() if use_capital_labels else glyph
        print(f"{label} {'/'.join(bitmap)}")
    print("```")


if __name__ == "__main__":
    main()
