# Aircraft sprite reference

Each aircraft silhouette is an exact **33×33px**, black-and-white PNG in
[`assets/aircraft-sprites/`](assets/aircraft-sprites/). White means a lit LED;
black means transparent/off. Every sprite is top-down with the nose pointing
up. There is no anti-aliasing, greyscale, transparency, resizing, or padding.

## Categories

1. `widebody_four_engine` — 747, A340, A380, MD-11, Il-96
2. `widebody_twin_jet` — 777, 787, A330, A350
3. `narrowbody_jet` — 737, A318–A321, 717, 727, 757, MD-80/DC-9
4. `regional_jet` — CRJ, Embraer E-Jets, ARJ, Superjet, Fokker/BAe regional jets
5. `turboprop` — Dash 8, ATR, Saab, Beech, PC-12, Cessna Caravan and similar
6. `light_prop` — Cessna piston singles, Piper, Cirrus, Diamond and similar
7. `business_jet` — Citation, Challenger, Gulfstream, Learjet, Falcon and similar
8. `helicopter` — rotorcraft
9. `generic_747` — unknown/unclassified aircraft fallback; the original supplied 747
10. `fighter_jet` — military fast jets such as F-16/F-18/F-22/F-35, F-15, MiG, Sukhoi and Rafale
11. `superheavy` — giant cargo aircraft and strategic bombers, including Antonov An-124/An-225, C-5 Galaxy and B-52

## Edit and import

Open any individual PNG in a pixel/sprite editor. Keep it at an actual 33×33
pixels—do not resize the canvas—and use only pure black (`#000000`) and pure
white (`#FFFFFF`). When you are done, replace the matching file and run:

```bash
cd /Users/cameronhildebrandt/Desktop/LED\ Plane\ Display/esp32-rgb-matrix-hello
python3 tools/png_to_aircraft_sprites.py assets/aircraft-sprites include/aircraft_sprites.h
```

That converts the nine PNGs into the exact firmware masks. To go the other
direction and restore the editable PNGs from the compiled source:

```bash
python3 tools/aircraft_sprites_to_png.py include/aircraft_sprites.h assets/aircraft-sprites
```

Send me any edited PNGs (or the whole `assets/aircraft-sprites` folder) and I
can import, build, and flash them.
