# Aircraft sprite reference

The complete fallback art library lives in
[`assets/aircraft-sprites/`](assets/aircraft-sprites/). It contains exactly
fifteen editable **33×33px** PNGs. Every silhouette is top-down with the nose
pointing up. Pure white means a lit LED; pure black means transparent/off.
Keep the canvas exactly 33×33 and use no anti-aliasing, greyscale, padding, or
resizing.

## Categories

1. `single_engine_prop` — Cessna Caravan/piston singles, Piper, Cirrus and Diamond
2. `twin_engine_prop` — Dash 8, ATR, Saab, Beech and other commuter twins
3. `four_engine_prop` — C-130, P-3, DHC-7 and similar four-prop aircraft
4. `twin_engine_jet` — normal commercial, regional and business jets
5. `tri_engine_jet` — 727, MD-11, DC-10 and L-1011
6. `quad_engine_jet` — 747, A340, A380, Il-96, An-124/An-225 and C-5
7. `single_rotor_helicopter` — conventional helicopter
8. `dual_rotor_helicopter` — Chinook/Sea Knight-style tandem rotor helicopter
9. `stratofortress` — B-52-style strategic bomber
10. `osprey` — V-22/CV-22 tiltrotor
11. `glider` — sailplane
12. `b2_dorito` — B-2 flying wing
13. `fighter_jet` — generic fast military jet
14. `generic_plane` — unknown/unclassified fixed-wing fallback
15. `balloon` — balloon

At this size, engine count, rotor layout, and broad wing geometry are more
useful than model-specific detail. One-pixel propeller discs are deliberately
preserved on turboprops.

## Edit and import

Open the individual PNGs in a pixel/sprite editor, edit them in place, then
regenerate the firmware masks:

```bash
cd /Users/cameronhildebrandt/Desktop/LED\ Plane\ Display/esp32-rgb-matrix-hello
python3 tools/png_to_aircraft_sprites.py assets/aircraft-sprites include/aircraft_sprites.h
```

To restore the editable PNGs from the compiled source:

```bash
python3 tools/aircraft_sprites_to_png.py include/aircraft_sprites.h assets/aircraft-sprites
```

You can send me any edited PNGs, or the whole `assets/aircraft-sprites` folder,
and I can import, build, and flash them.
