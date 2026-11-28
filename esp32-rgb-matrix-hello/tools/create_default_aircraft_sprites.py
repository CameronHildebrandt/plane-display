#!/usr/bin/env python3
"""Create the initial 33x33 aircraft sprite PNG library.

This is a one-time bootstrap from the silhouettes that used to be procedural
in main.cpp.  Afterwards, the PNGs in assets/aircraft-sprites/ are canonical:
edit them and run png_to_aircraft_sprites.py to update the firmware header.
"""

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


class Canvas:
    def __init__(self):
        self.pixels = [[False] * SIZE for _ in range(SIZE)]

    def pixel(self, x, y):
        if 0 <= x < SIZE and 0 <= y < SIZE:
            self.pixels[y][x] = True

    def rect(self, x, y, width, height):
        for row in range(y, y + height):
            for column in range(x, x + width):
                self.pixel(column, row)

    def fuselage(self, start, end, half):
        for row in range(start, end + 1):
            row_half = half
            if row <= start + 1 or row >= end - 1:
                row_half = 0
            elif row == start + 2 or row == end - 2:
                row_half = min(1, half)
            self.rect(16 - row_half, row, row_half * 2 + 1, 1)

    def save(self, destination):
        image = Image.new("1", (SIZE, SIZE), 0)
        pixels = image.load()
        for y, row in enumerate(self.pixels):
            for x, on in enumerate(row):
                pixels[x, y] = 1 if on else 0
        image.save(destination)


def airliner(canvas, widebody, four_engines):
    canvas.fuselage(1, 31, 3 if widebody else 2)
    for row in range(10, 19):
        span = min(15, (8 if widebody else 7) + (row - 10) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    if four_engines:
        for x, y in ((4, 18), (10, 19), (20, 19), (26, 18)):
            canvas.rect(x, y, 3, 4)
        canvas.rect(8, 28, 17, 2)
    else:
        canvas.rect(7, 18, 3, 4)
        canvas.rect(23, 18, 3, 4)
        canvas.rect(10, 28, 13, 2)


def regional_or_business(canvas):
    canvas.fuselage(2, 30, 2)
    for row in range(12, 18):
        span = 6 + (row - 12) * 2
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    canvas.rect(10, 22, 3, 5)
    canvas.rect(20, 22, 3, 5)
    canvas.rect(8, 27, 17, 2)


def generic_747(canvas):
    # Original supplied 747 mask faced right. Rotate it counter-clockwise so
    # every source asset is a top-down, nose-up 33x33 aircraft.
    rows = [
        0x000000000, 0x000600000, 0x000700000, 0x000380000, 0x000180000,
        0x0001D8000, 0x0000F8000, 0x000070000, 0x000070000, 0x000038000,
        0x00003F800, 0x00003F800, 0x08001F000, 0x0C001F000, 0x0E001FC00,
        0x077FFFFF8, 0x0FFFFFFFE, 0x077FFFFF8, 0x0E001FC00, 0x0C001F000,
        0x08001F000, 0x00003F800, 0x00003F800, 0x000038000, 0x000070000,
        0x000070000, 0x0000F8000, 0x0000D8000, 0x000180000, 0x000380000,
        0x000700000, 0x000600000, 0x000000000,
    ]
    for row, value in enumerate(rows):
        for column in range(SIZE):
            if value & (1 << (32 - column)):
                canvas.pixel(row, 32 - column)


def fighter_jet(canvas):
    # Pointed nose, clipped delta wings, small rear stabilizers and two tight
    # engine trails keep the fighter distinct at the 33px scale.
    canvas.pixel(16, 1)
    canvas.rect(15, 2, 3, 8)
    canvas.rect(14, 7, 5, 7)
    for row in range(10, 20):
        span = min(15, 2 + (row - 10) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    canvas.rect(15, 14, 3, 14)
    canvas.rect(10, 23, 13, 2)
    canvas.rect(14, 27, 2, 4)
    canvas.rect(17, 27, 2, 4)


def superheavy(canvas):
    # Oversized cargo/bomber planform: broad, nearly straight wings, chunky
    # fuselage, four engine pods, and a broad tail.
    canvas.fuselage(1, 31, 4)
    for row in range(9, 20):
        span = min(16, 8 + (row - 9) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    for x, y in ((2, 18), (8, 18), (22, 18), (28, 18)):
        canvas.rect(x, y, 3, 5)
    canvas.rect(6, 28, 21, 2)
    canvas.rect(11, 30, 11, 2)


def prop(canvas, engines):
    canvas.fuselage(3, 30, 1 if engines == 1 else 2)
    for row in range(12, 18):
        span = (5 if engines == 1 else 8) + (row - 12) * 2
        canvas.rect(16 - min(15, span), row, min(31, span * 2 + 1), 1)
    propellers = {1: (16,), 2: (4, 28), 4: (3, 10, 22, 29)}[engines]
    for x in propellers:
        canvas.rect(x, 8, 1, 11)
    canvas.rect(10, 27, 13, 2)


def jet(canvas, engines):
    canvas.fuselage(1, 31, 3 if engines == 4 else 2)
    for row in range(10, 19):
        span = min(15, 7 + (row - 10) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    positions = {2: ((7, 18), (23, 18)),
                 3: ((7, 18), (23, 18), (15, 24)),
                 4: ((4, 18), (10, 19), (20, 19), (26, 18))}[engines]
    for x, y in positions:
        canvas.rect(x, y, 3, 4)
    canvas.rect(8 if engines == 4 else 10, 28, 17 if engines == 4 else 13, 2)


def single_rotor_helicopter(canvas):
    canvas.fuselage(7, 25, 2)
    canvas.rect(2, 10, 29, 1)
    canvas.rect(15, 7, 3, 19)
    canvas.rect(15, 25, 3, 6)
    canvas.rect(11, 30, 11, 1)
    canvas.pixel(16, 31)


def dual_rotor_helicopter(canvas):
    canvas.fuselage(5, 29, 3)
    for rotor_y in (8, 21):
        canvas.rect(2, rotor_y, 29, 1)
        canvas.rect(15, rotor_y - 3, 3, 7)
    canvas.rect(10, 28, 13, 2)


def stratofortress(canvas):
    canvas.fuselage(2, 31, 1)
    for row in range(11, 19):
        span = min(16, 4 + (row - 11) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    for x in (2, 6, 11, 14, 18, 21, 26, 30):
        canvas.rect(x, 18, 1, 3)
    canvas.rect(7, 28, 19, 1)
    canvas.rect(15, 29, 3, 3)


def osprey(canvas):
    canvas.fuselage(7, 28, 2)
    canvas.rect(2, 14, 29, 2)
    for x in (3, 29):
        canvas.rect(x, 8, 1, 13)
        canvas.rect(x - 2, 13, 5, 3)
    canvas.rect(10, 26, 13, 2)


def glider(canvas):
    canvas.fuselage(2, 30, 0)
    canvas.rect(1, 14, 31, 1)
    canvas.rect(6, 15, 21, 1)
    canvas.rect(11, 27, 11, 1)


def b2_dorito(canvas):
    canvas.pixel(16, 3)
    for row in range(4, 26):
        span = min(15, (row - 3) * 2)
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    for row in range(26, 31):
        span = 15 - (row - 25) * 2
        canvas.rect(16 - max(1, span), row, max(3, span * 2 + 1), 1)


def balloon(canvas):
    for row, span in ((2, 3), (3, 6), (4, 9), (5, 11), (6, 13), (7, 14),
                      (8, 15), (9, 15), (10, 14), (11, 13), (12, 11),
                      (13, 9), (14, 6), (15, 3)):
        canvas.rect(16 - span, row, span * 2 + 1, 1)
    canvas.rect(13, 17, 1, 5)
    canvas.rect(19, 17, 1, 5)
    canvas.rect(14, 21, 5, 3)


def make(category):
    canvas = Canvas()
    if category == "single_engine_private":
        jet(canvas, 1)
    elif category == "single_engine_prop":
        prop(canvas, 1)
    elif category == "twin_engine_private":
        jet(canvas, 2)
    elif category == "twin_engine_prop":
        prop(canvas, 2)
    elif category == "four_engine_prop":
        prop(canvas, 4)
    elif category == "twin_engine_jet":
        jet(canvas, 2)
    elif category == "twin_engine_jet_rear_mounted":
        jet(canvas, 2)
    elif category == "quad_engine_jet":
        jet(canvas, 4)
    elif category == "single_rotor_helicopter":
        single_rotor_helicopter(canvas)
    elif category == "dual_rotor_helicopter":
        dual_rotor_helicopter(canvas)
    elif category == "stratofortress":
        stratofortress(canvas)
    elif category == "osprey":
        osprey(canvas)
    elif category == "glider":
        glider(canvas)
    elif category == "b2_dorito":
        b2_dorito(canvas)
    elif category == "fighter_jet":
        fighter_jet(canvas)
    elif category == "balloon":
        balloon(canvas)
    return canvas


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / "assets" / "aircraft-sprites"
    output.mkdir(parents=True, exist_ok=True)
    for category in CATEGORIES:
        make(category).save(output / f"{category}.png")
    print(f"Wrote {len(CATEGORIES)} exact {SIZE}x{SIZE} PNGs to {output}")


if __name__ == "__main__":
    main()
