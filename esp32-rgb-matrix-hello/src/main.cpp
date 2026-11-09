#include <Arduino.h>
#include <ctype.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <qrcode.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <time.h>
#include <WiFiManager.h>
#include "airport_cities.h"
#include "air_canada_logo_32.h"
#include "air_canada_jazz_logo_32.h"
#include "flair_logo_32.h"
#include "canwest_air_logo_32.h"
#include "central_mountain_air_logo_32.h"
#include "porter_logo_32.h"
#include "sunwest_logo_32.h"
#include "stars_logo_32.h"
#include "klm_logo_32.h"
#include "westjet_logo_32.h"
#include "us_military_roundel_32.h"

#if __has_include("local_secrets.h")
#include "local_secrets.h"
#endif

#ifndef LED_PLANE_OTA_PASSWORD
#define LED_PLANE_OTA_PASSWORD ""
#endif

#ifndef LED_PLANE_BACKEND_URL
#define LED_PLANE_BACKEND_URL ""
#endif

#ifndef LED_PLANE_DEVICE_KEY
#define LED_PLANE_DEVICE_KEY ""
#endif

namespace {

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 64;
constexpr uint8_t kPanelChain = 2;
constexpr uint8_t kBootButtonPin = 0;
constexpr uint8_t kMaxRoutableFlights = 20;
constexpr unsigned long kFlightRotationMillis = 20000;
constexpr unsigned long kIndicatorTransitionMillis = 2000;
constexpr unsigned long kDisplayMaintenanceWipeMillis = 60UL * 60UL * 1000UL;
constexpr unsigned long kNoPlanesAnimationStepMillis = 125;
constexpr int16_t kNoPlanesSpriteWidth = 33;
constexpr int16_t kRotationIndicatorWidth = 12;
constexpr int16_t kRotationIndicatorHeight = 4;
constexpr unsigned long kRotationPipBubbleMillis = 600;
constexpr char kDeviceHostname[] = "led-plane";
constexpr char kSetupAccessPoint[] = "LED-Plane-Setup";

// Waveshare ESP32-S3-RGB-Matrix HUB75 wiring. These match Waveshare's
// Arduino examples for this exact driver board.
HUB75_I2S_CFG::i2s_pins kHub75Pins = {
    4,   // R1
    5,   // G1
    6,   // B1
    7,   // R2
    15,  // G2
    16,  // B2
    18,  // A
    8,   // B
    3,   // C
    42,  // D
    9,   // E
    40,  // LAT
    2,   // OE
    41,  // CLK
};

HUB75_I2S_CFG makeMatrixConfig() {
  HUB75_I2S_CFG config(kPanelWidth, kPanelHeight, kPanelChain, kHub75Pins);
  config.driver = HUB75_I2S_CFG::FM6126A;
  config.clkphase = false;
  return config;
}

HUB75_I2S_CFG matrixConfig = makeMatrixConfig();
MatrixPanel_I2S_DMA matrix(matrixConfig);
WebServer updateServer(80);
bool shouldRestart = false;
unsigned long nextFlightPollMillis = 0;
String pairingUrl;
unsigned long pairingUrlRefreshMillis = 0;
bool showingPairingQr = false;
volatile bool pairingQrRequestActive = false;
volatile bool pairingQrRequestFinished = false;
volatile bool pairingQrRequestSucceeded = false;
char pairingQrRequestUrl[256] = {};
unsigned long pairingQrSpinnerStartMillis = 0;
TaskHandle_t pairingQrTaskHandle = nullptr;
bool bootButtonWasPressed = false;
unsigned long lastBootButtonPressMillis = 0;
unsigned long nextFlightRotationMillis = 0;
unsigned long nextRouteAnimationMillis = 0;
unsigned long nextRotationIndicatorMillis = 0;
unsigned long routeSceneStartMillis = 0;
unsigned long rotationIndicatorResetStartMillis = 0;
unsigned long routeProgressAnimationStartMillis = 0;
bool rotationIndicatorResetActive = false;
bool routeProgressAnimationActive = false;
float routeProgressFrom = -1.0f;
float routeProgressTo = -1.0f;
unsigned long letterTransitionStartMillis = 0;
uint8_t letterTransitionRouteIndex = 0;
bool letterTransitionActive = false;
bool letterTransitionChromeSwapped = false;
bool flightRefreshDeferred = false;
bool rotationPipTransitionActive = false;
uint8_t rotationPipFrom = 0;
uint8_t rotationPipTo = 0;
unsigned long rotationPipTransitionStartMillis = 0;
unsigned long nextNoPlanesAnimationMillis = 0;
unsigned long lastDisplayMaintenanceWipeMillis = 0;
int16_t noPlanesSpriteX = -kNoPlanesSpriteWidth;
uint8_t noPlanesAircraftSprite = 0;
bool showingNoPlanes = false;
bool hideUnknownRoutes = false;

struct Route {
  char identity[16];
  char origin[8];
  char destination[8];
  char flightNumber[12];
  char operatorCode[8];
  char registration[12];
  int altFt;
  int speedKt;
  int verticalSpeedFpm;
  int headingDeg;
  char aircraftType[12];
  float distanceKm;
  float flightProgressPercent;
  bool pendingRemoval;
};

Route routes[kMaxRoutableFlights];
Route refreshedRoutes[kMaxRoutableFlights];
Route mergedRoutes[kMaxRoutableFlights];
uint8_t routeCount = 0;
uint8_t routeIndex = 0;

// A deliberately blocky 5x7 face: every glyph is exactly seven LEDs high.
// It keeps airport codes legible while giving the display a Minecraft-like look.
constexpr char kMinecraftGlyphs[] = "abcdefghijklmnopqrstuvwxyz0123456789/- ";
constexpr uint8_t kMinecraftFont[][7] PROGMEM = {
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0F}, {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
    // The zero is deliberately four pixels wide, preserving its angular
    // interior while giving dense telemetry a little more breathing room.
    {0x0C,0x12,0x12,0x16,0x1A,0x12,0x0C}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, {0x00,0x00,0x1F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

constexpr uint8_t kTelemetryUnitGlyphs[][5] PROGMEM = {
    {0x3,0x4,0x6,0x4,0x4},  // f
    {0x2,0x7,0x2,0x2,0x3},  // t
    {0x5,0x6,0x6,0x5,0x5},  // k
    {0x7,0x4,0x2,0x1,0x7},  // s
    {0x0,0x6,0x5,0x5,0x5},  // m
    {0x1,0x1,0x2,0x4,0x4},  // /
};

enum class TelemetryUnit : uint8_t { None, Feet, FeetPerMinute, Knots, Degrees, Kilometres };

uint8_t minecraftGlyphIndex(char character);

int8_t telemetryUnitGlyphIndex(char character) {
  switch (character) {
    case 'f': return 0; case 't': return 1; case 'k': return 2;
    case 's': return 3; case 'm': return 4; case '/': return 5;
    default: return -1;
  }
}

const char* telemetryUnitText(TelemetryUnit unit) {
  switch (unit) {
    case TelemetryUnit::Feet: return "ft";
    case TelemetryUnit::FeetPerMinute: return "f/m";
    case TelemetryUnit::Knots: return "kts";
    case TelemetryUnit::Kilometres: return "km";
    default: return "";
  }
}

// Values and unit labels use the same full 5x7 Minecraft face as the rest of
// the flight card. A decimal point is the one-pixel mark it visually is, not
// an empty five-pixel character cell between two digits.
int16_t telemetryNumberWidth(const char* text) {
  int16_t width = 0;
  for (const char* character = text; *character; ++character) {
    width += *character == '.' ? 2 : (*character == '0' ? 5 : 6);
  }
  return width ? width - 1 : 0;
}

int16_t telemetryUnitWidth(TelemetryUnit unit) {
  if (unit == TelemetryUnit::None) return 0;
  if (unit == TelemetryUnit::Degrees) return 3;
  if (unit == TelemetryUnit::Feet) return 1;  // compact apostrophe
  if (unit == TelemetryUnit::Knots) return 14;  // k(5) + trimmed t(4) + s(5)
  return telemetryNumberWidth(telemetryUnitText(unit));
}

int16_t telemetryValueWidth(const char* value, TelemetryUnit unit) {
  const int16_t numberWidth = telemetryNumberWidth(value);
  return numberWidth + (unit == TelemetryUnit::None ? 0 : 1 + telemetryUnitWidth(unit));
}

int16_t telemetryLineWidth(const char* value, TelemetryUnit unit, bool directionArrow) {
  // The level-flight marker is the native six-wide route arrow; reserving its
  // full width also prevents clipping when a climb/descent becomes level.
  return telemetryValueWidth(value, unit) + (directionArrow ? 6 : 0);
}

int16_t telemetryMarqueeOffset(int16_t contentWidth, int16_t slotWidth) {
  if (contentWidth <= slotWidth) return 0;
  const int16_t travel = contentWidth - slotWidth;
  const unsigned long oneWay = travel * 75UL;
  const unsigned long phase = (millis() - routeSceneStartMillis) % (4000UL + oneWay * 2UL);
  if (phase < 2000UL) return 0;
  if (phase < 2000UL + oneWay) return (phase - 2000UL) / 75UL;
  if (phase < 4000UL + oneWay) return travel;
  return travel - (phase - 4000UL - oneWay) / 75UL;
}

void drawTelemetryPixel(int16_t x, int16_t y, uint16_t color, int16_t left, int16_t right) {
  if (x >= left && x < right) matrix.drawPixel(x, y, color);
}

int16_t drawMinecraftTelemetryNumber(const char* text, int16_t x, int16_t y, uint16_t color,
                                     int16_t left, int16_t right) {
  for (const char* character = text; *character; ++character) {
    if (*character == '.') {
      drawTelemetryPixel(x, y + 6, color, left, right);
      x += 2;
      continue;
    }
    const uint8_t glyph = minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        if (pixels & (1 << (4 - column))) drawTelemetryPixel(x + column, y + row, color, left, right);
      }
    }
    x += *character == '0' ? 5 : 6;
  }
  return x;
}

int16_t drawTelemetryUnit(TelemetryUnit unit, int16_t x, int16_t y, uint16_t color,
                          int16_t left, int16_t right) {
  if (unit == TelemetryUnit::Degrees) {
    // 3x3 hollow degree diamond:
    // oxo
    // xox
    // oxo
    drawTelemetryPixel(x + 1, y, color, left, right);
    drawTelemetryPixel(x, y + 1, color, left, right);
    drawTelemetryPixel(x + 2, y + 1, color, left, right);
    drawTelemetryPixel(x + 1, y + 2, color, left, right);
    return x + 3;
  }
  if (unit == TelemetryUnit::Feet) {
    // A proper compact feet apostrophe: two pixels tall, aligned to the cap
    // height rather than floating down beside the numeral.
    drawTelemetryPixel(x, y, color, left, right);
    drawTelemetryPixel(x, y + 1, color, left, right);
    return x + 1;
  }
  if (unit == TelemetryUnit::Knots) {
    // A compact, contiguous `kts`: the t's rightmost hook is removed so its
    // four-pixel cell can sit directly between the k and s.
    static constexpr uint8_t kGlyphIndices[] = {10, 19, 18};  // k, t, s
    static constexpr uint8_t kWidths[] = {5, 4, 5};
    for (uint8_t glyphNumber = 0; glyphNumber < 3; ++glyphNumber) {
      for (uint8_t row = 0; row < 7; ++row) {
        uint8_t pixels = pgm_read_byte(&kMinecraftFont[kGlyphIndices[glyphNumber]][row]);
        if (glyphNumber == 1 && row == 5) pixels &= 0x1E;  // remove t curl
        // This is deliberately local to the compact speed-unit sprite: its
        // final s loses the bottom-left pixel, so it stays visually separate
        // from the adjacent t without changing the normal Minecraft s.
        if (glyphNumber == 2 && row == 6) pixels &= 0x0F;
        for (uint8_t column = 0; column < kWidths[glyphNumber]; ++column) {
          if (pixels & (1 << (4 - column))) drawTelemetryPixel(x + column, y + row, color, left, right);
        }
      }
      x += kWidths[glyphNumber];
    }
    return x;
  }
  return drawMinecraftTelemetryNumber(telemetryUnitText(unit), x, y, color, left, right);
}

int16_t drawTelemetryValue(const char* value, TelemetryUnit unit, int16_t x, int16_t y,
                           uint16_t color, int16_t left, int16_t right) {
  const int16_t numberEnd = drawMinecraftTelemetryNumber(value, x, y, color, left, right);
  if (unit == TelemetryUnit::None) return numberEnd;
  return drawTelemetryUnit(unit, numberEnd, y, color, left, right);
}

// Airport identifiers use a separate, heavier capital face. It is still
// exactly seven LEDs tall, but the broader strokes make the codes the focus.
constexpr uint8_t kMinecraftBoldCaps[][7] PROGMEM = {
    {0x0E,0x1B,0x1B,0x1F,0x1B,0x1B,0x1B}, {0x1E,0x1B,0x1B,0x1E,0x1B,0x1B,0x1E},
    {0x0E,0x1B,0x18,0x18,0x18,0x1B,0x0E}, {0x1E,0x1B,0x1B,0x1B,0x1B,0x1B,0x1E},
    {0x1F,0x18,0x18,0x1E,0x18,0x18,0x1F}, {0x1F,0x18,0x18,0x1E,0x18,0x18,0x18},
    {0x0F,0x1A,0x18,0x1B,0x1B,0x1B,0x0F}, {0x1B,0x1B,0x1B,0x1F,0x1B,0x1B,0x1B},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, {0x07,0x02,0x02,0x02,0x1A,0x1A,0x0E},
    {0x1B,0x1B,0x1A,0x1C,0x1A,0x1B,0x1B}, {0x18,0x18,0x18,0x18,0x18,0x18,0x1F},
    {0x11,0x1B,0x1F,0x15,0x11,0x11,0x11}, {0x1B,0x1F,0x1F,0x1B,0x1B,0x1B,0x1B},
    {0x0E,0x1B,0x1B,0x1B,0x1B,0x1B,0x0E}, {0x1E,0x1B,0x1B,0x1E,0x18,0x18,0x18},
    {0x0E,0x1B,0x1B,0x1B,0x1F,0x0E,0x03}, {0x1E,0x1B,0x1B,0x1E,0x1A,0x1B,0x1B},
    {0x0F,0x18,0x0E,0x03,0x03,0x1B,0x0F}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x1B,0x1B,0x1B,0x1B,0x1B,0x1B,0x0E}, {0x1B,0x1B,0x1B,0x1B,0x1B,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x1F,0x1B,0x11}, {0x1B,0x1B,0x0E,0x04,0x0E,0x1B,0x1B},
    {0x1F,0x0E,0x0E,0x0C,0x0C,0x0C,0x0C}, {0x1F,0x03,0x06,0x0C,0x18,0x18,0x1F},
};

// Regular all-caps companion to the lowercase 7px body face. Airport codes
// alone use kMinecraftBoldCaps; telemetry and marquee text use this lighter set.
constexpr uint8_t kMinecraftNormalCaps[][7] PROGMEM = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

// The capital Y deliberately breaks the usual 5-pixel width: its 6-pixel
// silhouette follows the exact bitmap supplied for the airport-code face.
constexpr uint8_t kMinecraftBoldY[7] PROGMEM = {
    0x33, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
};

// A compact supporting face for live telemetry. Airport codes intentionally
// remain the only large all-capital text on the flight card.
constexpr char kTinyGlyphs[] = "abcdefghijklmnopqrstuvwxyz0123456789.- ";
constexpr uint8_t kTinyFont[][5] PROGMEM = {
    {0x0,0x6,0x1,0x7,0x7}, {0x4,0x6,0x5,0x5,0x6}, {0x0,0x3,0x4,0x4,0x3},
    {0x1,0x3,0x5,0x5,0x3}, {0x0,0x2,0x5,0x7,0x3}, {0x3,0x4,0x6,0x4,0x4},
    {0x0,0x3,0x5,0x3,0x6}, {0x4,0x6,0x5,0x5,0x5}, {0x2,0x0,0x2,0x2,0x2},
    {0x1,0x0,0x1,0x5,0x2}, {0x4,0x5,0x6,0x5,0x5}, {0x6,0x2,0x2,0x2,0x7},
    {0x0,0x6,0x7,0x5,0x5}, {0x0,0x6,0x5,0x5,0x5}, {0x0,0x2,0x5,0x5,0x2},
    {0x0,0x6,0x5,0x6,0x4}, {0x0,0x3,0x5,0x3,0x1}, {0x0,0x5,0x6,0x4,0x4},
    {0x0,0x3,0x6,0x1,0x6}, {0x2,0x7,0x2,0x2,0x3}, {0x0,0x5,0x5,0x5,0x3},
    {0x0,0x5,0x5,0x5,0x2}, {0x0,0x5,0x5,0x7,0x2}, {0x0,0x5,0x2,0x2,0x5},
    {0x0,0x5,0x5,0x3,0x6}, {0x0,0x7,0x2,0x4,0x7}, {0x2,0x5,0x5,0x5,0x2},
    {0x2,0x6,0x2,0x2,0x7}, {0x6,0x1,0x2,0x4,0x7}, {0x6,0x1,0x2,0x1,0x6},
    {0x1,0x3,0x5,0x7,0x1}, {0x7,0x4,0x6,0x1,0x6}, {0x3,0x4,0x6,0x5,0x2},
    {0x7,0x1,0x2,0x2,0x2}, {0x2,0x5,0x2,0x5,0x2}, {0x2,0x5,0x3,0x1,0x6},
    {0x0,0x0,0x0,0x0,0x2}, {0x0,0x0,0x7,0x0,0x0}, {0x0,0x0,0x0,0x0,0x0},
};

uint8_t minecraftGlyphIndex(char character) {
  if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
  const char* found = strchr(kMinecraftGlyphs, character);
  return found ? static_cast<uint8_t>(found - kMinecraftGlyphs) : 38;
}

void drawMinecraftCentered(const char* text, int16_t y, uint16_t color) {
  const int16_t width = strlen(text) * 6 - 1;
  int16_t x = (matrix.width() - width) / 2;
  for (const char* character = text; *character; ++character, x += 6) {
    const uint8_t glyph = minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        if (pixels & (1 << (4 - column))) matrix.drawPixel(x + column, y + row, color);
      }
    }
  }
}

void drawMinecraftMarquee(const char* text, int16_t left, int16_t y, int16_t width,
                          uint16_t color) {
  const int16_t textWidth = strlen(text) * 6 - 1;
  int16_t offset = 0;
  if (textWidth > width) {
    const int16_t travel = textWidth - width;
    const unsigned long oneWay = travel * 75UL;
    const unsigned long phase = (millis() - routeSceneStartMillis) % (4000UL + oneWay * 2UL);
    if (phase < 2000UL) offset = 0;
    else if (phase < 2000UL + oneWay) offset = (phase - 2000UL) / 75UL;
    else if (phase < 4000UL + oneWay) offset = travel;
    else offset = travel - (phase - 4000UL - oneWay) / 75UL;
  }
  int16_t x = left - offset;
  for (const char* character = text; *character; ++character, x += 6) {
    const bool uppercase = *character >= 'A' && *character <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(*character - 'A')
                                    : minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (4 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, y + row, color);
        }
      }
    }
  }
}

// Six-wide city-route arrow, intentionally distinct from the seven-wide
// airport-code arrow above.
constexpr uint8_t kCityArrowSprite[5] = {0x04, 0x02, 0x3F, 0x02, 0x04};

void drawCityArrow(int16_t x, int16_t y, int16_t left, int16_t right, uint16_t color) {
  for (uint8_t row = 0; row < 5; ++row) {
    for (uint8_t column = 0; column < 6; ++column) {
      const int16_t pixelX = x + column;
      if ((kCityArrowSprite[row] & (1 << (5 - column))) && pixelX >= left && pixelX < right) {
        matrix.drawPixel(pixelX, y + row + 1, color);
      }
    }
  }
}

// Reuse the route arrow for vertical speed rather than introducing a second
// visual language. The regular arrow points right; a counter-clockwise turn
// points up (climb), while a clockwise turn points down (descent).
void drawVerticalRouteArrow(int16_t x, int16_t y, bool climbing, uint16_t color,
                            int16_t left, int16_t right) {
  for (uint8_t sourceRow = 0; sourceRow < 5; ++sourceRow) {
    for (uint8_t sourceColumn = 0; sourceColumn < 6; ++sourceColumn) {
      if (!(kCityArrowSprite[sourceRow] & (1 << (5 - sourceColumn)))) continue;
      const int16_t pixelX = x + (climbing ? sourceRow : 4 - sourceRow);
      const int16_t pixelY = y + (climbing ? 5 - sourceColumn : sourceColumn);
      if (pixelX >= left && pixelX < right) matrix.drawPixel(pixelX, pixelY, color);
    }
  }
}

int16_t normalTextWidth(const char* text) {
  return *text ? static_cast<int16_t>(strlen(text) * 6 - 1) : 0;
}

enum class CityAccent : uint8_t { None, Acute, Grave, Circumflex, Tilde, Diaeresis, Ring, Cedilla };
struct CityGlyph { char base; uint8_t bytes; CityAccent accent; };

// Airport data is UTF-8, while the display font is intentionally one-byte.
// Decode common Latin-1 airport-name letters into a normal glyph plus its
// tiny pixel accent so a name like Cancún stays a six-letter word.
CityGlyph nextCityGlyph(const char* text) {
  const uint8_t first = static_cast<uint8_t>(text[0]);
  if (first < 0x80) return {static_cast<char>(first), 1, CityAccent::None};
  const uint8_t second = static_cast<uint8_t>(text[1]);
  if (first != 0xC3 || second == 0) return {'-', static_cast<uint8_t>((first & 0xE0) == 0xC0 ? 2 : 1), CityAccent::None};
  const bool upper = second >= 0x80 && second <= 0x9F;
  const uint8_t normalized = upper ? second + 0x20 : second;
  const char base =
      (normalized >= 0xA0 && normalized <= 0xA5) ? 'a' :
      normalized == 0xA7 ? 'c' :
      (normalized >= 0xA8 && normalized <= 0xAB) ? 'e' :
      (normalized >= 0xAC && normalized <= 0xAF) ? 'i' :
      normalized == 0xB1 ? 'n' :
      (normalized >= 0xB2 && normalized <= 0xB6) ? 'o' :
      (normalized >= 0xB9 && normalized <= 0xBC) ? 'u' :
      (normalized == 0xBD || normalized == 0xBF) ? 'y' : '-';
  const CityAccent accent =
      (normalized == 0xA1 || normalized == 0xA9 || normalized == 0xAD || normalized == 0xB3 || normalized == 0xBA || normalized == 0xBD) ? CityAccent::Acute :
      (normalized == 0xA0 || normalized == 0xA8 || normalized == 0xAC || normalized == 0xB2 || normalized == 0xB9) ? CityAccent::Grave :
      (normalized == 0xA2 || normalized == 0xAA || normalized == 0xAE || normalized == 0xB4 || normalized == 0xBB) ? CityAccent::Circumflex :
      (normalized == 0xA3 || normalized == 0xB1 || normalized == 0xB5) ? CityAccent::Tilde :
      (normalized == 0xA4 || normalized == 0xAB || normalized == 0xAF || normalized == 0xB6 || normalized == 0xBC || normalized == 0xBF) ? CityAccent::Diaeresis :
      normalized == 0xA5 ? CityAccent::Ring : normalized == 0xA7 ? CityAccent::Cedilla : CityAccent::None;
  return {static_cast<char>(upper ? toupper(base) : base), 2, accent};
}

uint8_t cityGlyphCount(const char* text) {
  uint8_t count = 0;
  for (const char* cursor = text; *cursor; cursor += nextCityGlyph(cursor).bytes) ++count;
  return count;
}

int16_t cityTextWidth(const char* text) {
  const uint8_t count = cityGlyphCount(text);
  return count ? count * 6 - 1 : 0;
}

void drawCityAccent(CityAccent accent, int16_t x, int16_t y, int16_t left, int16_t right, uint16_t color) {
  const auto pixel = [&](int8_t dx, int8_t dy) { if (x + dx >= left && x + dx < right) matrix.drawPixel(x + dx, y + dy, color); };
  switch (accent) {
    case CityAccent::Acute: pixel(3, 0); pixel(2, 1); break;
    case CityAccent::Grave: pixel(2, 0); pixel(3, 1); break;
    case CityAccent::Circumflex: pixel(2, 0); pixel(1, 1); pixel(3, 1); break;
    case CityAccent::Tilde: pixel(1, 0); pixel(2, 1); pixel(3, 0); break;
    case CityAccent::Diaeresis: pixel(1, 0); pixel(3, 0); break;
    case CityAccent::Ring: pixel(2, 0); pixel(1, 1); pixel(3, 1); break;
    case CityAccent::Cedilla: pixel(2, 6); break;
    default: break;
  }
}

void drawCityText(const char* text, int16_t x, int16_t y, int16_t left, int16_t right, uint16_t color) {
  for (const char* cursor = text; *cursor;) {
    const CityGlyph glyphInfo = nextCityGlyph(cursor);
    const bool uppercase = glyphInfo.base >= 'A' && glyphInfo.base <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(glyphInfo.base - 'A') : minecraftGlyphIndex(glyphInfo.base);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        if ((pixels & (1 << (4 - column))) && x + column >= left && x + column < right) matrix.drawPixel(x + column, y + row, color);
      }
    }
    drawCityAccent(glyphInfo.accent, x, y, left, right, color);
    x += 6;
    cursor += glyphInfo.bytes;
  }
}

void drawCityMarquee(const char* origin, const char* destination, int16_t left, int16_t y,
                     int16_t width, uint16_t color) {
  const int16_t originWidth = cityTextWidth(origin);
  const int16_t textWidth = originWidth + 2 + 6 + 2 + cityTextWidth(destination);
  int16_t offset = 0;
  if (textWidth > width) {
    const int16_t travel = textWidth - width;
    const unsigned long oneWay = travel * 75UL;
    const unsigned long phase = (millis() - routeSceneStartMillis) % (4000UL + oneWay * 2UL);
    if (phase < 2000UL) offset = 0;
    else if (phase < 2000UL + oneWay) offset = (phase - 2000UL) / 75UL;
    else if (phase < 4000UL + oneWay) offset = travel;
    else offset = travel - (phase - 4000UL - oneWay) / 75UL;
  }
  const int16_t x = left - offset;
  drawCityText(origin, x, y, left, left + width, color);
  const int16_t arrowX = x + originWidth + 2;
  drawCityArrow(arrowX, y, left, left + width, color);
  const int16_t destinationX = arrowX + 8;
  drawCityText(destination, destinationX, y, left, left + width, color);
}

void drawRegistrationMarquee(const char* text, int16_t left, int16_t y, int16_t width,
                             uint16_t color) {
  int16_t textWidth = -1;
  for (const char* character = text; *character; ++character) {
    textWidth += (*character == '-' ? 2 : 5) + 1;
  }
  int16_t offset = 0;
  if (textWidth > width) {
    const int16_t travel = textWidth - width;
    const unsigned long oneWay = travel * 75UL;
    const unsigned long phase = (millis() - routeSceneStartMillis) % (4000UL + oneWay * 2UL);
    if (phase < 2000UL) offset = 0;
    else if (phase < 2000UL + oneWay) offset = (phase - 2000UL) / 75UL;
    else if (phase < 4000UL + oneWay) offset = travel;
    else offset = travel - (phase - 4000UL - oneWay) / 75UL;
  }
  int16_t x = left - offset;
  for (const char* character = text; *character;) {
    if (*character == '-') {
      if (x >= left - 1 && x < left + width) matrix.fillRect(x, y + 3, 2, 1, color);
      x += 3;
      ++character;
      continue;
    }
    const bool uppercase = *character >= 'A' && *character <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(*character - 'A')
                                    : minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (4 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, y + row, color);
        }
      }
    }
    x += 6;
    ++character;
  }
}

void drawTinyText(const char* text, int16_t x, int16_t y, uint16_t color) {
  for (const char* character = text; *character; ++character, x += 4) {
    char lower = *character;
    if (lower >= 'A' && lower <= 'Z') lower += 'a' - 'A';
    const char* found = strchr(kTinyGlyphs, lower);
    const uint8_t glyph = found ? static_cast<uint8_t>(found - kTinyGlyphs) : 38;
    for (uint8_t row = 0; row < 5; ++row) {
      const uint8_t pixels = pgm_read_byte(&kTinyFont[glyph][row]);
      for (uint8_t column = 0; column < 3; ++column) {
        if (pixels & (1 << (2 - column))) matrix.drawPixel(x + column, y + row, color);
      }
    }
  }
}

int16_t drawMinecraftBoldCaps(const char* text, int16_t x, int16_t y, uint16_t color) {
  for (const char* character = text; *character; ++character) {
    if (*character == '-') {
      matrix.fillRect(x, y + 3, 5, 1, color);
      x += 6;
      continue;
    }
    const bool isY = *character == 'Y';
    const uint8_t glyph = (*character >= 'A' && *character <= 'Z')
                              ? static_cast<uint8_t>(*character - 'A')
                              : 0;
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = isY ? pgm_read_byte(&kMinecraftBoldY[row])
                                 : pgm_read_byte(&kMinecraftBoldCaps[glyph][row]);
      const uint8_t glyphWidth = isY ? 6 : 5;
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if (pixels & (1 << (glyphWidth - 1 - column))) {
          matrix.drawPixel(x + column, y + row, color);
        }
      }
    }
    x += (isY ? 6 : 5) + 1;
  }
  return x - 1;
}

void drawMinecraftBoldCapsCentered(const char* text, int16_t y, uint16_t color) {
  int16_t width = -1;
  for (const char* character = text; *character;) width += (*character++ == 'Y' ? 6 : 5) + 1;
  drawMinecraftBoldCaps(text, (matrix.width() - width) / 2, y, color);
}

uint16_t visibleColor(uint8_t red, uint8_t green, uint8_t blue) {
  // This panel's red/blue channels are physically swapped.
  return matrix.color565(blue, green, red);
}

void drawWestJetLogo(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 32; ++row) for (uint8_t column = 0; column < 32; ++column) {
    const uint8_t index = pgm_read_byte(&kWestJetLogoPixels[row][column]) - '0';
    if (!index) continue;
    const uint8_t red = pgm_read_byte(&kWestJetLogoPalette[index][0]);
    const uint8_t green = pgm_read_byte(&kWestJetLogoPalette[index][1]);
    const uint8_t blue = pgm_read_byte(&kWestJetLogoPalette[index][2]);
    matrix.drawPixel(x + column, y + row, matrix.color565(red, green, blue));
  }
}

bool isAirCanadaFlight(const Route& route) {
  // Flight feeds use either the IATA prefix (AC) or the ICAO prefix (ACA).
  return strncmp(route.operatorCode, "ACA", 3) == 0 || strncmp(route.flightNumber, "AC", 2) == 0;
}

bool isAirCanadaJazzFlight(const Route& route) {
  return strncmp(route.operatorCode, "JZA", 3) == 0 || strncmp(route.flightNumber, "JZA", 3) == 0;
}

bool isFlairFlight(const Route& route) {
  return strncmp(route.operatorCode, "FLE", 3) == 0 || strncmp(route.flightNumber, "FLE", 3) == 0;
}

bool isWestJetFlight(const Route& route) {
  return strncmp(route.operatorCode, "WJA", 3) == 0 || strncmp(route.operatorCode, "WEN", 3) == 0 ||
         strncmp(route.flightNumber, "WS", 2) == 0;
}

bool isCanWestFlight(const Route& route) {
  return strncmp(route.operatorCode, "CWA", 3) == 0 || strncmp(route.flightNumber, "CWA", 3) == 0;
}

bool isCentralMountainAirFlight(const Route& route) {
  return strncmp(route.operatorCode, "GLR", 3) == 0 || strncmp(route.flightNumber, "GLR", 3) == 0;
}

bool isPorterFlight(const Route& route) {
  return strncmp(route.operatorCode, "POE", 3) == 0 || strncmp(route.flightNumber, "POE", 3) == 0;
}

bool isSunwestFlight(const Route& route) {
  return strncmp(route.operatorCode, "CNK", 3) == 0 || strncmp(route.flightNumber, "CNK", 3) == 0;
}

bool isStarsFlight(const Route& route) {
  return strncmp(route.operatorCode, "STAR", 4) == 0 || strncmp(route.flightNumber, "STAR", 4) == 0;
}

bool isKlmFlight(const Route& route) {
  return strncmp(route.operatorCode, "KLM", 3) == 0 || strncmp(route.flightNumber, "KL", 2) == 0;
}

bool isUsMilitaryFlight(const Route& route) {
  return strncmp(route.operatorCode, "USM", 3) == 0 || strncmp(route.operatorCode, "RCH", 3) == 0 ||
         strncmp(route.operatorCode, "CNV", 3) == 0 || strncmp(route.operatorCode, "GKA", 3) == 0 ||
         strncmp(route.flightNumber, "USM", 3) == 0 || strncmp(route.flightNumber, "RCH", 3) == 0;
}

bool isOperator(const Route& route, const char* code) {
  return strncmp(route.operatorCode, code, 3) == 0 || strncmp(route.flightNumber, code, 3) == 0;
}

const char* airlineName(const Route& route) {
  if (isWestJetFlight(route)) return strncmp(route.operatorCode, "WEN", 3) == 0 ||
                                      strncmp(route.flightNumber, "WEN", 3) == 0
                                      ? "WestJet Encore" : "WestJet";
  if (isAirCanadaJazzFlight(route)) return "Air Canada Jazz";
  if (isAirCanadaFlight(route)) return "Air Canada";
  if (isFlairFlight(route)) return "Flair Airlines";
  if (isPorterFlight(route)) return "Porter";
  if (isCanWestFlight(route)) return "CanWest Air";
  if (isCentralMountainAirFlight(route)) return "Central Mountain Air";
  if (isSunwestFlight(route)) return "Sunwest Aviation";
  if (isStarsFlight(route)) return "Stars Air Ambulance";
  if (isKlmFlight(route)) return "KLM";
  if (isUsMilitaryFlight(route)) return "United States Military";
  return route.operatorCode[0] ? route.operatorCode : "";
}

void operatorBrandRgb(const Route& route, uint8_t& red, uint8_t& green, uint8_t& blue) {
  if (isAirCanadaJazzFlight(route) || isAirCanadaFlight(route)) {
    red = kAirCanadaBrandColor[0]; green = kAirCanadaBrandColor[1]; blue = kAirCanadaBrandColor[2];
  } else if (isFlairFlight(route)) {
    red = kFlairBrandColor[0]; green = kFlairBrandColor[1]; blue = kFlairBrandColor[2];
  } else if (isCanWestFlight(route)) {
    red = kCanWestBrandColor[0]; green = kCanWestBrandColor[1]; blue = kCanWestBrandColor[2];
  } else if (isCentralMountainAirFlight(route)) {
    red = kCentralMountainAirBrandColor[0]; green = kCentralMountainAirBrandColor[1]; blue = kCentralMountainAirBrandColor[2];
  } else if (isPorterFlight(route)) {
    red = kPorterBrandColor[0]; green = kPorterBrandColor[1]; blue = kPorterBrandColor[2];
  } else if (isSunwestFlight(route)) {
    red = kSunwestBrandColor[0]; green = kSunwestBrandColor[1]; blue = kSunwestBrandColor[2];
  } else if (isStarsFlight(route)) {
    red = kStarsBrandColor[0]; green = kStarsBrandColor[1]; blue = kStarsBrandColor[2];
  } else if (isKlmFlight(route)) {
    red = kKlmBrandColor[0]; green = kKlmBrandColor[1]; blue = kKlmBrandColor[2];
  } else if (isUsMilitaryFlight(route)) {
    red = 0; green = 40; blue = 104;  // Roundel navy.
  } else if (isWestJetFlight(route)) {
    red = 0; green = 170; blue = 166;  // WestJet teal, #00AAA6.
  } else {
    red = 255; green = 255; blue = 255;  // Unknown operator: neutral ticket.
  }
}

void drawAirCanadaLogo(int16_t x, int16_t y) {
  // The HUB75 panel has red and blue physically swapped. Keep the stored brand
  // value conventional (#F01428), then swap only at panel-output time.
  const uint16_t red = matrix.color565(kAirCanadaBrandColor[2], kAirCanadaBrandColor[1],
                                       kAirCanadaBrandColor[0]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kAirCanadaLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) matrix.drawPixel(x + column, y + row, red);
    }
  }
}

void drawAirCanadaJazzLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kAirCanadaBrandColor[0], kAirCanadaBrandColor[1],
                                    kAirCanadaBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kAirCanadaJazzLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) matrix.drawPixel(x + column, y + row, red);
    }
  }
}

void drawFlairLogo(int16_t x, int16_t y) {
  const uint16_t green = visibleColor(kFlairLogoGreen[0], kFlairLogoGreen[1], kFlairLogoGreen[2]);
  const uint16_t black = matrix.color565(0, 0, 0);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t greenPixels = pgm_read_dword(&kFlairLogoMask[row][0]);
    const uint32_t blackPixels = pgm_read_dword(&kFlairLogoMask[row][1]);
    for (uint8_t column = 0; column < 32; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      if (blackPixels & bit) matrix.drawPixel(x + column, y + row, black);
      else if (greenPixels & bit) matrix.drawPixel(x + column, y + row, green);
    }
  }
}

void drawCanWestLogo(int16_t x, int16_t y) {
  const uint16_t orange = visibleColor(kCanWestBrandColor[0], kCanWestBrandColor[1],
                                       kCanWestBrandColor[2]);
  const uint16_t blue = visibleColor(kCanWestWordmarkColor[0], kCanWestWordmarkColor[1],
                                     kCanWestWordmarkColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t orangePixels = pgm_read_dword(&kCanWestLogoMask[row][0]);
    const uint32_t bluePixels = pgm_read_dword(&kCanWestLogoMask[row][1]);
    for (uint8_t column = 0; column < 32; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      if (orangePixels & bit) matrix.drawPixel(x + column, y + row, orange);
      else if (bluePixels & bit) matrix.drawPixel(x + column, y + row, blue);
    }
  }
}

void drawCentralMountainAirLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kCentralMountainAirBrandColor[0],
                                    kCentralMountainAirBrandColor[1],
                                    kCentralMountainAirBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kCentralMountainAirLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) matrix.drawPixel(x + column, y + row, red);
    }
  }
}

void drawPorterLogo(int16_t x, int16_t y) {
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t blue = visibleColor(kPorterBrandColor[0], kPorterBrandColor[1],
                                     kPorterBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t whitePixels = pgm_read_dword(&kPorterLogoMask[row][0]);
    const uint32_t bluePixels = pgm_read_dword(&kPorterLogoMask[row][1]);
    // The owl is bilaterally symmetrical. Mirror the authored left half at
    // render time so a one-pixel rasterisation wobble can never skew its face.
    for (uint8_t column = 0; column < 16; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      const uint16_t color = whitePixels & bit ? white : bluePixels & bit ? blue : 0;
      if (color) {
        matrix.drawPixel(x + column, y + row, color);
        matrix.drawPixel(x + 31 - column, y + row, color);
      }
    }
  }
}

void drawSunwestLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kSunwestBrandColor[0], kSunwestBrandColor[1],
                                    kSunwestBrandColor[2]);
  const uint16_t navy = visibleColor(kSunwestNavyColor[0], kSunwestNavyColor[1],
                                     kSunwestNavyColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t redPixels = pgm_read_dword(&kSunwestLogoMask[row][0]);
    const uint32_t navyPixels = pgm_read_dword(&kSunwestLogoMask[row][1]);
    for (uint8_t column = 0; column < 32; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      if (redPixels & bit) matrix.drawPixel(x + column, y + row, red);
      else if (navyPixels & bit) matrix.drawPixel(x + column, y + row, navy);
    }
  }
}

void drawStarsLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kStarsBrandColor[0], kStarsBrandColor[1],
                                    kStarsBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kStarsLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) matrix.drawPixel(x + column, y + row, red);
    }
  }
}

void drawKlmLogo(int16_t x, int16_t y) {
  const uint16_t blue = visibleColor(kKlmBrandColor[0], kKlmBrandColor[1], kKlmBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kKlmLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) matrix.drawPixel(x + column, y + row, blue);
    }
  }
}

void drawUsMilitaryLogo(int16_t x, int16_t y) {
  const uint16_t colors[3] = {visibleColor(5, 25, 105), matrix.color565(255, 255, 255), visibleColor(220, 20, 30)};
  for (uint8_t row = 0; row < 32; ++row) for (uint8_t column = 0; column < 32; ++column) {
    const uint32_t bit = 1UL << (31 - column);
    for (uint8_t color = 0; color < 3; ++color) {
      if (pgm_read_dword(&kUsMilitaryRoundelMask[row][color]) & bit) { matrix.drawPixel(x + column, y + row, colors[color]); break; }
    }
  }
}

void drawPlaneSprite(int16_t x, int16_t y, uint16_t color);
void drawPlaneSpritePointingUp(int16_t x, int16_t y, uint16_t color);
void drawAircraftFallbackSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color);

void drawOperatorLogo(const Route& route, int16_t x, int16_t y) {
  if (isAirCanadaJazzFlight(route)) drawAirCanadaJazzLogo(x, y);
  else if (isAirCanadaFlight(route)) drawAirCanadaLogo(x, y);
  else if (isFlairFlight(route)) drawFlairLogo(x, y);
  else if (isCanWestFlight(route)) drawCanWestLogo(x, y);
  else if (isCentralMountainAirFlight(route)) drawCentralMountainAirLogo(x, y);
  else if (isPorterFlight(route)) drawPorterLogo(x, y);
  else if (isSunwestFlight(route)) drawSunwestLogo(x, y);
  else if (isStarsFlight(route)) drawStarsLogo(x, y);
  else if (isKlmFlight(route)) drawKlmLogo(x, y);
  else if (isUsMilitaryFlight(route)) drawUsMilitaryLogo(x, y);
  else if (isWestJetFlight(route)) drawWestJetLogo(x, y);
  // The unknown-airline badge deliberately uses the left and bottom one-pixel
  // gutters: 33px at x=0..32 and y=3..35. It is a quiet 25% white aircraft
  // silhouette selected from the incoming ICAO aircraft type.
  else drawAircraftFallbackSprite(route.aircraftType, x - 1, y, matrix.color565(64, 64, 64));
}

float easeInOut(float progress) {
  progress = constrain(progress, 0.0f, 1.0f);
  return progress * progress * (3.0f - 2.0f * progress);
}

bool hasFlightProgress(float progress) {
  return progress >= 0.0f && progress <= 100.0f;
}

struct RouteProgressFrame {
  float percent;
  float opacity;
};

RouteProgressFrame animatedRouteProgress() {
  if (!routeProgressAnimationActive) return {routeProgressTo, 1.0f};
  const unsigned long elapsed = millis() - routeProgressAnimationStartMillis;
  if (elapsed >= kIndicatorTransitionMillis) {
    return {routeProgressTo, hasFlightProgress(routeProgressTo) ? 1.0f : 0.0f};
  }
  const float blend = easeInOut(static_cast<float>(elapsed) / kIndicatorTransitionMillis);
  const bool fromKnown = hasFlightProgress(routeProgressFrom);
  const bool toKnown = hasFlightProgress(routeProgressTo);
  if (fromKnown && toKnown) {
    return {routeProgressFrom + (routeProgressTo - routeProgressFrom) * blend, 1.0f};
  }
  if (fromKnown) return {routeProgressFrom, 1.0f - blend};
  return {routeProgressTo, blend};
}

void drawRouteProgress(float progress, float opacity = 1.0f) {
  const uint16_t black = matrix.color565(0, 0, 0);
  matrix.fillRect(0, matrix.height() - 1, matrix.width(), 1, black);
  if (!hasFlightProgress(progress) || opacity <= 0.0f) return;

  opacity = constrain(opacity, 0.0f, 1.0f);
  const uint8_t incompleteLevel = static_cast<uint8_t>(26 * opacity);  // 10% green
  const uint8_t completeLevel = static_cast<uint8_t>(255 * opacity);
  const uint16_t incompleteGreen = visibleColor(0, incompleteLevel, 0);
  const uint16_t completeGreen = visibleColor(0, completeLevel, 0);
  const float completed = progress * matrix.width() / 100.0f;
  const uint8_t fullPixels = min<uint8_t>(matrix.width(), static_cast<uint8_t>(completed));
  matrix.fillRect(0, matrix.height() - 1, matrix.width(), 1, incompleteGreen);
  if (fullPixels > 0) matrix.fillRect(0, matrix.height() - 1, fullPixels, 1, completeGreen);
  // Fade the leading pixel so the one-pixel indicator still visibly eases.
  if (fullPixels < matrix.width()) {
    const float fractionalPixel = completed - fullPixels;
    const uint8_t green = static_cast<uint8_t>(
        incompleteLevel + (completeLevel - incompleteLevel) * fractionalPixel);
    matrix.drawPixel(fullPixels, matrix.height() - 1, visibleColor(0, green, 0));
  }
}

void drawRouteChrome(const Route& route, float progressOverride = -2.0f, float progressOpacity = 1.0f) {
  uint8_t red, green, blue;
  operatorBrandRgb(route, red, green, blue);
  matrix.fillRect(0, 0, matrix.width(), 2, visibleColor(red, green, blue));
  matrix.drawPixel(0, 0, visibleColor(red / 10, green / 10, blue / 10));
  matrix.drawPixel(matrix.width() - 1, 0, visibleColor(red / 10, green / 10, blue / 10));
  matrix.fillRect(0, 3, 33, 33, matrix.color565(0, 0, 0));
  drawOperatorLogo(route, 1, 3);
  drawRouteProgress(progressOverride > -1.5f ? progressOverride : route.flightProgressPercent,
                    progressOpacity);
}

uint8_t rotationIndicatorPipForRoute(uint8_t index) {
  const uint8_t pipCount = min<uint8_t>(routeCount, 5);
  return routeCount <= 5 ? index : static_cast<uint8_t>(index * pipCount / routeCount);
}

void drawRotationIndicator() {
  if (routeCount == 0 || showingPairingQr) return;
  constexpr int16_t x = 1;
  // Row 62 is intentionally left dark: it separates this display-progress
  // bundle from the route-progress tracker on the final row (63).
  constexpr int16_t y = kPanelHeight - 2 - kRotationIndicatorHeight;
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t brightPink = visibleColor(194, 31, 91);
  const uint16_t dimPink = visibleColor(19, 3, 9);
  matrix.fillRect(x, y, kRotationIndicatorWidth, kRotationIndicatorHeight, black);

  // Five 1px pips at columns 1, 3, 5, 7, and 9 deliberately reproduce the
  // compact 12px pattern. Most nearby-aircraft rotations fit directly; for a
  // longer list the pips represent evenly spaced positions through the list.
  const uint8_t pipCount = min<uint8_t>(routeCount, 5);
  const uint8_t activePip = rotationIndicatorPipForRoute(routeIndex);
  for (uint8_t pip = 0; pip < pipCount; ++pip) {
    const bool stableActive = !rotationPipTransitionActive && pip == activePip;
    matrix.drawPixel(x + 1 + pip * 2, y + 1, stableActive ? brightPink : dimPink);
  }

  if (rotationPipTransitionActive) {
    const unsigned long elapsed = millis() - rotationPipTransitionStartMillis;
    int16_t bubbleX = x + 1 + rotationPipFrom * 2;
    if (rotationPipTo > rotationPipFrom) {
      // Adjacent pips have exactly one LED between them: that is the bubble's
      // travel lane. After crossing it, the bubble settles into the new dot.
      if (elapsed >= kRotationPipBubbleMillis / 3 && elapsed < 2 * kRotationPipBubbleMillis / 3) {
        bubbleX += 1;
      } else if (elapsed >= 2 * kRotationPipBubbleMillis / 3) {
        bubbleX = x + 1 + rotationPipTo * 2;
      }
    } else if (rotationPipTo < rotationPipFrom) {
      // The last-to-first transition loops through the otherwise-unused outer
      // pixels, retaining the same little travelling-bubble feel.
      const uint8_t phase = min<uint8_t>(3, elapsed * 4 / kRotationPipBubbleMillis);
      static constexpr int8_t kWrapOffsets[] = {0, 1, 2, -1};
      bubbleX = x + 1 + rotationPipFrom * 2 + kWrapOffsets[phase];
      if (phase == 3) bubbleX = x + 1 + rotationPipTo * 2;
    }
    matrix.drawPixel(bubbleX, y + 1, brightPink);
  }

  float completion = 0.0f;
  if (rotationIndicatorResetActive) {
    const unsigned long elapsed = millis() - rotationIndicatorResetStartMillis;
    completion = 1.0f - easeInOut(static_cast<float>(min(elapsed, kIndicatorTransitionMillis)) /
                                   kIndicatorTransitionMillis);
  } else {
    const unsigned long remaining = nextFlightRotationMillis > millis()
        ? nextFlightRotationMillis - millis() : 0;
    const unsigned long elapsed = kFlightRotationMillis - min(kFlightRotationMillis, remaining);
    completion = easeInOut(static_cast<float>(elapsed) / kFlightRotationMillis);
  }
  const float completed = completion * kRotationIndicatorWidth;
  for (uint8_t column = 0; column < kRotationIndicatorWidth; ++column) {
    const float level = constrain(completed - column, 0.0f, 1.0f);
    const uint8_t red = static_cast<uint8_t>(19 + (194 - 19) * level);
    const uint8_t green = static_cast<uint8_t>(3 + (31 - 3) * level);
    const uint8_t blue = static_cast<uint8_t>(9 + (91 - 9) * level);
    matrix.drawPixel(x + column, y + 3, visibleColor(red, green, blue));
  }
}

const char* airportCity(const char* code) {
  if (strcmp(code, "---") == 0) return "---";
  for (size_t index = 0; index < kAirportCityCount; ++index) {
    if (strcmp(code, kAirportCities[index].code) == 0) return kAirportCities[index].city;
  }
  return "---";
}

void drawAirportRoute(const Route& route, int16_t x, int16_t y, uint16_t color) {
  x = drawMinecraftBoldCaps(route.origin, x, y, color) + 2;
  for (uint8_t row = 0; row < 5; ++row) {
    for (uint8_t column = 0; column < 6; ++column) {
      if (kCityArrowSprite[row] & (1 << (5 - column))) matrix.drawPixel(x + column, y + row + 1, color);
    }
  }
  drawMinecraftBoldCaps(route.destination, x + 8, y, color);
}

int16_t registrationTextWidth(const char* text) {
  int16_t width = -1;
  for (const char* character = text; *character; ++character) width += *character == '-' ? 3 : 6;
  return width;
}

void drawRouteMarquees(const Route& route) {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t secondaryWhite = matrix.color565(145, 145, 145);
  const uint16_t tertiaryWhite = matrix.color565(85, 85, 85);
  char registration[sizeof(route.registration)];
  strlcpy(registration, route.registration[0] ? route.registration : "UNKNOWN", sizeof(registration));
  for (char* character = registration; *character; ++character) *character = toupper(*character);
  const char* originCity = airportCity(route.origin);
  const char* destinationCity = airportCity(route.destination);
  matrix.fillRect(1, 37, 32, 7, black);
  matrix.fillRect(1, 47, 32, 7, black);
  matrix.fillRect(34, 16, 60, 7, black);
  matrix.fillRect(34, 28, 60, 7, black);
  matrix.fillRect(34, 40, 60, 7, black);
  drawRegistrationMarquee(registration, 1, 37, 32, secondaryWhite);
  drawMinecraftMarquee(route.aircraftType[0] ? route.aircraftType : "aircraft", 1, 47, 32, secondaryWhite);
  drawCityMarquee(originCity, destinationCity, 34, 16, 60, secondaryWhite);
  drawMinecraftMarquee(route.flightNumber[0] ? route.flightNumber : "-----", 34, 28, 60, tertiaryWhite);
  drawMinecraftMarquee(airlineName(route), 34, 40, 60, tertiaryWhite);
}

// The initial card render paints every label. Subsequent 75ms refreshes paint
// only fields which actually overflow their slot, preventing short labels from
// being cleared and re-drawn (the visible source of their flicker).
void refreshActiveMarquees(const Route& route) {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t secondaryWhite = matrix.color565(145, 145, 145);
  const uint16_t tertiaryWhite = matrix.color565(85, 85, 85);
  char registration[sizeof(route.registration)];
  strlcpy(registration, route.registration[0] ? route.registration : "UNKNOWN", sizeof(registration));
  for (char* character = registration; *character; ++character) *character = toupper(*character);
  const char* originCity = airportCity(route.origin);
  const char* destinationCity = airportCity(route.destination);
  const char* aircraftType = route.aircraftType[0] ? route.aircraftType : "aircraft";
  const char* flightNumber = route.flightNumber[0] ? route.flightNumber : "-----";
  const char* operatorName = airlineName(route);
  if (registrationTextWidth(registration) > 32) {
    matrix.fillRect(1, 37, 32, 7, black);
    drawRegistrationMarquee(registration, 1, 37, 32, secondaryWhite);
  }
  if (normalTextWidth(aircraftType) > 32) {
    matrix.fillRect(1, 47, 32, 7, black);
    drawMinecraftMarquee(aircraftType, 1, 47, 32, secondaryWhite);
  }
  if (normalTextWidth(originCity) + 2 + 6 + 2 + normalTextWidth(destinationCity) > 60) {
    matrix.fillRect(34, 16, 60, 7, black);
    drawCityMarquee(originCity, destinationCity, 34, 16, 60, secondaryWhite);
  }
  if (normalTextWidth(flightNumber) > 60) {
    matrix.fillRect(34, 28, 60, 7, black);
    drawMinecraftMarquee(flightNumber, 34, 28, 60, tertiaryWhite);
  }
  if (normalTextWidth(operatorName) > 60) {
    matrix.fillRect(34, 40, 60, 7, black);
    drawMinecraftMarquee(operatorName, 34, 40, 60, tertiaryWhite);
  }

  // Telemetry normally stays completely still. Only the exceptional values
  // wider than their 32-pixel slot are re-rendered as a marquee.
  char altitude[12], verticalSpeed[12], speed[12], heading[12], distance[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt); else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);
  if (route.speedKt >= 0) snprintf(speed, sizeof(speed), "%d", route.speedKt); else strlcpy(speed, "--", sizeof(speed));
  if (route.headingDeg >= 0) snprintf(heading, sizeof(heading), "%d", route.headingDeg); else strlcpy(heading, "--", sizeof(heading));
  if (route.distanceKm >= 0.0f) snprintf(distance, sizeof(distance), "%.1f", route.distanceKm); else strlcpy(distance, "--", sizeof(distance));

  const uint16_t telemetry = matrix.color565(85, 85, 85);
  const struct { const char* value; TelemetryUnit unit; bool arrow; bool climbing; bool level; int16_t y; } lines[] = {
      {altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None, false, false, false, 4},
      {verticalSpeed, TelemetryUnit::None, route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0, route.verticalSpeedFpm == 0, 16},
      {speed, route.speedKt >= 0 ? TelemetryUnit::Knots : TelemetryUnit::None, false, false, false, 28},
      {heading, route.headingDeg >= 0 ? TelemetryUnit::Degrees : TelemetryUnit::None, false, false, false, 40},
      {distance, route.distanceKm >= 0.0f ? TelemetryUnit::Kilometres : TelemetryUnit::None, false, false, false, 52},
  };
  for (const auto& line : lines) {
    if (telemetryLineWidth(line.value, line.unit, line.arrow) > 32) {
      matrix.fillRect(95, line.y, 32, 7, black);
      const int16_t offset = telemetryMarqueeOffset(telemetryLineWidth(line.value, line.unit, line.arrow), 32);
      const int16_t end = drawTelemetryValue(line.value, line.unit, 95 - offset, line.y, telemetry, 95, 127);
      if (line.arrow) {
        if (line.level) drawCityArrow(end + 1, line.y, 95, 127, telemetry);
        else drawVerticalRouteArrow(end + 1, line.y, line.climbing, telemetry, 95, 127);
      }
    }
  }
}

int16_t drawTelemetryLine(const char* value, TelemetryUnit unit, bool directionArrow,
                          bool climbing, bool level, int16_t y, uint16_t color) {
  constexpr int16_t left = 95, right = 127, width = right - left;
  const int16_t offset = telemetryMarqueeOffset(telemetryLineWidth(value, unit, directionArrow), width);
  const int16_t end = drawTelemetryValue(value, unit, left - offset, y, color, left, right);
  if (directionArrow) {
    if (level) drawCityArrow(end + 1, y, left, right, color);
    else drawVerticalRouteArrow(end + 1, y, climbing, color, left, right);
  }
  return end;
}

void drawTelemetryValues(const Route& route) {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t text = matrix.color565(85, 85, 85);
  matrix.fillRect(95, 4, 32, 7, black);
  matrix.fillRect(95, 16, 32, 7, black);
  matrix.fillRect(95, 28, 32, 7, black);
  matrix.fillRect(95, 40, 32, 7, black);
  matrix.fillRect(95, 52, 32, 7, black);

  char altitude[12], verticalSpeed[12], speed[12], heading[12], distance[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt);
  else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);
  if (route.speedKt >= 0) snprintf(speed, sizeof(speed), "%d", route.speedKt);
  else strlcpy(speed, "--", sizeof(speed));
  if (route.headingDeg >= 0) snprintf(heading, sizeof(heading), "%d", route.headingDeg);
  else strlcpy(heading, "--", sizeof(heading));
  if (route.distanceKm >= 0.0f) snprintf(distance, sizeof(distance), "%.1f", route.distanceKm);
  else strlcpy(distance, "--", sizeof(distance));

  drawTelemetryLine(altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None, false, false, false, 4, text);
  drawTelemetryLine(verticalSpeed, TelemetryUnit::None,
                    route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0, route.verticalSpeedFpm == 0, 16, text);
  drawTelemetryLine(speed, route.speedKt >= 0 ? TelemetryUnit::Knots : TelemetryUnit::None, false, false, false, 28, text);
  drawTelemetryLine(heading, route.headingDeg >= 0 ? TelemetryUnit::Degrees : TelemetryUnit::None, false, false, false, 40, text);
  drawTelemetryLine(distance, route.distanceKm >= 0.0f ? TelemetryUnit::Kilometres : TelemetryUnit::None, false, false, false, 52, text);
}

void drawRoute(const Route& route, uint8_t position, uint8_t total) {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t primaryWhite = matrix.color565(255, 255, 255);

  showingNoPlanes = false;
  matrix.fillScreen(black);
  drawRouteChrome(route);
  drawAirportRoute(route, 34, 4, primaryWhite);

  drawTelemetryValues(route);
  drawRouteMarquees(route);
}

// Direct 1-bit sampling of the supplied aircraft image at 33x33. The odd
// grid preserves a single-centre-pixel nose and tail; source engine sections
// are retained at two LEDs wide. The mask is rotated 90° right.
constexpr uint64_t kPlaneSprite[33] PROGMEM = {
    0x000000000ULL, 0x000600000ULL, 0x000700000ULL, 0x000380000ULL,
    0x000180000ULL, 0x0001D8000ULL, 0x0000F8000ULL, 0x000070000ULL,
    0x000070000ULL, 0x000038000ULL, 0x00003F800ULL, 0x00003F800ULL,
    0x08001F000ULL, 0x0C001F000ULL, 0x0E001FC00ULL, 0x077FFFFF8ULL,
    0x0FFFFFFFEULL, 0x077FFFFF8ULL, 0x0E001FC00ULL, 0x0C001F000ULL,
    0x08001F000ULL, 0x00003F800ULL, 0x00003F800ULL, 0x000038000ULL,
    0x000070000ULL, 0x000070000ULL, 0x0000F8000ULL, 0x0000D8000ULL,
    0x000180000ULL, 0x000380000ULL, 0x000700000ULL, 0x000600000ULL,
    0x000000000ULL,
};

void drawPlaneSprite(int16_t x, int16_t y, uint16_t color) {
  for (uint8_t row = 0; row < 33; ++row) {
    const uint64_t pixels = kPlaneSprite[row];
    for (uint8_t column = 0; column < 33; ++column) {
      if (pixels & (1ULL << (32 - column))) matrix.drawPixel(x + column, y + row, color);
    }
  }
}

void drawPlaneSpritePointingUp(int16_t x, int16_t y, uint16_t color) {
  // kPlaneSprite faces right; rotate it counter-clockwise so its nose points up.
  for (uint8_t row = 0; row < 33; ++row) {
    const uint64_t pixels = kPlaneSprite[row];
    for (uint8_t column = 0; column < 33; ++column) {
      if (pixels & (1ULL << (32 - column))) matrix.drawPixel(x + row, y + 32 - column, color);
    }
  }
}

// The left-card fallback is a deliberately small family library rather than a
// type-by-type catalogue. At 33px the important recognition cues are planform,
// engine count/position, and propeller or rotor discs.
enum class AircraftSprite : uint8_t {
  WidebodyFourEngine,
  WidebodyTwinJet,
  NarrowbodyJet,
  RegionalJet,
  Turboprop,
  LightProp,
  BusinessJet,
  Helicopter,
  Generic747,
};

bool typeContains(const char* type, const char* needle) {
  return type && strstr(type, needle) != nullptr;
}

AircraftSprite aircraftSpriteForType(const char* type) {
  // Helicopters first: some have manufacturer strings that otherwise resemble
  // fixed-wing types (for example Bell and Airbus Helicopters).
  if (typeContains(type, "H135") || typeContains(type, "H145") || typeContains(type, "H160") ||
      typeContains(type, "EC") || typeContains(type, "AS") || typeContains(type, "BK") ||
      typeContains(type, "B06") || typeContains(type, "BELL") || typeContains(type, "R22") ||
      typeContains(type, "R44") || typeContains(type, "S76") || typeContains(type, "AW")) {
    return AircraftSprite::Helicopter;
  }
  // Citation/Challenger/Gulfstream/Learjet and similar rear-engined business jets.
  if (typeContains(type, "C5") || typeContains(type, "C6") || typeContains(type, "C7") ||
      typeContains(type, "C68") || typeContains(type, "CL3") || typeContains(type, "CL6") ||
      typeContains(type, "GLF") || typeContains(type, "LJ") || typeContains(type, "FA") ||
      typeContains(type, "H25") || typeContains(type, "E35")) return AircraftSprite::BusinessJet;
  // Dash 8, ATR, Saab, Beech and commuter turboprops retain a one-pixel prop disc.
  if (typeContains(type, "DH8") || typeContains(type, "DHC") || typeContains(type, "AT4") ||
      typeContains(type, "ATR") || typeContains(type, "SF3") || typeContains(type, "BE2") ||
      typeContains(type, "BE3") || typeContains(type, "PC1") || typeContains(type, "C20") ||
      typeContains(type, "C40") || typeContains(type, "C44") || typeContains(type, "SW4")) {
    return AircraftSprite::Turboprop;
  }
  // Common light piston/single-prop families; keep them distinct from commuter twins.
  if (typeContains(type, "C1") || typeContains(type, "C2") || typeContains(type, "C3") ||
      typeContains(type, "C4") || typeContains(type, "C8") || typeContains(type, "PA") ||
      typeContains(type, "SR2") || typeContains(type, "DA4") || typeContains(type, "DA6")) {
    return AircraftSprite::LightProp;
  }
  // The 747/A340/A380 retain four visible nacelles; other widebodies are twins.
  if (typeContains(type, "B74") || typeContains(type, "A34") || typeContains(type, "A38") ||
      typeContains(type, "MD1") || typeContains(type, "IL9")) {
    return AircraftSprite::WidebodyFourEngine;
  }
  if (typeContains(type, "B77") || typeContains(type, "B78") || typeContains(type, "A33") ||
      typeContains(type, "A35")) return AircraftSprite::WidebodyTwinJet;
  // All 737 variants intentionally share this silhouette, alongside like-sized airliners.
  if (typeContains(type, "B73") || typeContains(type, "A31") || typeContains(type, "A32") ||
      typeContains(type, "A21") || typeContains(type, "B75") || typeContains(type, "B72") ||
      typeContains(type, "B71") || typeContains(type, "MD8") || typeContains(type, "DC9")) {
    return AircraftSprite::NarrowbodyJet;
  }
  if (typeContains(type, "E1") || typeContains(type, "E7") || typeContains(type, "E9") ||
      typeContains(type, "CRJ") || typeContains(type, "ARJ") || typeContains(type, "SU9") ||
      typeContains(type, "F70") || typeContains(type, "F10") || typeContains(type, "BAE")) {
    return AircraftSprite::RegionalJet;
  }
  return AircraftSprite::Generic747;
}

void drawFuselage(int16_t x, int16_t y, uint8_t start, uint8_t end, uint8_t bodyHalf,
                  uint16_t color) {
  for (uint8_t row = start; row <= end; ++row) {
    uint8_t half = bodyHalf;
    if (row <= start + 1 || row >= end - 1) half = 0;
    else if (row == start + 2 || row == end - 2) half = min<uint8_t>(1, bodyHalf);
    matrix.fillRect(x + 16 - half, y + row, half * 2 + 1, 1, color);
  }
}

void drawAircraftSprite(AircraftSprite sprite, int16_t x, int16_t y, uint16_t color) {
  if (sprite == AircraftSprite::Generic747) {
    drawPlaneSpritePointingUp(x, y, color);
    return;
  }

  // Each primitive below lives inside the same 33x33 canvas: nose at row 1,
  // tail at row 31, centreline at column 16. No anti-aliasing is used.
  if (sprite == AircraftSprite::Helicopter) {
    drawFuselage(x, y, 7, 25, 2, color);
    matrix.fillRect(x + 2, y + 10, 29, 1, color);             // main rotor
    matrix.fillRect(x + 15, y + 7, 3, 19, color);
    matrix.fillRect(x + 15, y + 25, 3, 6, color);             // tail boom
    matrix.fillRect(x + 11, y + 30, 11, 1, color);            // tailplane
    matrix.drawPixel(x + 16, y + 31, color);
    return;
  }

  if (sprite == AircraftSprite::LightProp) {
    matrix.fillRect(x + 9, y + 2, 15, 1, color);              // front propeller
    drawFuselage(x, y, 3, 29, 1, color);
    for (uint8_t row = 12; row < 18; ++row) {
      const uint8_t span = 4 + (row - 12) * 2;
      matrix.fillRect(x + 16 - span, y + row, span * 2 + 1, 1, color);
    }
    matrix.fillRect(x + 10, y + 27, 13, 2, color);
    return;
  }

  if (sprite == AircraftSprite::Turboprop) {
    drawFuselage(x, y, 2, 30, 2, color);
    for (uint8_t row = 11; row < 17; ++row) {
      const uint8_t span = 8 + (row - 11) * 2;
      matrix.fillRect(x + 16 - span, y + row, span * 2 + 1, 1, color);
    }
    // One-pixel vertical propeller discs at the wing stations are intentional.
    matrix.fillRect(x + 3, y + 9, 1, 10, color);
    matrix.fillRect(x + 29, y + 9, 1, 10, color);
    matrix.fillRect(x + 9, y + 27, 15, 2, color);
    return;
  }

  if (sprite == AircraftSprite::RegionalJet || sprite == AircraftSprite::BusinessJet) {
    drawFuselage(x, y, 2, 30, 2, color);
    for (uint8_t row = 12; row < 18; ++row) {
      const uint8_t span = 6 + (row - 12) * 2;
      matrix.fillRect(x + 16 - span, y + row, span * 2 + 1, 1, color);
    }
    // Rear-mounted engine pods and a T-tail distinguish the regional/business family.
    matrix.fillRect(x + 10, y + 22, 3, 5, color);
    matrix.fillRect(x + 20, y + 22, 3, 5, color);
    matrix.fillRect(x + 8, y + 27, 17, 2, color);
    return;
  }

  const bool widebody = sprite == AircraftSprite::WidebodyFourEngine ||
                        sprite == AircraftSprite::WidebodyTwinJet;
  drawFuselage(x, y, 1, 31, widebody ? 3 : 2, color);
  for (uint8_t row = 10; row < 19; ++row) {
    const uint8_t span = (widebody ? 8 : 7) + (row - 10) * (widebody ? 2 : 2);
    const uint8_t cappedSpan = min<uint8_t>(15, span);
    matrix.fillRect(x + 16 - cappedSpan, y + row, cappedSpan * 2 + 1, 1, color);
  }
  if (sprite == AircraftSprite::WidebodyFourEngine) {
    matrix.fillRect(x + 4, y + 18, 3, 4, color);
    matrix.fillRect(x + 10, y + 19, 3, 4, color);
    matrix.fillRect(x + 20, y + 19, 3, 4, color);
    matrix.fillRect(x + 26, y + 18, 3, 4, color);
    matrix.fillRect(x + 8, y + 28, 17, 2, color);
  } else {
    matrix.fillRect(x + 7, y + 18, 3, 4, color);
    matrix.fillRect(x + 23, y + 18, 3, 4, color);
    matrix.fillRect(x + 10, y + 28, 13, 2, color);
  }
}

void drawAircraftFallbackSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color) {
  drawAircraftSprite(aircraftSpriteForType(aircraftType), x, y, color);
}

// Fallback badges deliberately stay top-down. The no-planes animation is
// different: it travels left-to-right, so rotate each top-down silhouette
// clockwise and put its nose in the direction of travel.
void drawNoPlanesSpritePixel(int16_t x, int16_t y, uint8_t sourceX, uint8_t sourceY,
                             uint16_t color) {
  matrix.drawPixel(x + 32 - sourceY, y + sourceX, color);
}

void drawNoPlanesSpriteRect(int16_t x, int16_t y, uint8_t sourceX, uint8_t sourceY,
                            uint8_t width, uint8_t height, uint16_t color) {
  for (uint8_t row = 0; row < height; ++row) {
    for (uint8_t column = 0; column < width; ++column) {
      drawNoPlanesSpritePixel(x, y, sourceX + column, sourceY + row, color);
    }
  }
}

void drawNoPlanesFuselage(int16_t x, int16_t y, uint8_t start, uint8_t end,
                          uint8_t bodyHalf, uint16_t color) {
  for (uint8_t row = start; row <= end; ++row) {
    uint8_t half = bodyHalf;
    if (row <= start + 1 || row >= end - 1) half = 0;
    else if (row == start + 2 || row == end - 2) half = min<uint8_t>(1, bodyHalf);
    drawNoPlanesSpriteRect(x, y, 16 - half, row, half * 2 + 1, 1, color);
  }
}

void drawNoPlanesAircraftSprite(AircraftSprite sprite, int16_t x, int16_t y, uint16_t color) {
  // kPlaneSprite's native orientation already points right.
  if (sprite == AircraftSprite::Generic747) {
    drawPlaneSprite(x, y, color);
    return;
  }
  if (sprite == AircraftSprite::Helicopter) {
    drawNoPlanesFuselage(x, y, 7, 25, 2, color);
    drawNoPlanesSpriteRect(x, y, 2, 10, 29, 1, color);
    drawNoPlanesSpriteRect(x, y, 15, 7, 3, 19, color);
    drawNoPlanesSpriteRect(x, y, 15, 25, 3, 6, color);
    drawNoPlanesSpriteRect(x, y, 11, 30, 11, 1, color);
    drawNoPlanesSpritePixel(x, y, 16, 31, color);
    return;
  }
  if (sprite == AircraftSprite::LightProp) {
    drawNoPlanesSpriteRect(x, y, 9, 2, 15, 1, color);
    drawNoPlanesFuselage(x, y, 3, 29, 1, color);
    for (uint8_t row = 12; row < 18; ++row) {
      const uint8_t span = 4 + (row - 12) * 2;
      drawNoPlanesSpriteRect(x, y, 16 - span, row, span * 2 + 1, 1, color);
    }
    drawNoPlanesSpriteRect(x, y, 10, 27, 13, 2, color);
    return;
  }
  if (sprite == AircraftSprite::Turboprop) {
    drawNoPlanesFuselage(x, y, 2, 30, 2, color);
    for (uint8_t row = 11; row < 17; ++row) {
      const uint8_t span = 8 + (row - 11) * 2;
      drawNoPlanesSpriteRect(x, y, 16 - span, row, span * 2 + 1, 1, color);
    }
    drawNoPlanesSpriteRect(x, y, 3, 9, 1, 10, color);
    drawNoPlanesSpriteRect(x, y, 29, 9, 1, 10, color);
    drawNoPlanesSpriteRect(x, y, 9, 27, 15, 2, color);
    return;
  }
  if (sprite == AircraftSprite::RegionalJet || sprite == AircraftSprite::BusinessJet) {
    drawNoPlanesFuselage(x, y, 2, 30, 2, color);
    for (uint8_t row = 12; row < 18; ++row) {
      const uint8_t span = 6 + (row - 12) * 2;
      drawNoPlanesSpriteRect(x, y, 16 - span, row, span * 2 + 1, 1, color);
    }
    drawNoPlanesSpriteRect(x, y, 10, 22, 3, 5, color);
    drawNoPlanesSpriteRect(x, y, 20, 22, 3, 5, color);
    drawNoPlanesSpriteRect(x, y, 8, 27, 17, 2, color);
    return;
  }
  const bool widebody = sprite == AircraftSprite::WidebodyFourEngine ||
                        sprite == AircraftSprite::WidebodyTwinJet;
  drawNoPlanesFuselage(x, y, 1, 31, widebody ? 3 : 2, color);
  for (uint8_t row = 10; row < 19; ++row) {
    const uint8_t span = (widebody ? 8 : 7) + (row - 10) * 2;
    const uint8_t cappedSpan = min<uint8_t>(15, span);
    drawNoPlanesSpriteRect(x, y, 16 - cappedSpan, row, cappedSpan * 2 + 1, 1, color);
  }
  if (sprite == AircraftSprite::WidebodyFourEngine) {
    drawNoPlanesSpriteRect(x, y, 4, 18, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 10, 19, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 20, 19, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 26, 18, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 8, 28, 17, 2, color);
  } else {
    drawNoPlanesSpriteRect(x, y, 7, 18, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 23, 18, 3, 4, color);
    drawNoPlanesSpriteRect(x, y, 10, 28, 13, 2, color);
  }
}

void drawNoPlanesFrame() {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t dimWhite = matrix.color565(100, 100, 100);
  matrix.fillScreen(black);
  drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(noPlanesAircraftSprite), noPlanesSpriteX, 12, white);
  drawMinecraftCentered("no planes", 54, dimWhite);
}

void drawNoPlanes() {
  showingNoPlanes = true;
  noPlanesSpriteX = -kNoPlanesSpriteWidth;
  noPlanesAircraftSprite = random(0, static_cast<uint8_t>(AircraftSprite::Generic747) + 1);
  nextNoPlanesAnimationMillis = millis() + kNoPlanesAnimationStepMillis;
  drawNoPlanesFrame();
}

void advanceNoPlanesAnimation() {
  // Keep the fixed elements on the panel. Only clear the old plane footprint,
  // then paint its next position, so the border and status label never blink.
  const uint16_t black = matrix.color565(0, 0, 0);
  matrix.fillRect(noPlanesSpriteX, 12, 33, 33, black);

  ++noPlanesSpriteX;
  // matrix.width() is unsigned; cast it so a negative off-screen x position
  // remains negative rather than turning into a very large positive number.
  if (noPlanesSpriteX > static_cast<int16_t>(matrix.width())) {
    noPlanesSpriteX = -kNoPlanesSpriteWidth;
    const uint8_t spriteCount = static_cast<uint8_t>(AircraftSprite::Generic747) + 1;
    // A pass always changes family, even if the random draw repeats the last one.
    uint8_t nextSprite = random(0, spriteCount - 1);
    if (nextSprite >= noPlanesAircraftSprite) ++nextSprite;
    noPlanesAircraftSprite = nextSprite;
  }
  drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(noPlanesAircraftSprite), noPlanesSpriteX, 12,
                              matrix.color565(255, 255, 255));
}

void showNoPlanesAfterDataFailure() {
  // A transport failure used to paint a teal "data offline" card while the
  // old route scheduler remained active. Its marquee/telemetry updates then
  // wrote through that card. Treat unavailable data exactly like no aircraft:
  // clear every route-owned animation state before drawing the clean idle scene.
  routeCount = 0;
  routeIndex = 0;
  letterTransitionActive = false;
  flightRefreshDeferred = false;
  routeProgressAnimationActive = false;
  rotationIndicatorResetActive = false;
  rotationPipTransitionActive = false;
  nextRouteAnimationMillis = 0;
  nextRotationIndicatorMillis = 0;
  drawNoPlanes();
}

void drawCurrentRoute() {
  if (routeCount == 0) {
    return;
  }
  // A scene always begins at the marquee's paused, left-aligned start state.
  // This is deliberately reset only after the preceding drop animation ends.
  routeSceneStartMillis = millis();
  drawRoute(routes[routeIndex], routeIndex + 1, routeCount);
}

// Each data line is clipped to its own seven-pixel baseline. During a page
// change, old letters fall down out of that window one at a time; after a
// brief empty beat, new letters fall in from above. The logo and header are
// deliberately never touched by this animation.
constexpr unsigned long kLetterTransitionMillis = 2000;
constexpr unsigned long kLetterExitMillis = 850;
constexpr unsigned long kLetterEnterStartMillis = 1050;
constexpr unsigned long kLetterDropMillis = 220;
constexpr unsigned long kLetterStaggerMillis = 22;
constexpr unsigned long kLineStaggerMillis = 25;

int8_t letterDropOffset(unsigned long phase, uint8_t line, uint8_t character,
                        bool entering) {
  const unsigned long start = line * kLineStaggerMillis + character * kLetterStaggerMillis;
  if (entering) {
    if (phase <= start) return -7;
    const unsigned long elapsed = phase - start;
    if (elapsed >= kLetterDropMillis) return 0;
    return static_cast<int8_t>(-7 + (elapsed * 7UL) / kLetterDropMillis);
  }
  if (phase <= start) return 0;
  const unsigned long elapsed = phase - start;
  if (elapsed >= kLetterDropMillis) return 7;
  return static_cast<int8_t>((elapsed * 7UL) / kLetterDropMillis);
}

void drawAnimatedNormalLine(const char* text, int16_t left, int16_t y, int16_t width,
                            uint16_t color, uint8_t line, unsigned long phase,
                            bool entering, bool compactHyphen = false, uint8_t startIndex = 0) {
  int16_t x = left;
  uint8_t characterIndex = 0;
  for (const char* character = text; *character; ++character, ++characterIndex) {
    const int8_t offset = letterDropOffset(phase, line, characterIndex + startIndex, entering);
    if (*character == '-' && compactHyphen) {
      if (x + 1 >= left && x < left + width && y + 3 + offset >= y && y + 3 + offset < y + 7) {
        matrix.fillRect(x, y + 3 + offset, 2, 1, color);
      }
      x += 3;
      continue;
    }
    const bool uppercase = *character >= 'A' && *character <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(*character - 'A')
                                    : minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (4 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, pixelY, color);
        }
      }
    }
    x += 6;
  }
}

int16_t telemetryTextAdvance(const char* text) {
  int16_t advance = 0;
  for (const char* character = text; *character; ++character) {
    advance += *character == '.' ? 2 : (*character == '0' ? 5 : 6);
  }
  return advance;
}

void drawAnimatedTelemetryNumber(const char* text, int16_t x, int16_t y, int16_t left, int16_t right,
                                 uint16_t color, uint8_t line, unsigned long phase,
                                 bool entering, uint8_t startIndex = 0) {
  uint8_t characterIndex = startIndex;
  for (const char* character = text; *character; ++character, ++characterIndex) {
    const int8_t offset = letterDropOffset(phase, line, characterIndex, entering);
    if (*character == '.') {
      const int16_t pixelY = y + 6 + offset;
      if (pixelY >= y && pixelY < y + 7 && x >= left && x < right) matrix.drawPixel(x, pixelY, color);
      x += 2;
      continue;
    }
    const uint8_t glyph = minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (4 - column))) && pixelX >= left && pixelX < right) matrix.drawPixel(pixelX, pixelY, color);
      }
    }
    x += *character == '0' ? 5 : 6;
  }
}

void drawAnimatedKnotsUnit(int16_t x, int16_t y, int16_t left, int16_t right,
                           uint16_t color, uint8_t line, unsigned long phase,
                           bool entering, uint8_t startIndex) {
  static constexpr uint8_t kGlyphIndices[] = {10, 19, 18};  // k, t, s
  static constexpr uint8_t kWidths[] = {5, 4, 5};
  for (uint8_t glyphNumber = 0; glyphNumber < 3; ++glyphNumber) {
    const int8_t offset = letterDropOffset(phase, line, startIndex + glyphNumber, entering);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      uint8_t pixels = pgm_read_byte(&kMinecraftFont[kGlyphIndices[glyphNumber]][row]);
      if (glyphNumber == 1 && row == 5) pixels &= 0x1E;
      if (glyphNumber == 2 && row == 6) pixels &= 0x0F;
      for (uint8_t column = 0; column < kWidths[glyphNumber]; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (4 - column))) && pixelX >= left && pixelX < right) {
          matrix.drawPixel(pixelX, pixelY, color);
        }
      }
    }
    x += kWidths[glyphNumber];
  }
}

// Telemetry units and the vertical-speed direction arrow join the same drop
// sequence as their number. This keeps the settled animation frame identical
// to the static card, rather than adding those pixels in a separate flash.
void drawAnimatedTelemetryAdornment(const char* value, TelemetryUnit unit, bool directionArrow,
                                    bool climbing, bool level, int16_t y, uint8_t line,
                                    unsigned long phase, bool entering, uint16_t color) {
  constexpr int16_t left = 95, right = 127;
  const int8_t offset = letterDropOffset(phase, line, strlen(value), entering);
  int16_t x = left + telemetryTextAdvance(value);
  if (unit == TelemetryUnit::Degrees) {
    constexpr uint8_t kDegreeSprite[] = {0x2, 0x5, 0x2};
    for (uint8_t row = 0; row < 3; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      for (uint8_t column = 0; column < 3; ++column) {
        if ((kDegreeSprite[row] & (1 << (2 - column))) &&
            x + column >= left && x + column < right) {
          matrix.drawPixel(x + column, pixelY, color);
        }
      }
    }
    x += 3;
  } else if (unit == TelemetryUnit::Feet) {
    for (uint8_t row = 0; row < 2; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY >= y && pixelY < y + 7 && x >= left && x < right) matrix.drawPixel(x, pixelY, color);
    }
    ++x;
  } else if (unit == TelemetryUnit::Knots) {
    drawAnimatedKnotsUnit(x, y, left, right, color, line, phase, entering, strlen(value));
    x += telemetryUnitWidth(unit);
  } else if (unit != TelemetryUnit::None) {
    drawAnimatedTelemetryNumber(telemetryUnitText(unit), x, y, left, right, color, line,
                                phase, entering, strlen(value));
    x += telemetryTextAdvance(telemetryUnitText(unit));
  }
  if (!directionArrow) return;
  // Keep the route-arrow motif attached to the same dropped baseline as the
  // unit, clipping it to the line while the transition is in flight.
  const int16_t arrowX = x + 1;
  if (level) {
    for (uint8_t row = 0; row < 5; ++row) {
      const int16_t pixelY = y + row + 1 + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      for (uint8_t column = 0; column < 6; ++column) {
        if ((kCityArrowSprite[row] & (1 << (5 - column))) &&
            arrowX + column >= left && arrowX + column < right) {
          matrix.drawPixel(arrowX + column, pixelY, color);
        }
      }
    }
    return;
  }
  for (uint8_t sourceRow = 0; sourceRow < 5; ++sourceRow) {
    for (uint8_t sourceColumn = 0; sourceColumn < 6; ++sourceColumn) {
      if (!(kCityArrowSprite[sourceRow] & (1 << (5 - sourceColumn)))) continue;
      const int16_t pixelX = arrowX + (climbing ? sourceRow : 4 - sourceRow);
      const int16_t pixelY = y + offset + (climbing ? 5 - sourceColumn : sourceColumn);
      if (pixelX >= left && pixelX < right && pixelY >= y && pixelY < y + 7) {
        matrix.drawPixel(pixelX, pixelY, color);
      }
    }
  }
}

void drawAnimatedCityText(const char* text, int16_t x, int16_t y, int16_t left, int16_t right,
                          uint16_t color, uint8_t line, unsigned long phase, bool entering,
                          uint8_t startIndex = 0) {
  uint8_t glyphIndex = startIndex;
  for (const char* cursor = text; *cursor;) {
    const CityGlyph glyphInfo = nextCityGlyph(cursor);
    const int8_t offset = letterDropOffset(phase, line, glyphIndex, entering);
    const bool uppercase = glyphInfo.base >= 'A' && glyphInfo.base <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(glyphInfo.base - 'A') : minecraftGlyphIndex(glyphInfo.base);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < 5; ++column) {
        if ((pixels & (1 << (4 - column))) && x + column >= left && x + column < right) {
          matrix.drawPixel(x + column, pixelY, color);
        }
      }
    }
    // Keep the accent attached to its base glyph throughout the drop.
    if (glyphInfo.accent != CityAccent::None) {
      const auto accentPixel = [&](int8_t dx, int8_t dy) {
        const int16_t pixelY = y + dy + offset;
        if (x + dx >= left && x + dx < right && pixelY >= y && pixelY < y + 7) matrix.drawPixel(x + dx, pixelY, color);
      };
      switch (glyphInfo.accent) {
        case CityAccent::Acute: accentPixel(3, 0); accentPixel(2, 1); break;
        case CityAccent::Grave: accentPixel(2, 0); accentPixel(3, 1); break;
        case CityAccent::Circumflex: accentPixel(2, 0); accentPixel(1, 1); accentPixel(3, 1); break;
        case CityAccent::Tilde: accentPixel(1, 0); accentPixel(2, 1); accentPixel(3, 0); break;
        case CityAccent::Diaeresis: accentPixel(1, 0); accentPixel(3, 0); break;
        case CityAccent::Ring: accentPixel(2, 0); accentPixel(1, 1); accentPixel(3, 1); break;
        case CityAccent::Cedilla: accentPixel(2, 6); break;
        default: break;
      }
    }
    x += 6;
    cursor += glyphInfo.bytes;
    ++glyphIndex;
  }
}

void drawAnimatedCityLine(const char* origin, const char* destination, unsigned long phase,
                          bool entering, uint16_t color) {
  constexpr int16_t left = 34;
  constexpr int16_t y = 16;
  constexpr int16_t width = 60;
  drawAnimatedCityText(origin, left, y, left, left + width, color, 1, phase, entering);
  const uint8_t originGlyphCount = cityGlyphCount(origin);
  const int16_t arrowX = left + cityTextWidth(origin) + 2;
  const int8_t arrowOffset = letterDropOffset(phase, 1, originGlyphCount, entering);
  for (uint8_t row = 0; row < 5; ++row) {
    const int16_t pixelY = y + row + 1 + arrowOffset;
    if (pixelY < y || pixelY >= y + 7) continue;
    for (uint8_t column = 0; column < 6; ++column) {
      const int16_t pixelX = arrowX + column;
      if ((kCityArrowSprite[row] & (1 << (5 - column))) && pixelX >= left && pixelX < left + width) {
        matrix.drawPixel(pixelX, pixelY, color);
      }
    }
  }
  drawAnimatedCityText(destination, arrowX + 8, y, left, left + width, color, 1,
                       phase, entering, originGlyphCount + 1);
}

int16_t drawAnimatedBoldLine(const char* text, int16_t x, int16_t y, int16_t left,
                             int16_t right, uint16_t color, uint8_t line,
                             unsigned long phase, bool entering, uint8_t startIndex = 0) {
  uint8_t characterIndex = startIndex;
  for (const char* character = text; *character; ++character, ++characterIndex) {
    const int8_t offset = letterDropOffset(phase, line, characterIndex, entering);
    if (*character == '-') {
      const int16_t pixelY = y + 3 + offset;
      if (pixelY >= y && pixelY < y + 7) matrix.fillRect(x, pixelY, 5, 1, color);
      x += 6;
      continue;
    }
    const bool isY = *character == 'Y';
    const uint8_t glyph = (*character >= 'A' && *character <= 'Z')
                              ? static_cast<uint8_t>(*character - 'A') : 0;
    const uint8_t glyphWidth = isY ? 6 : 5;
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = isY ? pgm_read_byte(&kMinecraftBoldY[row])
                                 : pgm_read_byte(&kMinecraftBoldCaps[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (glyphWidth - 1 - column))) && pixelX >= left && pixelX < right) {
          matrix.drawPixel(pixelX, pixelY, color);
        }
      }
    }
    x += glyphWidth + 1;
  }
  return x;
}

void drawAnimatedAirportRoute(const Route& route, unsigned long phase, bool entering) {
  constexpr int16_t left = 34;
  constexpr int16_t right = 94;
  constexpr int16_t y = 4;
  const uint16_t white = matrix.color565(255, 255, 255);
  int16_t x = drawAnimatedBoldLine(route.origin, left, y, left, right, white, 0, phase, entering);
  x += 2;
  const int8_t arrowOffset = letterDropOffset(phase, 0, strlen(route.origin), entering);
  for (uint8_t row = 0; row < 5; ++row) {
    const int16_t pixelY = y + row + 1 + arrowOffset;
    if (pixelY < y || pixelY >= y + 7) continue;
    for (uint8_t column = 0; column < 6; ++column) {
      if (kCityArrowSprite[row] & (1 << (5 - column))) matrix.drawPixel(x + column, pixelY, white);
    }
  }
  drawAnimatedBoldLine(route.destination, x + 8, y, left, right, white, 0, phase, entering,
                       strlen(route.origin) + 1);
}

void clearTransitionTextAreas() {
  const uint16_t black = matrix.color565(0, 0, 0);
  matrix.fillRect(34, 4, 60, 7, black);
  matrix.fillRect(34, 16, 60, 7, black);
  matrix.fillRect(34, 28, 60, 7, black);
  matrix.fillRect(34, 40, 60, 7, black);
  matrix.fillRect(1, 37, 32, 7, black);
  matrix.fillRect(1, 47, 32, 7, black);
  matrix.fillRect(95, 4, 32, 7, black);
  matrix.fillRect(95, 16, 32, 7, black);
  matrix.fillRect(95, 28, 32, 7, black);
  matrix.fillRect(95, 40, 32, 7, black);
  matrix.fillRect(95, 52, 32, 7, black);
}

void drawAnimatedRouteText(const Route& route, unsigned long phase, bool entering) {
  const uint16_t secondaryWhite = matrix.color565(145, 145, 145);
  const uint16_t tertiaryWhite = matrix.color565(85, 85, 85);
  char registration[sizeof(route.registration)];
  strlcpy(registration, route.registration[0] ? route.registration : "UNKNOWN", sizeof(registration));
  for (char* character = registration; *character; ++character) *character = toupper(*character);
  const char* originCity = airportCity(route.origin);
  const char* destinationCity = airportCity(route.destination);
  char altitude[12], verticalSpeed[12], speed[12], heading[12], distance[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt);
  else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);
  if (route.speedKt >= 0) snprintf(speed, sizeof(speed), "%d", route.speedKt);
  else strlcpy(speed, "--", sizeof(speed));
  snprintf(heading, sizeof(heading), route.headingDeg >= 0 ? "%d" : "--", route.headingDeg);
  if (route.distanceKm >= 0.0f) snprintf(distance, sizeof(distance), "%.1f", route.distanceKm);
  else strlcpy(distance, "--", sizeof(distance));
  drawAnimatedAirportRoute(route, phase, entering);
  drawAnimatedCityLine(originCity, destinationCity, phase, entering, secondaryWhite);
  drawAnimatedNormalLine(route.flightNumber[0] ? route.flightNumber : "-----", 34, 28, 60,
                         tertiaryWhite, 2, phase, entering);
  drawAnimatedNormalLine(airlineName(route), 34, 40, 60, tertiaryWhite, 9, phase, entering);
  drawAnimatedNormalLine(registration, 1, 37, 32, secondaryWhite, 3, phase, entering, true);
  drawAnimatedNormalLine(route.aircraftType[0] ? route.aircraftType : "aircraft", 1, 47, 32,
                         secondaryWhite, 4, phase, entering);
  drawAnimatedTelemetryNumber(altitude, 95, 4, 95, 127, tertiaryWhite, 5, phase, entering);
  drawAnimatedTelemetryNumber(verticalSpeed, 95, 16, 95, 127, tertiaryWhite, 6, phase, entering);
  drawAnimatedTelemetryNumber(speed, 95, 28, 95, 127, tertiaryWhite, 7, phase, entering);
  drawAnimatedTelemetryNumber(heading, 95, 40, 95, 127, tertiaryWhite, 8, phase, entering);
  drawAnimatedTelemetryNumber(distance, 95, 52, 95, 127, tertiaryWhite, 10, phase, entering);
  drawAnimatedTelemetryAdornment(altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None,
                                 false, false, false, 4, 5, phase, entering, tertiaryWhite);
  drawAnimatedTelemetryAdornment(verticalSpeed,
                                 TelemetryUnit::None,
                                 route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0, route.verticalSpeedFpm == 0,
                                 16, 6, phase, entering, tertiaryWhite);
  drawAnimatedTelemetryAdornment(speed, route.speedKt >= 0 ? TelemetryUnit::Knots : TelemetryUnit::None,
                                 false, false, false, 28, 7, phase, entering, tertiaryWhite);
  drawAnimatedTelemetryAdornment(heading, route.headingDeg >= 0 ? TelemetryUnit::Degrees : TelemetryUnit::None,
                                 false, false, false, 40, 8, phase, entering, tertiaryWhite);
  drawAnimatedTelemetryAdornment(distance, route.distanceKm >= 0.0f ? TelemetryUnit::Kilometres : TelemetryUnit::None,
                                 false, false, false, 52, 10, phase, entering, tertiaryWhite);
}

void updateLetterTransition() {
  const unsigned long elapsed = millis() - letterTransitionStartMillis;
  if (elapsed >= kLetterTransitionMillis) {
    letterTransitionActive = false;
    // The departing card may have left the live radius during its 20-second
    // scene. Remove it only after its drop-out and the incoming card's
    // drop-in have both completed, then correct the target index after the
    // compacting shift.
    if (routeCount > 0 && routes[routeIndex].pendingRemoval) {
      const uint8_t removedIndex = routeIndex;
      for (uint8_t index = removedIndex; index + 1 < routeCount; ++index) {
        routes[index] = routes[index + 1];
      }
      --routeCount;
      if (letterTransitionRouteIndex > removedIndex) --letterTransitionRouteIndex;
      if (routeCount == 0) {
        drawNoPlanes();
        return;
      }
    }
    routeIndex = letterTransitionRouteIndex;
    routeProgressAnimationActive = false;
    rotationIndicatorResetActive = false;
    rotationPipTransitionActive = false;
    nextFlightRotationMillis = millis() + kFlightRotationMillis;
    // Every detail, including telemetry units and the vertical-speed arrow, is
    // already settled. Avoid clearing and repainting it at this boundary.
    routeSceneStartMillis = millis();
    if (routeSceneStartMillis - lastDisplayMaintenanceWipeMillis >= kDisplayMaintenanceWipeMillis) {
      // This safe handoff point is the only time we deliberately reset every
      // physical LED. It clears any long-lived residual colour without making
      // the otherwise-stationary card flicker during its scene.
      matrix.fillScreen(matrix.color565(0, 0, 0));
      drawRoute(routes[routeIndex], routeIndex + 1, routeCount);
      drawRotationIndicator();
      lastDisplayMaintenanceWipeMillis = routeSceneStartMillis;
    } else {
      drawRouteProgress(routes[routeIndex].flightProgressPercent);
    }
    nextRouteAnimationMillis = routeSceneStartMillis + 2000;
    nextRotationIndicatorMillis = routeSceneStartMillis;
    // A poll that became due mid-transition waits through the new scene's
    // initial two-second marquee pause instead of repainting its first frame.
    if (flightRefreshDeferred) {
      nextFlightPollMillis = millis() + 2000;
      flightRefreshDeferred = false;
    }
    return;
  }
  if (elapsed >= kLetterEnterStartMillis && !letterTransitionChromeSwapped) {
    const RouteProgressFrame progress = animatedRouteProgress();
    drawRouteChrome(routes[letterTransitionRouteIndex], progress.percent, progress.opacity);
    letterTransitionChromeSwapped = true;
  }
  clearTransitionTextAreas();
  if (elapsed < kLetterExitMillis) {
    drawAnimatedRouteText(routes[routeIndex], elapsed, false);
  } else if (elapsed >= kLetterEnterStartMillis) {
    drawAnimatedRouteText(routes[letterTransitionRouteIndex], elapsed - kLetterEnterStartMillis, true);
  }
}

bool beginBackendRequest(HTTPClient& request, WiFiClientSecure& secureClient,
                         const String& url) {
  if (url.startsWith("https://")) {
    // The ESP32 Arduino build contains a current public root-certificate bundle.
    // This validates the Worker certificate instead of accepting any HTTPS server.
    secureClient.useBuiltinCACertBundle();
    return request.begin(secureClient, url);
  }
  return request.begin(url);
}

String pairingEndpoint() {
  String endpoint(LED_PLANE_BACKEND_URL);
  const int finalSlash = endpoint.lastIndexOf('/');
  return finalSlash < 0 ? String() : endpoint.substring(0, finalSlash) + "/api/device/pairing";
}

void drawPairingQr(const String& url) {
  // The pairing URL fits in a native 37x37 version-5 grid. Draw each module as
  // exactly one LED: no scaling, interpolation, or partial modules.
  constexpr uint8_t kQrVersion = 5;
  uint8_t qrBuffer[qrcode_getBufferSize(kQrVersion)];
  QRCode qr;
  qrcode_initText(&qr, qrBuffer, kQrVersion, ECC_LOW, url.c_str());

  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t white = matrix.color565(255, 255, 255);
  const int16_t left = (matrix.width() - qr.size) / 2;
  const int16_t top = (matrix.height() - qr.size) / 2;
  showingNoPlanes = false;
  matrix.fillScreen(black);
  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      if (qrcode_getModule(&qr, x, y)) {
        matrix.drawPixel(left + x, top + y, white);
      }
    }
  }
}

void drawPairingQrLoading() {
  // Pairing creation includes a network round trip. The lead circles the ring
  // every two seconds, with a distinctly fading tail so it reads as loading,
  // not as a static icon.
  const uint16_t black = matrix.color565(0, 0, 0);
  constexpr int16_t centerX = 64;
  constexpr int16_t centerY = 32;
  constexpr int8_t kRing[][2] = {
      {-3, -5}, {0, -6}, {3, -5}, {-5, -3}, {5, -3}, {-6, 0},
      {6, 0}, {-5, 3}, {5, 3}, {-3, 5}, {0, 6}, {3, 5},
  };
  // Lead, then a smoothly diminishing tail. The final two LEDs deliberately
  // settle at 10% brightness rather than disappearing abruptly.
  constexpr uint8_t kBrightness[] = {255, 210, 170, 135, 105, 80,
                                     60, 45, 35, 28, 26, 26};
  const uint8_t lead = ((millis() - pairingQrSpinnerStartMillis) * 12UL / 2000UL) % 12;
  showingNoPlanes = false;
  matrix.fillScreen(black);
  for (uint8_t tail = 0; tail < 12; ++tail) {
    const uint8_t index = (lead + 12 - tail) % 12;
    const uint8_t level = kBrightness[tail];
    matrix.drawPixel(centerX + kRing[index][0], centerY + kRing[index][1],
                     matrix.color565(level, level, level));
  }
}

void pairingQrRequestTask(void*) {
  bool success = false;
  char url[sizeof(pairingQrRequestUrl)] = {};

  WiFiClientSecure secureClient;
  HTTPClient request;
  const String endpoint = pairingEndpoint();
  if (!endpoint.isEmpty() && beginBackendRequest(request, secureClient, endpoint)) {
    request.setTimeout(15000);
    request.addHeader("X-Device-Key", LED_PLANE_DEVICE_KEY);
    const int status = request.POST("");
    if (status == HTTP_CODE_OK) {
      JsonDocument payload;
      const DeserializationError error = deserializeJson(payload, request.getStream());
      const char* responseUrl = payload["pairUrl"] | "";
      if (!error && responseUrl[0]) {
        strlcpy(url, responseUrl, sizeof(url));
        success = true;
      }
    } else {
      Serial.printf("Pairing request failed: HTTP %d\n", status);
    }
    request.end();
  }

  if (success) strlcpy(pairingQrRequestUrl, url, sizeof(pairingQrRequestUrl));
  pairingQrRequestSucceeded = success;
  pairingQrRequestFinished = true;
  pairingQrTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

bool fetchAndDrawPairingQr() {
  if (strlen(LED_PLANE_BACKEND_URL) == 0 || strlen(LED_PLANE_DEVICE_KEY) == 0 ||
      WiFi.status() != WL_CONNECTED) {
    return false;
  }

  // Pairing links are valid for five minutes. Reuse one for four minutes so a
  // temporarily unavailable flight feed does not fill the database with tokens.
  if (!pairingUrl.isEmpty() && millis() < pairingUrlRefreshMillis) {
    drawPairingQr(pairingUrl);
    return true;
  }

  if (pairingQrRequestActive) return true;
  drawPairingQrLoading();
  pairingQrSpinnerStartMillis = millis();
  pairingQrRequestFinished = false;
  pairingQrRequestSucceeded = false;
  pairingQrRequestActive = xTaskCreatePinnedToCore(
      pairingQrRequestTask, "pairing-qr", 8192, nullptr, 1, &pairingQrTaskHandle, 0) == pdPASS;
  if (!pairingQrRequestActive) {
    Serial.println("Could not start pairing QR request");
    return false;
  }
  return true;
}

void updatePairingQrRequest() {
  if (!pairingQrRequestActive) return;
  if (!pairingQrRequestFinished) {
    if (showingPairingQr) drawPairingQrLoading();
    return;
  }

  pairingQrRequestActive = false;
  if (pairingQrRequestSucceeded) {
    pairingUrl = pairingQrRequestUrl;
    pairingUrlRefreshMillis = millis() + 4UL * 60UL * 1000UL;
    if (showingPairingQr) drawPairingQr(pairingUrl);
  } else if (showingPairingQr) {
    matrix.fillScreen(matrix.color565(0, 0, 0));
    drawMinecraftCentered("QR", 22, matrix.color565(255, 255, 255));
    drawMinecraftCentered("OFFLINE", 37, matrix.color565(255, 255, 255));
  }
}

bool routesHaveSameIdentity(const Route& first, const Route& second) {
  return first.identity[0] && second.identity[0] && strcmp(first.identity, second.identity) == 0;
}

// A poll updates the queue without restarting it. Existing aircraft retain
// their place, aircraft that disappeared are collapsed out, and new aircraft
// arrive at the tail. The on-screen card is deliberately held as a snapshot
// until it has finished its 20-second scene and its exit animation.
void mergeRefreshedRouteQueue(uint8_t refreshedCount) {
  if (routeCount == 0) {
    memcpy(routes, refreshedRoutes, sizeof(Route) * refreshedCount);
    routeCount = refreshedCount;
    routeIndex = 0;
    routeProgressAnimationActive = false;
    rotationIndicatorResetActive = false;
    rotationPipTransitionActive = false;
    routeProgressTo = routes[routeIndex].flightProgressPercent;
    nextFlightRotationMillis = millis() + kFlightRotationMillis;
    drawCurrentRoute();
    return;
  }

  bool consumed[kMaxRoutableFlights] = {};
  uint8_t mergedCount = 0;
  uint8_t activeMergedIndex = 0;
  for (uint8_t existingIndex = 0; existingIndex < routeCount; ++existingIndex) {
    int8_t refreshedIndex = -1;
    for (uint8_t candidate = 0; candidate < refreshedCount; ++candidate) {
      if (!consumed[candidate] && routesHaveSameIdentity(routes[existingIndex], refreshedRoutes[candidate])) {
        refreshedIndex = candidate;
        break;
      }
    }

    if (existingIndex == routeIndex) {
      // Keep the exact current snapshot while it is visible. If it disappeared
      // from the feed, mark it for removal at the end of its transition.
      mergedRoutes[mergedCount] = routes[existingIndex];
      mergedRoutes[mergedCount].pendingRemoval = refreshedIndex < 0;
      activeMergedIndex = mergedCount++;
      if (refreshedIndex >= 0) consumed[refreshedIndex] = true;
    } else if (refreshedIndex >= 0) {
      mergedRoutes[mergedCount] = refreshedRoutes[refreshedIndex];
      mergedRoutes[mergedCount].pendingRemoval = false;
      consumed[refreshedIndex] = true;
      ++mergedCount;
    }
  }
  for (uint8_t refreshedIndex = 0; refreshedIndex < refreshedCount && mergedCount < kMaxRoutableFlights;
       ++refreshedIndex) {
    if (!consumed[refreshedIndex]) {
      mergedRoutes[mergedCount] = refreshedRoutes[refreshedIndex];
      mergedRoutes[mergedCount].pendingRemoval = false;
      ++mergedCount;
    }
  }

  memcpy(routes, mergedRoutes, sizeof(Route) * mergedCount);
  routeCount = mergedCount;
  routeIndex = activeMergedIndex;
  Serial.printf("Queue refreshed: %u aircraft, active index %u\n", routeCount, routeIndex);
}

void fetchAndDrawFirstFlight() {
  if (strlen(LED_PLANE_BACKEND_URL) == 0 || strlen(LED_PLANE_DEVICE_KEY) == 0 ||
      WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure secureClient;
  // Keep the TLS client alive until HTTPClient has finished destroying itself.
  // Local variables are destroyed in reverse order.
  HTTPClient request;
  if (!beginBackendRequest(request, secureClient, LED_PLANE_BACKEND_URL)) {
    showNoPlanesAfterDataFailure();
    return;
  }
  // A fresh nearby-aircraft lookup can take several seconds before the backend
  // has a cached snapshot. Give it time rather than leaving the greeting up.
  request.setTimeout(60000);
  request.addHeader("X-Device-Key", LED_PLANE_DEVICE_KEY);
  const int status = request.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Flight request failed: HTTP %d\n", status);
    request.end();
    showNoPlanesAfterDataFailure();
    return;
  }

  JsonDocument payload;
  const DeserializationError error = deserializeJson(payload, request.getStream());
  request.end();
  if (error) {
    Serial.printf("Flight JSON failed: %s\n", error.c_str());
    showNoPlanesAfterDataFailure();
    return;
  }

  uint8_t newRouteCount = 0;
  for (JsonObject flight : payload["flights"].as<JsonArray>()) {
    const char* candidateOrigin = flight["origin"] | "";
    const char* candidateDestination = flight["destination"] | "";
    const char* candidateFlightNumber = flight["flightNumber"] | "";
    const char* candidateOperator = flight["airline"] | "";
    const char* candidateRegistration = flight["registration"] | "";
    const char* candidateIcao24 = flight["icao24"] | "";
    const bool hasKnownRoute = candidateOrigin[0] && candidateDestination[0] &&
        strcmp(candidateOrigin, "---") != 0 && strcmp(candidateDestination, "---") != 0;
    if (hideUnknownRoutes && !hasKnownRoute) continue;
    // A route is optional enrichment. Keep any live aircraft that has an
    // identity, even when the provider has not resolved its airports yet.
    if ((candidateOrigin[0] || candidateDestination[0] || candidateFlightNumber[0] ||
         candidateRegistration[0] || candidateIcao24[0]) && newRouteCount < kMaxRoutableFlights) {
      Route& route = refreshedRoutes[newRouteCount];
      if (candidateIcao24[0]) strlcpy(route.identity, candidateIcao24, sizeof(route.identity));
      else if (candidateRegistration[0]) strlcpy(route.identity, candidateRegistration, sizeof(route.identity));
      else if (candidateFlightNumber[0]) strlcpy(route.identity, candidateFlightNumber, sizeof(route.identity));
      else snprintf(route.identity, sizeof(route.identity), "%s>%s", candidateOrigin, candidateDestination);
      strlcpy(route.origin, candidateOrigin[0] ? candidateOrigin : "---", sizeof(route.origin));
      strlcpy(route.destination, candidateDestination[0] ? candidateDestination : "---", sizeof(route.destination));
      strlcpy(route.flightNumber, candidateFlightNumber, sizeof(route.flightNumber));
      strlcpy(route.operatorCode, candidateOperator, sizeof(route.operatorCode));
      strlcpy(route.registration, candidateRegistration, sizeof(route.registration));
      route.altFt = flight["altFt"] | -1;
      route.speedKt = flight["speedKt"] | -1;
      route.verticalSpeedFpm = flight["verticalSpeedFpm"] | -100000;
      route.headingDeg = flight["headingDeg"] | -1;
      strlcpy(route.aircraftType, flight["aircraftType"] | "", sizeof(route.aircraftType));
      route.distanceKm = flight["distanceKm"] | -1.0f;
      route.flightProgressPercent = flight["flightProgressPercent"] | -1.0f;
      route.pendingRemoval = false;
      ++newRouteCount;
    }
  }
  if (newRouteCount == 0 && routeCount == 0) {
    routeCount = 0;
    drawNoPlanes();
    Serial.println("No routable flights in the current response.");
  } else {
    mergeRefreshedRouteQueue(newRouteCount);
    Serial.printf("Queued %u routable flights\n", routeCount);
  }
}

void handleBootButton() {
  // On the Waveshare board the BOOT button is GPIO 0 and reads LOW when held.
  const bool pressed = digitalRead(kBootButtonPin) == LOW;
  if (pressed && !bootButtonWasPressed && millis() - lastBootButtonPressMillis > 300) {
    lastBootButtonPressMillis = millis();
    showingPairingQr = !showingPairingQr;
    if (showingPairingQr) {
      Serial.println("Display mode: pairing QR");
      if (!fetchAndDrawPairingQr()) {
        matrix.fillScreen(matrix.color565(0, 0, 0));
        drawMinecraftCentered("QR", 22, matrix.color565(255, 255, 255));
        drawMinecraftCentered("OFFLINE", 37, matrix.color565(255, 255, 255));
      }
    } else {
      Serial.println("Display mode: flight route");
      if (routeCount > 0) {
        drawCurrentRoute();
      } else {
        drawNoPlanes();
      }
      nextFlightRotationMillis = millis() + kFlightRotationMillis;
      fetchAndDrawFirstFlight();
    }
  }
  bootButtonWasPressed = pressed;
}

bool updateRequestIsAuthorized() {
  return strlen(LED_PLANE_OTA_PASSWORD) == 0 ||
         updateServer.authenticate("admin", LED_PLANE_OTA_PASSWORD);
}

void loadDisplayPreferences() {
  Preferences preferences;
  preferences.begin("led-plane", true);
  hideUnknownRoutes = preferences.getBool("hide-unknown-routes", false);
  preferences.end();
}

void saveDisplayPreferences() {
  Preferences preferences;
  preferences.begin("led-plane", false);
  preferences.putBool("hide-unknown-routes", hideUnknownRoutes);
  preferences.end();
}

void enableLocalUpdater() {
  updateServer.on("/", HTTP_GET, []() {
    if (!updateRequestIsAuthorized()) {
      return updateServer.requestAuthentication();
    }
    String page =
        "<!doctype html><html><body style='font-family:sans-serif;max-width:42rem;"
        "margin:3rem auto'><h1>LED Plane settings</h1>"
        "<form method='post' action='/settings'><label>"
        "<input type='checkbox' name='hideUnknownRoutes' value='1'";
    if (hideUnknownRoutes) page += " checked";
    page += "> Hide planes with unknown routes</label> "
            "<button>Save</button></form>"
            "<h2>Firmware update</h2>"
            "<form method='post' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='update' accept='.bin' required> "
            "<button>Install update</button></form></body></html>";
    updateServer.send(200, "text/html", page);
  });

  updateServer.on("/settings", HTTP_POST, []() {
    if (!updateRequestIsAuthorized()) {
      return updateServer.requestAuthentication();
    }
    hideUnknownRoutes = updateServer.hasArg("hideUnknownRoutes");
    saveDisplayPreferences();
    fetchAndDrawFirstFlight();
    updateServer.sendHeader("Location", "/");
    updateServer.send(303);
  });

  updateServer.on(
      "/update", HTTP_POST,
      []() {
        if (!updateRequestIsAuthorized()) {
          return updateServer.requestAuthentication();
        }
        if (Update.hasError()) {
          updateServer.send(500, "text/plain", "Update failed.");
          return;
        }
        updateServer.send(200, "text/plain", "Update installed. Restarting now.");
        shouldRestart = true;
      },
      []() {
        if (!updateRequestIsAuthorized()) {
          return;
        }
        HTTPUpload& upload = updateServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("Web update started: %s\n", upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            Serial.printf("Web update complete: %u bytes\n", upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });
  updateServer.begin();
}

void saveWifiCredentials() {
  wifi_config_t stationConfig = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &stationConfig) != ESP_OK ||
      stationConfig.sta.ssid[0] == '\0') {
    return;
  }

  Preferences preferences;
  preferences.begin("led-plane", false);
  preferences.putString("ssid", reinterpret_cast<const char*>(stationConfig.sta.ssid));
  preferences.putString("pass", reinterpret_cast<const char*>(stationConfig.sta.password));
  preferences.end();
}

void synchronizeClock() {
  // TLS rejects even a valid certificate when the ESP32 still believes it is
  // 1970. Set UTC from public NTP before contacting the HTTPS Worker.
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  time_t now = 0;
  const unsigned long deadline = millis() + 15000;
  while (now < 1700000000 && millis() < deadline) {
    time(&now);
    delay(250);
  }
  if (now < 1700000000) {
    Serial.println("Network time unavailable; HTTPS requests may fail until Wi-Fi reconnects.");
  }
}

void connectWifiAndEnableLocalUpdater() {
  WiFi.mode(WIFI_STA);
  // Matrix refresh plus Wi-Fi power saving can make OTA acknowledgements late.
  // Keep Wi-Fi awake and allow plenty of time for a local network transfer.
  WiFi.setSleep(false);
  WiFi.setHostname(kDeviceHostname);

  Preferences preferences;
  preferences.begin("led-plane", true);
  const String savedSsid = preferences.getString("ssid", "");
  const String savedPass = preferences.getString("pass", "");
  preferences.end();

  bool connected = false;
  if (!savedSsid.isEmpty()) {
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    const unsigned long reconnectDeadline = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < reconnectDeadline) {
      delay(250);
    }
    connected = WiFi.status() == WL_CONNECTED;
  }

  if (!connected) {
    WiFiManager wifiManager;
    // Saved network credentials are stored in the ESP32's non-volatile memory.
    // If reconnecting fails (including the first boot), start the setup portal.
    wifiManager.setConfigPortalBlocking(true);
    wifiManager.setConnectTimeout(30);

    if (!wifiManager.autoConnect(kSetupAccessPoint)) {
      Serial.println("Wi-Fi setup timed out; restarting portal.");
      delay(1000);
      ESP.restart();
    }
    saveWifiCredentials();
  }

  synchronizeClock();

  // The setup helper, when needed, is out of scope before the updater uses port 80.
  enableLocalUpdater();

  Serial.printf("Wi-Fi connected. Local updater: http://%s.local (%s)\n",
                kDeviceHostname, WiFi.localIP().toString().c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("Boot reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  matrix.begin();
  // Keep the first panel test deliberately dim; raise only after the external
  // 5 V panel supply is verified.
  matrix.setBrightness8(255);
  lastDisplayMaintenanceWipeMillis = millis();
  drawNoPlanes();
  pinMode(kBootButtonPin, INPUT_PULLUP);
  loadDisplayPreferences();
  connectWifiAndEnableLocalUpdater();
  fetchAndDrawFirstFlight();
  nextFlightPollMillis = millis() + 60000;
}

void loop() {
  // HUB75 DMA refresh runs independently; handle local web update traffic here.
  updateServer.handleClient();
  handleBootButton();
  updatePairingQrRequest();
  const bool transitionWasActive = letterTransitionActive;
  // If the only card disappeared during its scene, honour the full scene
  // duration before falling back to the idle animation.
  if (!showingPairingQr && routeCount == 1 && routes[0].pendingRemoval &&
      !letterTransitionActive && millis() >= nextFlightRotationMillis) {
    routeCount = 0;
    routeIndex = 0;
    drawNoPlanes();
  }
  if (!showingPairingQr && routeCount > 1 && !letterTransitionActive &&
      millis() >= nextFlightRotationMillis) {
    letterTransitionRouteIndex = (routeIndex + 1) % routeCount;
    letterTransitionStartMillis = millis();
    letterTransitionActive = true;
    letterTransitionChromeSwapped = false;
    rotationPipFrom = rotationIndicatorPipForRoute(routeIndex);
    rotationPipTo = rotationIndicatorPipForRoute(letterTransitionRouteIndex);
    rotationPipTransitionActive = rotationPipFrom != rotationPipTo;
    rotationPipTransitionStartMillis = letterTransitionStartMillis;
    rotationIndicatorResetActive = true;
    rotationIndicatorResetStartMillis = letterTransitionStartMillis;
    routeProgressFrom = routes[routeIndex].flightProgressPercent;
    routeProgressTo = routes[letterTransitionRouteIndex].flightProgressPercent;
    routeProgressAnimationActive = hasFlightProgress(routeProgressFrom) ||
        hasFlightProgress(routeProgressTo);
    // Share the letter transition's full two-second timeline. In particular,
    // known↔unknown route changes ease the green row all the way out or in,
    // rather than jumping on the first redraw.
    routeProgressAnimationStartMillis = letterTransitionStartMillis;
    // Do not start another scene while this one is gracefully winding down.
    nextFlightRotationMillis = 0xFFFFFFFFUL;
  }
  if (letterTransitionActive) updateLetterTransition();
  if (millis() >= nextFlightPollMillis) {
    // Do not let a blocking network refresh erase the exit/enter frames. When
    // a refresh lands on a scene boundary, hold it until the new card has had
    // its initial marquee pause.
    if (!showingPairingQr && (letterTransitionActive || transitionWasActive)) {
      if (letterTransitionActive) flightRefreshDeferred = true;
      else nextFlightPollMillis = millis() + 2000;
    } else {
      if (showingPairingQr) fetchAndDrawPairingQr();
      else fetchAndDrawFirstFlight();
      nextFlightPollMillis = millis() + 60000;
    }
  }
  if (!showingPairingQr && routeCount > 0 && !letterTransitionActive && millis() >= nextRouteAnimationMillis) {
    refreshActiveMarquees(routes[routeIndex]);
    nextRouteAnimationMillis = millis() + 75;
  }
  if (!showingPairingQr && routeCount > 0 && millis() >= nextRotationIndicatorMillis) {
    if (routeProgressAnimationActive) {
      const RouteProgressFrame progress = animatedRouteProgress();
      drawRouteProgress(progress.percent, progress.opacity);
    }
    drawRotationIndicator();
    // A 33 fps cadence keeps the eased one-pixel progress fade visually
    // continuous while a card is transitioning; the static indicator does
    // not need the extra redraws.
    nextRotationIndicatorMillis = millis() + (routeProgressAnimationActive ? 30 : 50);
  }
  if (showingNoPlanes && !showingPairingQr && millis() >= nextNoPlanesAnimationMillis) {
    advanceNoPlanesAnimation();
    nextNoPlanesAnimationMillis = millis() + kNoPlanesAnimationStepMillis;
  }
  if (shouldRestart) {
    delay(500);
    ESP.restart();
  }
}
