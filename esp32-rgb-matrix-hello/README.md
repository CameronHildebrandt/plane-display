# Waveshare ESP32-S3-RGB-Matrix: Hello World

A standalone PlatformIO demo for the Waveshare **ESP32-S3-RGB-Matrix** driver
board and two chained 64×64 HUB75 panels (128×64 total). It renders a static,
high-contrast `HELLO` / `KENNY` design across the complete display.

## Hardware

1. Connect the board's `HUB75 OUT` connector to the first panel's `IN`.
2. Connect the first panel's `OUT` to the second panel's `IN`.
3. Power each panel with a suitable 5 V supply. Keep the board and panel power
   grounds common. USB alone is not suitable for powering the panels.
4. Connect the board's USB-C port to the Mac with a data-capable cable.

The project uses the RGB-matrix board's documented HUB75 pin map and FM6126A
panel-driver initialization. It assumes both panels are 64×64 and use standard
scan wiring.

## Build and flash

Open this folder in VS Code. PlatformIO should offer to install its core tools
the first time. In the PlatformIO sidebar, select the
`waveshare_esp32_s3_rgb_matrix` environment, then choose **Upload**. Choose
**Monitor** to see the 115200-baud confirmation message.

If macOS does not show a USB serial port, hold **BOOT** while connecting USB,
release it after connection, upload, then press **RESET** to start the demo.

## Wi-Fi and wireless updates

The firmware starts a temporary Wi-Fi setup network named **LED-Plane-Setup**
when it has no saved network credentials. Join that network from a phone or
computer, complete the captive portal with your normal Wi-Fi credentials, and
the board will reconnect automatically on later boots.

After it joins Wi-Fi, open `http://10.0.0.25/` to see its local firmware-update
page. In the PlatformIO sidebar, select the
`waveshare_esp32_s3_rgb_matrix_ota` environment and click **Upload** to build
and update without connecting USB. Your development Mac and the ESP32 must be
on the same local network. If the router assigns the board a different address,
update `upload_port` in `platformio.ini` (or reserve `10.0.0.25` for the board
in the router).

For a private network this works without a password. Before using wireless
updates on a shared network, copy `include/local_secrets.example.h` to
`include/local_secrets.h`, replace its value with a long random password, and
perform one USB upload. That file is deliberately excluded from Git.

## Layout

`src/main.cpp` uses the full 128×64 canvas: two 64×64 panels chained
horizontally. To use a different panel size, update `kPanelWidth`,
`kPanelHeight`, and `kPanelChain` together.
