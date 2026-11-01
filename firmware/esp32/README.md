# ESP32-S3 target: ESP32-HUB75-MatrixPanel-DMA

This target uses [mrcodetastic/ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA), selected for its ESP32-S3 DMA support and Adafruit-GFX-compatible API.

`hub75_canvas.hpp` adapts the library's `MatrixPanel_I2S_DMA` object to the shared `flight_display::Canvas`. All ticket layout code stays in `../include/flight_ticket.hpp`, which is also used by the Mac emulator.

Before this can be built for hardware, select the exact S3 board/adapter and replace the `-1` placeholders in `flight_display.ino` with its verified HUB75 pin mapping. For two chained 64×64 panels, wire the E address line too. Do not guess these pins.

For the first hardware test, run the library's own test-pattern sketch before this application. Confirm panel driver-chip and scan compatibility, 5V power capacity, shared ground, and any needed 3.3V→5V level shifting.
