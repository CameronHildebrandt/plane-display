# ESP32-S3 target: ESP32-HUB75-MatrixPanel-DMA

This target uses [mrcodetastic/ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA), selected for its ESP32-S3 DMA support and Adafruit-GFX-compatible API.

`hub75_canvas.hpp` adapts the library's `MatrixPanel_I2S_DMA` object to the shared `flight_display::Canvas`. All ticket layout code stays in `../include/flight_ticket.hpp`, which is also used by the Mac emulator.

Before this can be built for hardware, select the exact S3 board/adapter and replace the `-1` placeholders in `flight_display.ino` with its verified HUB75 pin mapping. For two chained 64×64 panels, wire the E address line too. Do not guess these pins.

For the first hardware test, run the library's own test-pattern sketch before this application. Confirm panel driver-chip and scan compatibility, 5V power capacity, shared ground, and any needed 3.3V→5V level shifting.

## VS Code flash workflow

This folder is a self-contained PlatformIO project. Open **`firmware/esp32`** in
VS Code, install the suggested **PlatformIO IDE** extension, then use the
PlatformIO toolbar (the alien-head icon in VS Code's left sidebar):

- **Build** — validates the sketch.
- **Upload** — builds and flashes the connected board.
- **Monitor** — opens its 115200-baud diagnostic log.

The project currently targets the generic `esp32-s3-devkitc-1`, which is a
safe starting point for a Waveshare ESP32-S3 board but must be checked against
the product model before the first upload. PlatformIO automatically finds the
USB port; if it does not, use a data-capable USB cable and enter download mode
(hold **BOOT**, tap **RESET**, then release **BOOT**) before uploading.

The application intentionally will not compile until every HUB75 GPIO value in
`flight_display.ino` is replaced with the verified mapping for the Waveshare
board/adapter. That prevents an upload based on guessed display wiring.
