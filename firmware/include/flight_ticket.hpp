#pragma once

// Platform-neutral application renderer. This is the code that belongs in the
// ESP32 project; it deliberately knows nothing about Wi-Fi, HTTP, HUB75 pins,
// or the Mac simulator.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace flight_display {

struct Color { uint8_t r, g, b; };

struct FlightTicket {
  std::string airline = "WESTJET";
  std::string number = "WS123";
  std::string origin = "YVR";
  std::string destination = "YEG";
  std::string aircraft_type = "B38M";
  uint32_t altitude_ft = 10000;
  uint32_t ground_speed_kt = 400;
  float distance_km = 42;
  bool approaching = true;
  float journey_progress = 0.64F;
  Color brand = {22, 91, 160};
};

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual void pixel(int x, int y, Color color) = 0;
  virtual void fill_rect(int x, int y, int width, int height, Color color) = 0;
};

inline void draw_glyph(Canvas& canvas, int x, int y, char letter, int scale, Color color) {
  // Compact 3x5 font: enough for flight codes and the status labels at 128x64.
  const char* rows = "00000";
  switch (letter) {
    case 'A': rows="010101111101101"; break; case 'B': rows="110101110101110"; break;
    case 'C': rows="011100100100011"; break; case 'D': rows="110101101101110"; break;
    case 'E': rows="111100110100111"; break; case 'F': rows="111100110100100"; break;
    case 'G': rows="011100101101011"; break; case 'H': rows="101101111101101"; break;
    case 'I': rows="111010010010111"; break; case 'J': rows="001001001101010"; break;
    case 'K': rows="101101110101101"; break; case 'L': rows="100100100100111"; break;
    case 'M': rows="101111111101101"; break; case 'N': rows="101111111111101"; break;
    case 'O': rows="010101101101010"; break; case 'P': rows="110101110100100"; break;
    case 'Q': rows="010101101111011"; break; case 'R': rows="110101110101101"; break;
    case 'S': rows="011100010001110"; break; case 'T': rows="111010010010010"; break;
    case 'U': rows="101101101101111"; break; case 'V': rows="101101101101010"; break;
    case 'W': rows="101101111111101"; break; case 'X': rows="101101010101101"; break;
    case 'Y': rows="101101010010010"; break; case 'Z': rows="111001010100111"; break;
    case '0': rows="111101101101111"; break; case '1': rows="010110010010111"; break;
    case '2': rows="110001111100111"; break; case '3': rows="110001011001110"; break;
    case '4': rows="101101111001001"; break; case '5': rows="111100110001110"; break;
    case '6': rows="011100111101010"; break; case '7': rows="111001010100100"; break;
    case '8': rows="010101010101010"; break; case '9': rows="010101011001110"; break;
    case '-': rows="000000111000000"; break; case ' ': return; default: rows="111101101101111";
  }
  for (int row = 0; row < 5; ++row)
    for (int col = 0; col < 3; ++col)
      if (rows[row * 3 + col] == '1') canvas.fill_rect(x + col * scale, y + row * scale, scale, scale, color);
}

inline void text(Canvas& canvas, int x, int y, std::string value, int scale, Color color, size_t limit) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  for (size_t i = 0; i < std::min(value.size(), limit); ++i) draw_glyph(canvas, x + static_cast<int>(i) * 4 * scale, y, value[i], scale, color);
}

inline void plane(Canvas& canvas, int x, int y, Color color) {
  const int points[][2] = {{0,3},{1,3},{2,3},{3,2},{4,1},{5,0},{5,3},{6,3},{7,4},{8,5},{6,4},{5,4},{4,6},{3,6},{3,4},{1,4}};
  for (const auto& point : points) canvas.pixel(x + point[0], y + point[1], color);
}

inline void render_flight_ticket(Canvas& canvas, const FlightTicket& ticket) {
  const Color white{255,255,255}, muted{145,154,170}, cyan{85,205,255}, green{184,228,137}, dark{42,49,60};
  canvas.fill_rect(0, 0, 128, 64, {0,0,0});
  canvas.fill_rect(0, 0, 128, 19, ticket.brand);
  canvas.fill_rect(4, 4, 16, 11, {240,248,255}); plane(canvas, 6, 6, {20,110,175});
  text(canvas, 24, 5, ticket.airline, 1, white, 16); text(canvas, 24, 12, ticket.number, 1, {198,231,255}, 16);
  plane(canvas, 103, 5, white); text(canvas, 112, 13, ticket.aircraft_type, 1, {219,234,254}, 4);
  const bool known_route = ticket.origin != "---" && ticket.destination != "---";
  if (known_route) {
    text(canvas, 4, 22, "FROM", 1, muted, 4); text(canvas, 109, 22, "TO", 1, muted, 2);
    text(canvas, 4, 30, ticket.origin, 2, white, 3); text(canvas, 103, 30, ticket.destination, 2, white, 3);
  } else text(canvas, 4, 28, ticket.aircraft_type, 2, white, 6);
  text(canvas, 4, 43, std::to_string(ticket.altitude_ft) + "FT", 1, cyan, 11);
  text(canvas, 72, 43, std::to_string(ticket.ground_speed_kt) + "KT", 1, cyan, 10);
  const std::string proximity = ticket.distance_km < 1 ? "OVERHEAD" : ticket.approaching ? "APPROACH" : "DEPART";
  text(canvas, 4, 50, std::to_string(static_cast<int>(ticket.distance_km)) + "KM " + proximity, 1, green, 16);
  const int filled = static_cast<int>(120 * std::clamp(ticket.journey_progress, 0.0F, 1.0F));
  canvas.fill_rect(4, 59, 120, 3, dark); canvas.fill_rect(4, 59, filled, 3, {63,191,242});
}

}  // namespace flight_display
