// Minimal ESP32-S3 starting point. Copy the pin mapping from the board/panel
// documentation before uploading; a 64x64 panel needs the E address line.
#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "../include/flight_ticket.hpp"
#include "hub75_canvas.hpp"

// These are intentionally not defaults. HUB75 GPIO wiring is board-specific.
// Set them to your verified adapter-board pin map before compiling.
#define R1_PIN -1
#define G1_PIN -1
#define B1_PIN -1
#define R2_PIN -1
#define G2_PIN -1
#define B2_PIN -1
#define A_PIN -1
#define B_PIN -1
#define C_PIN -1
#define D_PIN -1
#define E_PIN -1
#define LAT_PIN -1
#define OE_PIN -1
#define CLK_PIN -1

#if R1_PIN < 0 || G1_PIN < 0 || B1_PIN < 0 || R2_PIN < 0 || G2_PIN < 0 || B2_PIN < 0 || A_PIN < 0 || B_PIN < 0 || C_PIN < 0 || D_PIN < 0 || E_PIN < 0 || LAT_PIN < 0 || OE_PIN < 0 || CLK_PIN < 0
#error "Set the HUB75 pin mapping for your ESP32-S3 board before building."
#endif

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 64;
constexpr uint8_t kPanelChain = 2;  // Two physical 64x64 panels = 128x64.

HUB75_I2S_CFG::i2s_pins pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
HUB75_I2S_CFG config(kPanelWidth, kPanelHeight, kPanelChain, pins);
MatrixPanel_I2S_DMA panel(config);

void setup() {
  panel.begin();
  panel.setBrightness8(96);
  flight_display::Hub75Canvas canvas(panel);
  flight_display::FlightTicket ticket;  // Next: populate this from backend JSON.
  flight_display::render_flight_ticket(canvas, ticket);
}

void loop() {
  // DMA refreshes the panel independently. Poll the backend and redraw only
  // when the normalized presentation changes.
}
