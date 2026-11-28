#!/usr/bin/env python3
"""Export the firmware aircraft-sprite header back to exact 33x33 PNGs."""

import argparse
import re
from pathlib import Path

from PIL import Image

SIZE = 33
CATEGORIES = [
    "single_engine_private", "single_engine_prop", "twin_engine_private",
    "twin_engine_prop", "four_engine_prop", "twin_engine_jet",
    "twin_engine_jet_rear_mounted", "quad_engine_jet",
    "single_rotor_helicopter", "dual_rotor_helicopter", "stratofortress",
    "osprey", "glider", "b2_dorito", "fighter_jet", "balloon",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path, help="Generated include/aircraft_sprites.h")
    parser.add_argument("output_directory", type=Path, help="Directory for the sixteen 33x33 PNGs")
    args = parser.parse_args()
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)ULL", args.header.read_text(encoding="utf-8"))]
    expected = len(CATEGORIES) * SIZE
    if len(values) != expected:
        raise ValueError(f"Expected {expected} bitmap rows, found {len(values)}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    for index, category in enumerate(CATEGORIES):
        image = Image.new("1", (SIZE, SIZE), 0)
        pixels = image.load()
        for y, value in enumerate(values[index * SIZE:(index + 1) * SIZE]):
            for x in range(SIZE):
                pixels[x, y] = 1 if value & (1 << (32 - x)) else 0
        image.save(args.output_directory / f"{category}.png")
    print(f"Wrote {len(CATEGORIES)} exact {SIZE}x{SIZE} PNGs to {args.output_directory}")


if __name__ == "__main__":
    main()
