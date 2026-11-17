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
#include "aircraft_sprites.h"
#include "air_canada_logo_32.h"
#include "air_canada_jazz_logo_32.h"
#include "alberta_health_logo_32.h"
#include "flair_logo_32.h"
#include "cargojet_logo_32.h"
#include "canwest_air_logo_32.h"
#include "central_mountain_air_logo_32.h"
#include "coywolf_aviation_logo_32.h"
#include "airsprint_logo_32.h"
#include "alberta_health_logo_32.h"
#include "north_cariboo_air_logo_32.h"
#include "porter_logo_32.h"
#include "rcaf_roundel_32.h"
#include "sunwest_logo_32.h"
#include "stars_logo_32.h"
#include "klm_logo_32.h"
#include "unknown_brand_globe_32.h"
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
constexpr size_t kFlightResponseCapacity = 16384;
constexpr unsigned long kFlightFetchWatchdogMillis = 30000;
constexpr int16_t kRotationIndicatorWidth = 32;
constexpr int16_t kRotationIndicatorHeight = 4;
constexpr unsigned long kRotationPipBubbleMillis = 600;
constexpr unsigned long kBootChevronMillis = 2000;
constexpr unsigned long kBootChevronStaggerMillis = 1000;
constexpr unsigned long kBootExitChevronMillis = 600;
constexpr unsigned long kBootRouteRollInMillis = 900;
constexpr unsigned long kWifiOfflinePortalMillis = 60UL * 1000UL;
constexpr char kDeviceHostname[] = "led-plane";

// WiFiManager owns the captive-portal routes, but its small injection points
// let the setup experience feel like part of the display rather than a router
// administration page. Keep these strings static: WiFiManager retains their
// pointers while the portal is open.
constexpr char kPortalHead[] = R"html(
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body.invert{margin:0;min-height:100vh;display:grid;place-items:center;background:#101014!important;color:#f8f4fa!important;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}.wrap{width:min(100%,420px)!important;min-width:0!important;margin:auto;padding:30px 24px;text-align:left;background:#19171d;border:1px solid #332b39;border-radius:24px;box-shadow:0 24px 80px #0008}body.invert .wrap>h1,body.invert .wrap>h3{display:none}.portal-brand{text-align:center;padding:4px 0 20px}.portal-mark{width:48px;height:48px;margin:0 auto 13px;border-radius:16px;display:grid;place-items:center;background:linear-gradient(135deg,#1b927f,#147c6d);color:white;font-size:26px;box-shadow:0 8px 25px #147c6d55}.portal-brand h1{margin:0;color:#fff;font-family:"Arial Black",Impact,sans-serif;font-size:28px;line-height:1;letter-spacing:-1.2px}.portal-brand p{margin:10px 0 0;color:#bcb3c2;font-size:15px}.portal-actions{display:grid;gap:10px}.portal-actions form{margin:0}.portal-actions button,button,input[type='submit']{border:0!important;border-radius:14px!important;background:#147c6d!important;color:#fff!important;font-weight:800!important;font-size:16px!important;line-height:1.25rem!important;padding:15px 16px!important;box-shadow:0 8px 20px #147c6d38!important}.portal-actions .info{background:#2b2630!important;color:#f8eefa!important;box-shadow:none!important}.button-icon{display:inline-block;width:24px;margin-right:7px;font-size:18px;vertical-align:-1px}button:active{transform:translateY(1px);opacity:.88!important}label{display:block;color:#d8cdd9!important;font-weight:700!important;margin:14px 0 6px!important}input,select{width:100%!important;border:1px solid #4b3f4e!important;border-radius:12px!important;background:#211d24!important;color:#fff!important;padding:13px!important;font-size:16px!important}input:focus{outline:2px solid #138271!important;border-color:transparent!important}a{color:#69c9b7!important}hr{border:0!important;border-top:1px solid #3a303d!important;margin:22px 0!important}.msg{border:1px solid #573b50!important;border-left:4px solid #147c6d!important;border-radius:14px!important;background:#251f27!important;color:#f5eefa!important;padding:16px!important}.q{filter:invert(1) hue-rotate(240deg)}dl{margin:0!important}dt{color:#79cebc!important;margin-top:13px!important}dd{color:#d8cdd9!important;word-break:break-word}form[action='/']{display:none!important}@media(max-width:460px){.wrap{min-height:100vh;border:0;border-radius:0;display:flex;flex-direction:column;justify-content:center;padding:28px 22px}}
</style>)html";
constexpr char kPortalMenu[] = R"html(
<section class="portal-brand"><div class="portal-mark">✈</div><h1>Plane Display</h1><p>Let’s get this display online.</p></section>
<section class="portal-actions"><form action="/wifi" method="get"><button type="submit"><span class="button-icon">⌁</span>Connect</button></form><form action="/info" method="get"><button class="info" type="submit"><span class="button-icon">ⓘ</span>Info</button></form></section>)html";
const char* kPortalMenuItems[] = {"custom"};

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
char setupAccessPoint[32] = "Plane Display ----";
unsigned long wifiOfflineSinceMillis = 0;
bool networkSetupPortalActive = false;
unsigned long nextFlightPollMillis = 0;
unsigned long nextRefreshDebugRedrawMillis = 0;
String pairingUrl;
unsigned long pairingUrlRefreshMillis = 0;
bool showingPairingQr = false;
bool pairingQrLoadingPainted = false;
int pairingQrRequestStatus = 0;
bool pairingQrRetryPending = false;
unsigned long pairingQrRetryMillis = 0;
unsigned long pairingQrSpinnerStartMillis = 0;
volatile bool pairingQrFetchInProgress = false;
volatile bool pairingQrFetchFinished = false;
volatile int pairingQrFetchStatus = 0;
char pairingQrFetchedUrl[256] = {};
// Display settings are deliberately fetched separately from flight data. The
// settings response is tiny and authoritative for a board's name/location.
bool deviceConfigRefreshPending = false;
bool deviceConfigRefreshFlightsPending = false;
volatile bool deviceConfigFetchInProgress = false;
volatile bool deviceConfigFetchFinished = false;
volatile int deviceConfigFetchStatus = 0;
char deviceConfigFetchedName[49] = {};
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
int16_t nextNoPlanesSpriteX = -kNoPlanesSpriteWidth;
uint8_t nextNoPlanesAircraftSprite = 0;
bool nextNoPlanesSpriteActive = false;
bool showingNoPlanes = false;
// The final boot card holds the configured display name until the first
// successful flight response gives us a real scene to reveal.
bool bootTransitionPending = false;
bool bootToRouteTransitionActive = false;
float bootExitChevronProgress = 0.0f;
char bootDisplayName[49] = "PLANE DISPLAY";
// The initial factory/provisioning build supplies this once. Afterwards the
// board-owned copy in NVS is authoritative, so a shared OTA firmware image
// cannot accidentally turn several displays into the same backend device.
char persistedDeviceKey[96] = {};
void cacheBootDisplayName(const char* name);
bool hideUnknownRoutes = false;
volatile bool flightFetchInProgress = false;
volatile bool flightFetchFinished = false;
volatile int flightFetchStatus = 0;
volatile unsigned long flightFetchStartedMillis = 0;
volatile uint32_t flightFetchAttempts = 0;
volatile uint32_t flightFetchCompletions = 0;
uint8_t consecutiveFlightFetchFailures = 0;
size_t flightFetchLength = 0;
char flightFetchResponse[kFlightResponseCapacity] = {};
// A deliberately visible diagnostic while we track down stale-card reports.
// It records when the display last accepted a complete backend snapshot, not
// when a request merely began.
char lastFlightRefreshTime[9] = "--:--:--";
uint32_t appliedFlightRefreshes = 0;
bool idleToRouteTransitionActive = false;
bool routeToIdleTransitionActive = false;
bool idleToRouteCardDrawn = false;
unsigned long idleToRouteTransitionStartMillis = 0;
unsigned long routeToIdleTransitionStartMillis = 0;

struct Route {
  char identity[16];
  char origin[8];
  char destination[8];
  char flightNumber[12];
  char operatorCode[8];
  char registration[12];
  char specialFlightTitle[48];
  bool specialFlight;
  int altFt;
  int speedKt;
  int verticalSpeedFpm;
  int trackDeg;
  char aircraftType[12];
  float distanceKm;
  char etaDisplay[8];
  uint32_t estimatedArrivalEpoch;
  float arrivalDelayPercent;
  float flightProgressPercent;
  bool pendingRemoval;
};

enum class TailOverrideLogo : uint8_t { None, AlbertaHealth };

// Registration data takes priority over a callsign: it is a stronger identity
// signal for aircraft that do not use a branded callsign. Add future special
// aircraft here; this is intentionally a data table rather than carrier logic.
struct TailOverride {
  const char* registration;
  const char* operatorName;
  const uint8_t* brandColor;
  TailOverrideLogo logo;
};

constexpr TailOverride kTailOverrides[] = {
    {"C-GLUD", "Alberta Health Services", kAlbertaHealthBrandColor, TailOverrideLogo::AlbertaHealth},
    {"C-GLUL", "Alberta Health Services", kAlbertaHealthBrandColor, TailOverrideLogo::AlbertaHealth},
    {"C-GLUN", "Alberta Health Services", kAlbertaHealthBrandColor, TailOverrideLogo::AlbertaHealth},
    {"C-FSUG", "Alberta Health Services", kAlbertaHealthBrandColor, TailOverrideLogo::AlbertaHealth},
};
constexpr size_t kTailOverrideCount = sizeof(kTailOverrides) / sizeof(kTailOverrides[0]);

Route routes[kMaxRoutableFlights];
Route refreshedRoutes[kMaxRoutableFlights];
Route mergedRoutes[kMaxRoutableFlights];
uint8_t routeCount = 0;
uint8_t routeIndex = 0;

// A deliberately blocky 5x7 face: every glyph is exactly seven LEDs high.
// It keeps airport codes legible while giving the display a Minecraft-like look.
constexpr char kMinecraftGlyphs[] = "abcdefghijklmnopqrstuvwxyz0123456789/- :'?";
constexpr uint8_t kMinecraftFont[][7] PROGMEM = {
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0F}, {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x1E}, {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
    {0x02,0x00,0x06,0x02,0x02,0x02,0x07}, {0x01,0x00,0x03,0x01,0x01,0x09,0x06},
    {0x08,0x08,0x09,0x0A,0x0C,0x0A,0x09}, {0x06,0x02,0x02,0x02,0x02,0x02,0x07},
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0B}, {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x1E}, {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, {0x00,0x00,0x1F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // : — intentionally two centred pixels so debug timestamps stay compact.
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00},
    // ' — compact apostrophe for display names and city/operator text.
    {0x03,0x03,0x02,0x00,0x00,0x00,0x00},
    // ? — explicit unknown-airport marker, rather than silently drawing a space.
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
};

constexpr uint8_t kMinecraftLowercaseWidths[] PROGMEM = {
    5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

// The full face uses a standard five-wide zero. Only dense telemetry on the
// right side gets this compact four-wide variant.
constexpr uint8_t kMinecraftTelemetryZero[7] PROGMEM = {0x0C,0x12,0x12,0x16,0x1A,0x12,0x0C};

// The compact `kts` sprite has intentionally bespoke spacing and two local
// pixel edits. Keep it independent from the editable lowercase face.
constexpr uint8_t kCompactKnotsGlyphs[][7] PROGMEM = {
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12},  // k
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},  // t
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},  // s
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
    width += (*character == '.' || *character == ':') ? 2 : (*character == '0' ? 5 : 6);
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

// The card refresh cadence is intentionally faster than a pixel of marquee
// travel. Repainting during either two-second endpoint hold clears and redraws
// identical LEDs over and over, which reads as a distracting flash on the
// physical matrix. Only redraw a marquee while its offset can actually change.
bool marqueeIsMoving(int16_t contentWidth, int16_t slotWidth) {
  if (contentWidth <= slotWidth) return false;
  const unsigned long oneWay = (contentWidth - slotWidth) * 75UL;
  const unsigned long phase = (millis() - routeSceneStartMillis) % (4000UL + oneWay * 2UL);
  const unsigned long endHoldStart = 2000UL + oneWay;
  const unsigned long returnStart = 4000UL + oneWay;
  // The display's incremental render can arrive just after a 75ms boundary.
  // Give each endpoint a single, short 200ms handoff window: it reliably
  // paints the terminal offset even with that scheduling jitter, then leaves
  // the remaining pause completely untouched. A 75ms window was occasionally
  // missed, leaving the held marquee at travel-1 until the return journey.
  constexpr unsigned long kEndpointHandoffMillis = 200UL;
  if (phase < kEndpointHandoffMillis) return true;
  if (phase >= 2000UL && phase < endHoldStart) return true;
  if (phase >= endHoldStart && phase < endHoldStart + kEndpointHandoffMillis) return true;
  return phase >= returnStart;
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
    if (*character == ':') {
      drawTelemetryPixel(x, y + 2, color, left, right);
      drawTelemetryPixel(x, y + 5, color, left, right);
      x += 2;
      continue;
    }
    const uint8_t glyph = minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = *character == '0'
                                 ? pgm_read_byte(&kMinecraftTelemetryZero[row])
                                 : pgm_read_byte(&kMinecraftFont[glyph][row]);
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
    static constexpr uint8_t kWidths[] = {5, 4, 5};
    for (uint8_t glyphNumber = 0; glyphNumber < 3; ++glyphNumber) {
      for (uint8_t row = 0; row < 7; ++row) {
        uint8_t pixels = pgm_read_byte(&kCompactKnotsGlyphs[glyphNumber][row]);
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
    {0x1E,0x33,0x3F,0x33,0x33,0x33,0x33}, {0x3E,0x33,0x3E,0x33,0x33,0x33,0x3E},
    {0x1E,0x33,0x30,0x30,0x30,0x33,0x1E}, {0x3E,0x33,0x33,0x33,0x33,0x33,0x3E},
    {0x1F,0x18,0x1E,0x18,0x18,0x18,0x1F}, {0x1F,0x18,0x1E,0x18,0x18,0x18,0x18},
    {0x1F,0x30,0x37,0x33,0x33,0x33,0x1E}, {0x33,0x33,0x33,0x3F,0x33,0x33,0x33},
    {0x0F,0x06,0x06,0x06,0x06,0x06,0x0F}, {0x03,0x03,0x03,0x03,0x03,0x33,0x1E},
    {0x33,0x36,0x3C,0x36,0x33,0x33,0x33}, {0x18,0x18,0x18,0x18,0x18,0x18,0x1F},
    {0x33,0x3F,0x3F,0x33,0x33,0x33,0x33}, {0x33,0x3B,0x3F,0x37,0x33,0x33,0x33},
    {0x1E,0x33,0x33,0x33,0x33,0x33,0x1E}, {0x3E,0x33,0x3E,0x30,0x30,0x30,0x30},
    {0x1E,0x33,0x33,0x33,0x33,0x36,0x1F}, {0x3E,0x33,0x3E,0x33,0x33,0x33,0x33},
    {0x1F,0x30,0x1E,0x03,0x03,0x33,0x1E}, {0x3F,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x1E}, {0x33,0x33,0x33,0x33,0x1E,0x1E,0x0C},
    {0x33,0x33,0x33,0x33,0x3F,0x3F,0x33}, {0x33,0x1E,0x0C,0x1E,0x3F,0x33,0x33},
    {0x33,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C}, {0x1F,0x03,0x06,0x0C,0x18,0x18,0x1F},
};
constexpr uint8_t kMinecraftBoldCapsWidths[] PROGMEM = {
    6, 6, 6, 6, 5, 5, 6, 6, 4, 6, 6, 5, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5,
};

// The bold lowercase face is its own variable-width sheet. Keeping the
// source width with the bitmap prevents the last column of broad letters from
// being clipped while still giving narrow forms their intended breathing room.
constexpr uint8_t kMinecraftBoldLowercase[][7] PROGMEM = {
    {0x00,0x00,0x1E,0x03,0x1F,0x33,0x1F}, {0x30,0x30,0x3E,0x33,0x33,0x33,0x3E},
    {0x00,0x00,0x1E,0x33,0x30,0x33,0x1E}, {0x03,0x03,0x1F,0x33,0x33,0x33,0x1F},
    {0x00,0x00,0x1E,0x33,0x3F,0x30,0x1F}, {0x07,0x0C,0x1F,0x0C,0x0C,0x0C,0x0C},
    {0x00,0x1F,0x33,0x33,0x1F,0x03,0x3E}, {0x30,0x30,0x3E,0x33,0x33,0x33,0x33},
    {0x03,0x00,0x03,0x03,0x03,0x03,0x03}, {0x03,0x00,0x03,0x03,0x03,0x33,0x1E},
    {0x18,0x18,0x1B,0x1E,0x1C,0x1E,0x1B}, {0x06,0x06,0x06,0x06,0x06,0x06,0x03},
    {0x00,0x00,0x3E,0x3F,0x3F,0x33,0x33}, {0x00,0x00,0x3E,0x33,0x33,0x33,0x33},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E}, {0x00,0x00,0x3E,0x33,0x3E,0x30,0x30},
    {0x00,0x00,0x1F,0x33,0x1F,0x03,0x03}, {0x00,0x00,0x3E,0x33,0x30,0x30,0x30},
    {0x00,0x00,0x0F,0x18,0x0E,0x03,0x1E}, {0x06,0x06,0x0F,0x06,0x06,0x06,0x03},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x1F}, {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C},
    {0x00,0x00,0x33,0x33,0x33,0x3F,0x1F}, {0x00,0x00,0x33,0x1E,0x0C,0x1E,0x33},
    {0x00,0x00,0x33,0x33,0x1F,0x03,0x3E}, {0x00,0x00,0x3F,0x06,0x0C,0x18,0x3F},
};
constexpr uint8_t kMinecraftBoldLowercaseWidths[] PROGMEM = {
    6, 6, 6, 6, 6, 5, 6, 6, 3, 6, 5, 4, 6,
    6, 6, 6, 6, 6, 5, 4, 6, 6, 6, 6, 6, 6,
};

// Regular all-caps companion to the lowercase 7px body face. Airport codes
// alone use kMinecraftBoldCaps; telemetry and marquee text use this lighter set.
constexpr uint8_t kMinecraftNormalCaps[][7] PROGMEM = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x07,0x02,0x02,0x02,0x02,0x02,0x07},{0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x1C,0x12,0x11,0x11,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x11,0x15,0x15,0x0A},{0x11,0x0A,0x04,0x0A,0x11,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};
constexpr uint8_t kMinecraftNormalCapsWidths[] PROGMEM = {
    5, 5, 5, 5, 5, 5, 5, 5, 4, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

constexpr uint8_t kMinecraftBoldQuestion[7] PROGMEM = {
    0x0E, 0x1B, 0x03, 0x06, 0x0C, 0x00, 0x0C,
};
constexpr uint8_t kMinecraftBoldApostrophe[7] PROGMEM = {
    0x03, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00,
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

uint8_t normalGlyphWidth(char character) {
  if (character >= 'A' && character <= 'Z') {
    return pgm_read_byte(&kMinecraftNormalCapsWidths[character - 'A']);
  }
  if (character >= 'a' && character <= 'z') {
    return pgm_read_byte(&kMinecraftLowercaseWidths[character - 'a']);
  }
  if (character == '\'') return 2;
  return 5;
}

uint8_t normalGlyphAdvance(char character, bool compactZero = false) {
  return compactZero && character == '0' ? 5 : normalGlyphWidth(character) + 1;
}

int16_t normalTextWidth(const char* text) {
  int16_t width = -1;
  for (const char* character = text; *character; ++character) width += normalGlyphAdvance(*character);
  return width;
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
  const int16_t textWidth = normalTextWidth(text);
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
    const bool uppercase = *character >= 'A' && *character <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(*character - 'A')
                                    : minecraftGlyphIndex(*character);
    const uint8_t glyphWidth = normalGlyphWidth(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (glyphWidth - 1 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, y + row, color);
        }
      }
    }
    x += normalGlyphAdvance(*character++);
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
  int16_t width = -1;
  for (const char* cursor = text; *cursor;) {
    const CityGlyph glyph = nextCityGlyph(cursor);
    width += normalGlyphAdvance(glyph.base);
    cursor += glyph.bytes;
  }
  return width;
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
    // The generic normal-font dash sits one row too high for the route arrow.
    // City-route placeholders therefore use the airport route's shared
    // baseline, so --- -> --- reads as one clean horizontal composition.
    if (glyphInfo.base == '-') {
      if (x + 4 >= left && x < right) matrix.fillRect(x, y + 3, 5, 1, color);
      x += 6;
      cursor += glyphInfo.bytes;
      continue;
    }
    const bool uppercase = glyphInfo.base >= 'A' && glyphInfo.base <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(glyphInfo.base - 'A') : minecraftGlyphIndex(glyphInfo.base);
    const uint8_t glyphWidth = normalGlyphWidth(glyphInfo.base);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if ((pixels & (1 << (glyphWidth - 1 - column))) && x + column >= left && x + column < right) matrix.drawPixel(x + column, y + row, color);
      }
    }
    drawCityAccent(glyphInfo.accent, x, y, left, right, color);
    x += normalGlyphAdvance(glyphInfo.base);
    cursor += glyphInfo.bytes;
  }
}

void drawCityMarquee(const char* origin, const char* destination, int16_t left, int16_t y,
                     int16_t width, uint16_t color) {
  const int16_t originWidth = cityTextWidth(origin);
  // Airport-code and city routes deliberately use the same three-pixel air
  // on both sides of the shared six-pixel arrow.
  const int16_t textWidth = originWidth + 3 + 6 + 3 + cityTextWidth(destination);
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
  const int16_t arrowX = x + originWidth + 3;
  drawCityArrow(arrowX, y, left, left + width, color);
  const int16_t destinationX = arrowX + 9;
  drawCityText(destination, destinationX, y, left, left + width, color);
}

void drawRegistrationMarquee(const char* text, int16_t left, int16_t y, int16_t width,
                             uint16_t color) {
  int16_t textWidth = -1;
  for (const char* character = text; *character; ++character) {
    textWidth += *character == '-' ? 3 : normalGlyphAdvance(*character, true);
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
    const bool compactZero = *character == '0';
    const bool uppercase = *character >= 'A' && *character <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(*character - 'A')
                                    : minecraftGlyphIndex(*character);
    const uint8_t glyphWidth = compactZero ? 5 : normalGlyphWidth(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = compactZero ? pgm_read_byte(&kMinecraftTelemetryZero[row])
                                       : uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (glyphWidth - 1 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, y + row, color);
        }
      }
    }
    x += normalGlyphAdvance(*character, compactZero);
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
    if (*character == ' ') {
      x += 3;
      continue;
    }
    if (*character == '-') {
      matrix.fillRect(x, y + 3, 5, 1, color);
      x += 6;
      continue;
    }
    if (*character == '\'') {
      for (uint8_t row = 0; row < 7; ++row) {
        const uint8_t pixels = pgm_read_byte(&kMinecraftBoldApostrophe[row]);
        for (uint8_t column = 0; column < 2; ++column) {
          if (pixels & (1 << (1 - column))) matrix.drawPixel(x + column, y + row, color);
        }
      }
      x += 3;
      continue;
    }
    const bool isQuestion = *character == '?';
    const uint8_t glyph = (*character >= 'A' && *character <= 'Z')
                              ? static_cast<uint8_t>(*character - 'A')
                              : 0;
    const uint8_t glyphWidth = isQuestion ? 5 : pgm_read_byte(&kMinecraftBoldCapsWidths[glyph]);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = isQuestion ? pgm_read_byte(&kMinecraftBoldQuestion[row])
                                        : pgm_read_byte(&kMinecraftBoldCaps[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if (pixels & (1 << (glyphWidth - 1 - column))) {
          matrix.drawPixel(x + column, y + row, color);
        }
      }
    }
    x += glyphWidth + 1;
  }
  return x - 1;
}

void drawMinecraftBoldCapsCentered(const char* text, int16_t y, uint16_t color) {
  int16_t width = -1;
  for (const char* character = text; *character; ++character) {
    if (*character == ' ') {
      width += 3;
      continue;
    }
    if (*character == '\'') {
      width += 3;
      continue;
    }
    const uint8_t glyph = (*character >= 'A' && *character <= 'Z')
                              ? static_cast<uint8_t>(*character - 'A') : 0;
    width += ((*character == '-' || *character == '?')
                  ? 5 : pgm_read_byte(&kMinecraftBoldCapsWidths[glyph])) + 1;
  }
  drawMinecraftBoldCaps(text, (matrix.width() - width) / 2, y, color);
}

int16_t drawMinecraftBoldLowercase(const char* text, int16_t x, int16_t y, uint16_t color) {
  for (const char* character = text; *character; ++character) {
    if (*character == '-') {
      matrix.fillRect(x, y + 3, 5, 1, color);
      x += 6;
      continue;
    }
    const uint8_t glyph = (*character >= 'a' && *character <= 'z')
                              ? static_cast<uint8_t>(*character - 'a') : 0;
    const uint8_t glyphWidth = pgm_read_byte(&kMinecraftBoldLowercaseWidths[glyph]);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = pgm_read_byte(&kMinecraftBoldLowercase[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if (pixels & (1 << (glyphWidth - 1 - column))) {
          matrix.drawPixel(x + column, y + row, color);
        }
      }
    }
    x += glyphWidth + 1;
  }
  return x - 1;
}

int16_t boldGlyphAdvance(char character) {
  if (character == ' ') return 3;
  if (character == '-' || character == '?') return 6;
  if (character == '\'') return 3;
  if (character >= 'A' && character <= 'Z') return pgm_read_byte(&kMinecraftBoldCapsWidths[character - 'A']) + 1;
  if (character >= 'a' && character <= 'z') return pgm_read_byte(&kMinecraftBoldLowercaseWidths[character - 'a']) + 1;
  return 0;
}

int16_t boldTextWidth(const char* text) {
  int16_t width = 0;
  for (const char* character = text; *character; ++character) width += boldGlyphAdvance(*character);
  return width ? width - 1 : 0;
}

void drawMinecraftBoldText(const char* text, int16_t x, int16_t y, uint16_t color) {
  for (const char* character = text; *character; ++character) {
    if (*character == ' ') { x += 3; continue; }
    if (*character == '-') { matrix.fillRect(x, y + 3, 5, 1, color); x += 6; continue; }
    if (*character == '\'') {
      for (uint8_t row = 0; row < 7; ++row) {
        const uint8_t pixels = pgm_read_byte(&kMinecraftBoldApostrophe[row]);
        for (uint8_t column = 0; column < 2; ++column) {
          if (pixels & (1 << (1 - column))) matrix.drawPixel(x + column, y + row, color);
        }
      }
      x += 3;
      continue;
    }
    const bool upper = *character >= 'A' && *character <= 'Z';
    const bool lower = *character >= 'a' && *character <= 'z';
    if (!upper && !lower) continue;
    const uint8_t glyph = upper ? static_cast<uint8_t>(*character - 'A')
                                : static_cast<uint8_t>(*character - 'a');
    const uint8_t glyphWidth = upper ? pgm_read_byte(&kMinecraftBoldCapsWidths[glyph])
                                     : pgm_read_byte(&kMinecraftBoldLowercaseWidths[glyph]);
    for (uint8_t row = 0; row < 7; ++row) {
      const uint8_t pixels = upper ? pgm_read_byte(&kMinecraftBoldCaps[glyph][row])
                                   : pgm_read_byte(&kMinecraftBoldLowercase[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if (pixels & (1 << (glyphWidth - 1 - column))) matrix.drawPixel(x + column, y + row, color);
      }
    }
    x += glyphWidth + 1;
  }
}

void drawMinecraftBoldTextCentered(const char* text, int16_t centerX, int16_t y, uint16_t color) {
  drawMinecraftBoldText(text, centerX - boldTextWidth(text) / 2, y, color);
}

uint16_t visibleColor(uint8_t red, uint8_t green, uint8_t blue) {
  // This panel's red/blue channels are physically swapped.
  return matrix.color565(blue, green, red);
}

float easeInOut(float progress);

// A two-layer compositor used only during a changing-brand transition. Logo
// renderers continue to be simple pixel artists; this tiny gate reveals the
// incoming layer only behind the clockwise radar arm.
enum class BrandSweepLayer : uint8_t { None, Outgoing, Incoming };
BrandSweepLayer brandSweepLayer = BrandSweepLayer::None;
float brandSweepProgress = 0.0f;

bool brandSweepPaintsPixel(int16_t x, int16_t y) {
  if (brandSweepLayer == BrandSweepLayer::None) return true;
  const float dx = static_cast<float>(x - 16);
  const float dy = static_cast<float>(y - 19);
  float angle = atan2f(dx, -dy);  // 0 = top; positive turns clockwise.
  if (angle < 0.0f) angle += 2.0f * PI;
  const bool swept = angle <= brandSweepProgress * 2.0f * PI;
  return brandSweepLayer == BrandSweepLayer::Incoming ? swept : !swept;
}

void drawOperatorLogoPixel(int16_t x, int16_t y, uint16_t color) {
  if (brandSweepPaintsPixel(x, y)) matrix.drawPixel(x, y, color);
}

void drawWestJetLogo(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 32; ++row) for (uint8_t column = 0; column < 32; ++column) {
    const uint8_t index = pgm_read_byte(&kWestJetLogoPixels[row][column]) - '0';
    if (!index) continue;
    const uint8_t red = pgm_read_byte(&kWestJetLogoPalette[index][0]);
    const uint8_t green = pgm_read_byte(&kWestJetLogoPalette[index][1]);
    const uint8_t blue = pgm_read_byte(&kWestJetLogoPalette[index][2]);
    drawOperatorLogoPixel(x + column, y + row, matrix.color565(red, green, blue));
  }
}

void drawCargojetLogo(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 32; ++row) for (uint8_t column = 0; column < 32; ++column) {
    const uint8_t index = pgm_read_byte(&kCargojetLogoPixels[row][column]) - '0';
    if (!index) continue;
    const uint8_t red = pgm_read_byte(&kCargojetLogoPalette[index][0]);
    const uint8_t green = pgm_read_byte(&kCargojetLogoPalette[index][1]);
    const uint8_t blue = pgm_read_byte(&kCargojetLogoPalette[index][2]);
    drawOperatorLogoPixel(x + column, y + row, matrix.color565(red, green, blue));
  }
}

void drawAlbertaHealthLogo(int16_t x, int16_t y) {
  for (uint8_t row = 0; row < 32; ++row) {
    for (uint8_t column = 0; column < 32; ++column) {
      const uint8_t index = pgm_read_byte(&kAlbertaHealthLogoPixels[row][column]);
      if (!index) continue;
      const uint8_t red = pgm_read_byte(&kAlbertaHealthLogoPalette[index][0]);
      const uint8_t green = pgm_read_byte(&kAlbertaHealthLogoPalette[index][1]);
      const uint8_t blue = pgm_read_byte(&kAlbertaHealthLogoPalette[index][2]);
      // The source palette is sRGB (#009D4D green, #005CB9 blue). Route it
      // through the panel's physical red/blue channel correction just like
      // every other calibrated operator logo.
      drawOperatorLogoPixel(x + column, y + row, visibleColor(red, green, blue));
    }
  }
}

uint64_t readRcafMask(const uint64_t* masks, uint8_t row) {
  // ESP32's PROGMEM helpers expose 32-bit reads; combine the two native words
  // that make up one 33-bit raster row.
  const uint32_t* words = reinterpret_cast<const uint32_t*>(&masks[row]);
  return static_cast<uint64_t>(pgm_read_dword(words)) |
         (static_cast<uint64_t>(pgm_read_dword(words + 1)) << 32);
}

void drawRcafLogo(int16_t x, int16_t y) {
  const uint16_t navy = visibleColor(kRcafNavy[0], kRcafNavy[1], kRcafNavy[2]);
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t red = visibleColor(kRcafRed[0], kRcafRed[1], kRcafRed[2]);
  for (uint8_t row = 0; row < 33; ++row) {
    const uint64_t navyPixels = readRcafMask(kRcafRoundelNavyMask, row);
    const uint64_t whitePixels = readRcafMask(kRcafRoundelWhiteMask, row);
    const uint64_t redPixels = readRcafMask(kRcafRoundelRedMask, row);
    for (uint8_t column = 0; column < 33; ++column) {
      const uint64_t bit = 1ULL << (32 - column);
      if (navyPixels & bit) drawOperatorLogoPixel(x + column, y + row, navy);
      else if (whitePixels & bit) drawOperatorLogoPixel(x + column, y + row, white);
      else if (redPixels & bit) drawOperatorLogoPixel(x + column, y + row, red);
    }
  }
}

bool isAirCanadaFlight(const Route& route) {
  // Flight feeds use either the IATA prefix (AC) or the ICAO prefix (ACA).
  return strncmp(route.operatorCode, "ACA", 3) == 0 || strncmp(route.flightNumber, "AC", 2) == 0;
}

bool registrationsMatch(const char* first, const char* second) {
  // Providers vary between C-GLUN and CGLUN. Compare the meaningful letters
  // and digits only so a formatting difference never loses an override.
  if (!first || !*first) return false;
  while (*first || *second) {
    while (*first && !isalnum(static_cast<unsigned char>(*first))) ++first;
    while (*second && !isalnum(static_cast<unsigned char>(*second))) ++second;
    if (toupper(static_cast<unsigned char>(*first)) != toupper(static_cast<unsigned char>(*second))) {
      return false;
    }
    if (!*first) return true;
    ++first;
    ++second;
  }
  return true;
}

const TailOverride* tailOverrideFor(const Route& route) {
  for (size_t index = 0; index < kTailOverrideCount; ++index) {
    if (registrationsMatch(route.registration, kTailOverrides[index].registration)) {
      return &kTailOverrides[index];
    }
  }
  return nullptr;
}

bool isAirCanadaJazzFlight(const Route& route) {
  return strncmp(route.operatorCode, "JZA", 3) == 0 || strncmp(route.flightNumber, "JZA", 3) == 0;
}

bool isFlairFlight(const Route& route) {
  return strncmp(route.operatorCode, "FLE", 3) == 0 || strncmp(route.flightNumber, "FLE", 3) == 0;
}

bool isCargojetFlight(const Route& route) {
  return strncmp(route.operatorCode, "CJT", 3) == 0 || strncmp(route.flightNumber, "CJT", 3) == 0;
}

bool isRcafFlight(const Route& route) {
  // CFC is the RCAF's standard ADS-B callsign prefix. The numeric military
  // registration (such as 330004) is not used for carrier identification.
  return strncmp(route.operatorCode, "CFC", 3) == 0 || strncmp(route.flightNumber, "CFC", 3) == 0;
}

bool isNorthCaribooAirFlight(const Route& route) {
  return strncmp(route.operatorCode, "NCB", 3) == 0 || strncmp(route.flightNumber, "NCB", 3) == 0;
}

bool isWestJetFlight(const Route& route) {
  return strncmp(route.operatorCode, "WJA", 3) == 0 || strncmp(route.operatorCode, "WEN", 3) == 0 ||
         strncmp(route.flightNumber, "WJA", 3) == 0 || strncmp(route.flightNumber, "WEN", 3) == 0 ||
         strncmp(route.flightNumber, "WS", 2) == 0;
}

bool isCanWestFlight(const Route& route) {
  return strncmp(route.operatorCode, "CWA", 3) == 0 || strncmp(route.flightNumber, "CWA", 3) == 0;
}

bool isCentralMountainAirFlight(const Route& route) {
  return strncmp(route.operatorCode, "GLR", 3) == 0 || strncmp(route.flightNumber, "GLR", 3) == 0;
}

bool isCoywolfFlight(const Route& route) {
  return strncmp(route.operatorCode, "WPK", 3) == 0 || strncmp(route.flightNumber, "WPK", 3) == 0;
}

bool isAirSprintFlight(const Route& route) {
  return strncmp(route.operatorCode, "ASP", 3) == 0 || strncmp(route.flightNumber, "ASP", 3) == 0;
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
  if (const TailOverride* override = tailOverrideFor(route)) return override->operatorName;
  if (isWestJetFlight(route)) return strncmp(route.operatorCode, "WEN", 3) == 0 ||
                                      strncmp(route.flightNumber, "WEN", 3) == 0
                                      ? "WestJet Encore" : "WestJet";
  if (isAirCanadaJazzFlight(route)) return "Air Canada Jazz";
  if (isAirCanadaFlight(route)) return "Air Canada";
  if (isFlairFlight(route)) return "Flair Airlines";
  if (isCargojetFlight(route)) return "Cargojet";
  if (isRcafFlight(route)) return "Royal Canadian Air Force";
  if (isNorthCaribooAirFlight(route)) return "North Cariboo Air";
  if (isPorterFlight(route)) return "Porter";
  if (isCanWestFlight(route)) return "CanWest Air";
  if (isCentralMountainAirFlight(route)) return "Central Mountain Air";
  if (isCoywolfFlight(route)) return "Coywolf Aviation";
  if (isAirSprintFlight(route)) return "AirSprint";
  if (isRcafFlight(route)) return "Royal Canadian Air Force";
  if (isSunwestFlight(route)) return "Sunwest Aviation";
  if (isStarsFlight(route)) return "Stars Air Ambulance";
  if (isKlmFlight(route)) return "Koninklijke Luchtvaart Maatschappij";
  if (isUsMilitaryFlight(route)) return "United States Military";
  return route.operatorCode[0] ? route.operatorCode : "";
}

void operatorBrandRgb(const Route& route, uint8_t& red, uint8_t& green, uint8_t& blue) {
  if (const TailOverride* override = tailOverrideFor(route)) {
    red = override->brandColor[0]; green = override->brandColor[1]; blue = override->brandColor[2];
  } else if (isAirCanadaJazzFlight(route) || isAirCanadaFlight(route)) {
    red = kAirCanadaBrandColor[0]; green = kAirCanadaBrandColor[1]; blue = kAirCanadaBrandColor[2];
  } else if (isFlairFlight(route)) {
    red = kFlairBrandColor[0]; green = kFlairBrandColor[1]; blue = kFlairBrandColor[2];
  } else if (isCargojetFlight(route)) {
    red = 237; green = 27; blue = 45;  // Cargojet red, #ED1B2D.
  } else if (isRcafFlight(route)) {
    red = kRcafNavy[0]; green = kRcafNavy[1]; blue = kRcafNavy[2];  // #012169.
  } else if (isNorthCaribooAirFlight(route)) {
    red = kNorthCaribooAirBrandColor[0]; green = kNorthCaribooAirBrandColor[1]; blue = kNorthCaribooAirBrandColor[2];
  } else if (isCanWestFlight(route)) {
    red = kCanWestBrandColor[0]; green = kCanWestBrandColor[1]; blue = kCanWestBrandColor[2];
  } else if (isCentralMountainAirFlight(route)) {
    red = kCentralMountainAirBrandColor[0]; green = kCentralMountainAirBrandColor[1]; blue = kCentralMountainAirBrandColor[2];
  } else if (isCoywolfFlight(route)) {
    red = kCoywolfBrandColor[0]; green = kCoywolfBrandColor[1]; blue = kCoywolfBrandColor[2];
  } else if (isAirSprintFlight(route)) {
    red = kAirSprintBrandColor[0]; green = kAirSprintBrandColor[1]; blue = kAirSprintBrandColor[2];
  } else if (isRcafFlight(route)) {
    red = 0; green = 59; blue = 113;  // RCAF roundel/navy blue.
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
    // Keep the two-row ticket area reserved, but leave it unlit when we do
    // not know the operator rather than implying a generic white brand.
    red = 0; green = 0; blue = 0;
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
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, red);
    }
  }
}

void drawAirCanadaJazzLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kAirCanadaBrandColor[0], kAirCanadaBrandColor[1],
                                    kAirCanadaBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kAirCanadaJazzLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, red);
    }
  }
}

void drawFlairLogo(int16_t x, int16_t y) {
  const uint16_t green = visibleColor(kFlairBrandColor[0], kFlairBrandColor[1], kFlairBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kFlairLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      if (pixels & bit) drawOperatorLogoPixel(x + column, y + row, green);
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
      if (orangePixels & bit) drawOperatorLogoPixel(x + column, y + row, orange);
      else if (bluePixels & bit) drawOperatorLogoPixel(x + column, y + row, blue);
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
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, red);
    }
  }
}

void drawCoywolfLogo(int16_t x, int16_t y) {
  const uint16_t orange = visibleColor(kCoywolfBrandColor[0], kCoywolfBrandColor[1],
                                       kCoywolfBrandColor[2]);
  const uint16_t grey = visibleColor(kCoywolfWolfColor[0], kCoywolfWolfColor[1],
                                     kCoywolfWolfColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t orangePixels = pgm_read_dword(&kCoywolfLogoMask[row][0]);
    const uint32_t greyPixels = pgm_read_dword(&kCoywolfLogoMask[row][1]);
    for (uint8_t column = 0; column < 32; ++column) {
      const uint32_t bit = 1UL << (31 - column);
      if (orangePixels & bit) drawOperatorLogoPixel(x + column, y + row, orange);
      else if (greyPixels & bit) drawOperatorLogoPixel(x + column, y + row, grey);
    }
  }
}

void drawAirSprintLogo(int16_t x, int16_t y) {
  const uint16_t blue = visibleColor(kAirSprintBrandColor[0], kAirSprintBrandColor[1],
                                     kAirSprintBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kAirSprintLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, blue);
    }
  }
}

void drawNorthCaribooAirLogo(int16_t x, int16_t y) {
  const uint16_t gold = visibleColor(kNorthCaribooAirBrandColor[0],
                                     kNorthCaribooAirBrandColor[1],
                                     kNorthCaribooAirBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kNorthCaribooAirLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, gold);
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
        drawOperatorLogoPixel(x + column, y + row, color);
        drawOperatorLogoPixel(x + 31 - column, y + row, color);
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
      if (redPixels & bit) drawOperatorLogoPixel(x + column, y + row, red);
      else if (navyPixels & bit) drawOperatorLogoPixel(x + column, y + row, navy);
    }
  }
}

void drawStarsLogo(int16_t x, int16_t y) {
  const uint16_t red = visibleColor(kStarsBrandColor[0], kStarsBrandColor[1],
                                    kStarsBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kStarsLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, red);
    }
  }
}

void drawKlmLogo(int16_t x, int16_t y) {
  const uint16_t blue = visibleColor(kKlmBrandColor[0], kKlmBrandColor[1], kKlmBrandColor[2]);
  for (uint8_t row = 0; row < 32; ++row) {
    const uint32_t pixels = pgm_read_dword(&kKlmLogoMask[row]);
    for (uint8_t column = 0; column < 32; ++column) {
      if (pixels & (1UL << (31 - column))) drawOperatorLogoPixel(x + column, y + row, blue);
    }
  }
}

void drawUsMilitaryLogo(int16_t x, int16_t y) {
  const uint16_t colors[3] = {visibleColor(5, 25, 105), matrix.color565(255, 255, 255), visibleColor(220, 20, 30)};
  for (uint8_t row = 0; row < 32; ++row) for (uint8_t column = 0; column < 32; ++column) {
    const uint32_t bit = 1UL << (31 - column);
    for (uint8_t color = 0; color < 3; ++color) {
      if (pgm_read_dword(&kUsMilitaryRoundelMask[row][color]) & bit) { drawOperatorLogoPixel(x + column, y + row, colors[color]); break; }
    }
  }
}

void drawPlaneSprite(int16_t x, int16_t y, uint16_t color);
void drawPlaneSpritePointingUp(int16_t x, int16_t y, uint16_t color);
void drawAircraftFallbackSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color);
void drawAircraftPanelSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color);

// A neutral identity mark for aircraft whose operator is unknown. This is the
// supplied crisp 32x32 globe sprite: black is transparent, while its white
// strokes are rendered at 50% brightness so the fallback stays subordinate to
// a known operator logo. The aircraft type silhouette remains at upper right.
void drawUnknownBrandGlobe(int16_t x, int16_t y) {
  const uint16_t grey = visibleColor(128, 128, 128);
  for (uint8_t row = 0; row < 32; ++row) {
    for (uint8_t column = 0; column < 32; ++column) {
      if (pgm_read_byte(&kUnknownBrandGlobePixels[row][column])) {
        drawOperatorLogoPixel(x + column, y + row, grey);
      }
    }
  }
}

void drawOperatorLogo(const Route& route, int16_t x, int16_t y) {
  if (const TailOverride* override = tailOverrideFor(route)) {
    if (override->logo == TailOverrideLogo::AlbertaHealth) drawAlbertaHealthLogo(x, y);
  } else if (isAirCanadaJazzFlight(route)) drawAirCanadaJazzLogo(x, y);
  else if (isAirCanadaFlight(route)) drawAirCanadaLogo(x, y);
  else if (isFlairFlight(route)) drawFlairLogo(x, y);
  else if (isCargojetFlight(route)) drawCargojetLogo(x, y);
  else if (isRcafFlight(route)) drawRcafLogo(0, y);
  else if (isCanWestFlight(route)) drawCanWestLogo(x, y);
  else if (isCentralMountainAirFlight(route)) drawCentralMountainAirLogo(x, y);
  else if (isCoywolfFlight(route)) drawCoywolfLogo(x, y);
  else if (isAirSprintFlight(route)) drawAirSprintLogo(x, y);
  else if (isNorthCaribooAirFlight(route)) drawNorthCaribooAirLogo(x, y);
  else if (isPorterFlight(route)) drawPorterLogo(x, y);
  else if (isSunwestFlight(route)) drawSunwestLogo(x, y);
  else if (isStarsFlight(route)) drawStarsLogo(x, y);
  else if (isKlmFlight(route)) drawKlmLogo(x, y);
  else if (isUsMilitaryFlight(route)) drawUsMilitaryLogo(x, y);
  else if (isWestJetFlight(route)) drawWestJetLogo(x, y);
  else drawUnknownBrandGlobe(x, y);
}

const char* brandIdentity(const Route& route) {
  if (tailOverrideFor(route)) return "alberta-health";
  if (isAirCanadaJazzFlight(route)) return "air-canada-jazz";
  if (isAirCanadaFlight(route)) return "air-canada";
  if (isFlairFlight(route)) return "flair";
  if (isCargojetFlight(route)) return "cargojet";
  if (isRcafFlight(route)) return "rcaf";
  if (isCanWestFlight(route)) return "canwest";
  if (isCentralMountainAirFlight(route)) return "central-mountain";
  if (isCoywolfFlight(route)) return "coywolf";
  if (isAirSprintFlight(route)) return "airsprint";
  if (isNorthCaribooAirFlight(route)) return "north-cariboo";
  if (isPorterFlight(route)) return "porter";
  if (isSunwestFlight(route)) return "sunwest";
  if (isStarsFlight(route)) return "stars";
  if (isKlmFlight(route)) return "klm";
  if (isUsMilitaryFlight(route)) return "us-military";
  if (isWestJetFlight(route)) return "westjet";
  return "unknown";
}

bool brandChanges(const Route& outgoing, const Route& incoming) {
  return strcmp(brandIdentity(outgoing), brandIdentity(incoming)) != 0;
}

void drawBrandRadarRay(float angle, uint16_t color) {
  // Drawing an "invisible" ray as black erases the artwork underneath it.
  // Treat black as transparent so every radar ray is a true overlay.
  if (color == matrix.color565(0, 0, 0)) return;
  constexpr int16_t centerX = 16;
  constexpr int16_t centerY = 19;
  for (uint8_t radius = 0; radius <= 23; ++radius) {
    const int16_t x = centerX + static_cast<int16_t>(roundf(sinf(angle) * radius));
    const int16_t y = centerY - static_cast<int16_t>(roundf(cosf(angle) * radius));
    if (x >= 0 && x < 33 && y >= 3 && y < 36) matrix.drawPixel(x, y, color);
  }
}

// The panel has no alpha compositor, so the green afterglow is quantized into
// a few dim rays. It is intentionally painted over either logo, like a real
// radar phosphor trail, while the incoming badge is revealed behind the arm.
void drawBrandRadarSweep(const Route& outgoing, const Route& incoming, unsigned long elapsed) {
  if (!brandChanges(outgoing, incoming)) return;
  const float progress = constrain(static_cast<float>(elapsed) / 2000.0f, 0.0f, 1.0f);
  // Travel one and a half turns in the same two seconds: a quarter-turn fade
  // in before the top-centre start, one full reveal turn, then a quarter-turn
  // fade out after completion. The two bookends never change logo ownership.
  const float turns = -0.25f + progress * 1.5f;
  matrix.fillRect(0, 3, 33, 33, matrix.color565(0, 0, 0));
  float rayOpacity = 1.0f;
  const bool revealing = turns >= 0.0f && turns <= 1.0f;
  if (revealing) {
    brandSweepProgress = turns;
    brandSweepLayer = BrandSweepLayer::Outgoing;
    drawOperatorLogo(outgoing, 1, 3);
    brandSweepLayer = BrandSweepLayer::Incoming;
    drawOperatorLogo(incoming, 1, 3);
  } else if (turns < 0.0f) {
    // Fade in over the preceding quarter-turn while leaving the outgoing logo
    // completely intact beneath the radar line.
    brandSweepLayer = BrandSweepLayer::None;
    drawOperatorLogo(outgoing, 1, 3);
    rayOpacity = easeInOut((turns + 0.25f) / 0.25f);
  } else {
    // The outgoing logo has fully handed off. Keep the incoming one intact
    // through the exit quarter-turn, then fade the overlay away.
    brandSweepLayer = BrandSweepLayer::None;
    drawOperatorLogo(incoming, 1, 3);
    rayOpacity = 1.0f - easeInOut((turns - 1.0f) / 0.25f);
  }
  brandSweepLayer = BrandSweepLayer::None;

  const float angle = turns * 2.0f * PI;
  if (revealing) {
    for (uint8_t trail = 7; trail > 0; --trail) {
      const float trailStrength = (8 - trail) / 16.0f;
      drawBrandRadarRay(angle - trail * 0.075f,
                        visibleColor(0, static_cast<uint8_t>(64 * trailStrength),
                                     static_cast<uint8_t>(16 * trailStrength)));
    }
  }
  drawBrandRadarRay(angle, visibleColor(0, static_cast<uint8_t>(145 * rayOpacity),
                                        static_cast<uint8_t>(36 * rayOpacity)));
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

void drawRouteProgress(float progress, float opacity = 1.0f, float delayPercent = -1.0f,
                       bool delayElapsed = false) {
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
  if (delayPercent > 0.0f) {
    const uint8_t delayPixels = max<uint8_t>(1, min<uint8_t>(matrix.width(),
        static_cast<uint8_t>(ceilf(delayPercent * matrix.width() / 100.0f))));
    const uint8_t red = static_cast<uint8_t>((delayElapsed ? 255 : 26) * opacity);
    matrix.fillRect(matrix.width() - delayPixels, matrix.height() - 1, delayPixels, 1,
                    visibleColor(red, 0, 0));
  }
}

void drawRouteProgressForRoute(const Route& route, float progress, float opacity = 1.0f) {
  time_t now = 0;
  time(&now);
  drawRouteProgress(progress, opacity, route.arrivalDelayPercent,
                    route.estimatedArrivalEpoch > 0 && now >= route.estimatedArrivalEpoch);
}

void drawRouteChrome(const Route& route, float progressOverride = -2.0f, float progressOpacity = 1.0f) {
  uint8_t red, green, blue;
  operatorBrandRgb(route, red, green, blue);
  matrix.fillRect(0, 0, matrix.width(), 2, visibleColor(red, green, blue));
  matrix.drawPixel(0, 0, visibleColor(red / 10, green / 10, blue / 10));
  matrix.drawPixel(matrix.width() - 1, 0, visibleColor(red / 10, green / 10, blue / 10));
  matrix.fillRect(0, 3, 33, 33, matrix.color565(0, 0, 0));
  // Aircraft source art is exactly 33×33. It deliberately occupies x=95…127,
  // borrowing the outer display-edge gutter so its sharp nose, tail, and wing
  // endpoints remain pixel-for-pixel intact.
  matrix.fillRect(95, 3, 33, 33, matrix.color565(0, 0, 0));
  // x=33 is the deliberately-off spacer between the 32px badge and the
  // centre text. It is not otherwise painted, so explicitly clear it to
  // prevent an old incremental frame leaving a bright sleeping pixel there.
  matrix.fillRect(33, 2, 1, matrix.height() - 3, matrix.color565(0, 0, 0));
  // The other deliberately-off gutter separates the city marquee from the
  // telemetry column. Clear it on every card paint so incremental drawing
  // can never leave a coloured pixel between those two regions.
  matrix.fillRect(94, 2, 1, matrix.height() - 3, matrix.color565(0, 0, 0));
  drawOperatorLogo(route, 1, 3);
  drawAircraftPanelSprite(route.aircraftType, 95, 3, matrix.color565(64, 64, 64));
  drawRouteProgressForRoute(route, progressOverride > -1.5f ? progressOverride : route.flightProgressPercent,
                           progressOpacity);
}

uint8_t rotationIndicatorPipForRoute(uint8_t index) {
  return min<uint8_t>(index, kMaxRoutableFlights - 1);
}

int16_t rotationIndicatorPipOffset(uint8_t index) {
  // Sixteen comfortably fit in 32px with a dark LED between every pair.
  // Only a genuinely dense 17–20 aircraft queue gives up that breathing room.
  return routeCount <= 16 ? index * 2 : index;
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

  // Keep the original 1px gap through 16 aircraft. Above that, fit each of
  // the remaining positions contiguously rather than hiding aircraft.
  const uint8_t pipCount = min<uint8_t>(routeCount, kMaxRoutableFlights);
  const uint8_t activePip = rotationIndicatorPipForRoute(routeIndex);
  for (uint8_t pip = 0; pip < pipCount; ++pip) {
    const bool stableActive = !rotationPipTransitionActive && pip == activePip;
    matrix.drawPixel(x + rotationIndicatorPipOffset(pip), y + 1, stableActive ? brightPink : dimPink);
  }

  if (rotationPipTransitionActive) {
    const unsigned long elapsed = millis() - rotationPipTransitionStartMillis;
    int16_t bubbleX = x + rotationIndicatorPipOffset(rotationPipFrom);
    if (rotationPipTo > rotationPipFrom) {
      // In the spaced mode, the dark LED between pips is the bubble's travel
      // lane. Dense mode settles directly into the adjacent position.
      if (routeCount <= 16 && elapsed >= kRotationPipBubbleMillis / 3 && elapsed < 2 * kRotationPipBubbleMillis / 3) {
        bubbleX += 1;
      } else if (elapsed >= 2 * kRotationPipBubbleMillis / 3) {
        bubbleX = x + rotationIndicatorPipOffset(rotationPipTo);
      }
    } else if (rotationPipTo < rotationPipFrom) {
      // The last-to-first transition loops through the otherwise-unused outer
      // pixels, retaining the same little travelling-bubble feel.
      const uint8_t phase = min<uint8_t>(3, elapsed * 4 / kRotationPipBubbleMillis);
      static constexpr int8_t kWrapOffsets[] = {0, 1, 2, -1};
      bubbleX = x + rotationIndicatorPipOffset(rotationPipFrom) + kWrapOffsets[phase];
      if (phase == 3) bubbleX = x + rotationIndicatorPipOffset(rotationPipTo);
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
  if (strcmp(code, "???") == 0) return "???";
  for (size_t index = 0; index < kAirportCityCount; ++index) {
    if (strcmp(code, kAirportCities[index].code) == 0) return kAirportCities[index].city;
  }
  return "???";
}

const char* displayAircraftType(const Route& route) {
  const char* type = route.aircraftType;
  // Some feeds use the literal word TAIL as a placeholder rather than an
  // ICAO type. Treat it exactly like missing data on the display.
  const bool tailPlaceholder = strlen(type) == 4 &&
      toupper(type[0]) == 'T' && toupper(type[1]) == 'A' &&
      toupper(type[2]) == 'I' && toupper(type[3]) == 'L';
  return type[0] && !tailPlaceholder ? type : "-----";
}

void drawAirportRoute(const Route& route, int16_t x, int16_t y, uint16_t color) {
  // drawMinecraftBoldCaps returns the final advance column. Leave exactly three
  // dark columns before and after the route arrow, matching drawCityMarquee.
  x = drawMinecraftBoldCaps(route.origin, x, y, color) + 3;
  for (uint8_t row = 0; row < 5; ++row) {
    for (uint8_t column = 0; column < 6; ++column) {
      if (kCityArrowSprite[row] & (1 << (5 - column))) matrix.drawPixel(x + column, y + row + 1, color);
    }
  }
  drawMinecraftBoldCaps(route.destination, x + 9, y, color);
}

int16_t registrationTextWidth(const char* text) {
  int16_t width = -1;
  for (const char* character = text; *character; ++character) {
    width += *character == '-' ? 3 : normalGlyphAdvance(*character, true);
  }
  return width;
}

void refreshClockMinutesSeconds(char output[6]) {
  if (strlen(lastFlightRefreshTime) == 8) {
    output[0] = lastFlightRefreshTime[3];
    output[1] = lastFlightRefreshTime[4];
    output[2] = ':';
    output[3] = lastFlightRefreshTime[6];
    output[4] = lastFlightRefreshTime[7];
    output[5] = '\0';
  } else {
    strlcpy(output, "--:--", 6);
  }
}

void refreshCountdownSeconds(char output[3]) {
  const int32_t remainingMillis = static_cast<int32_t>(nextFlightPollMillis - millis());
  const uint8_t remainingSeconds = remainingMillis > 0
      ? min<int32_t>(99, (remainingMillis + 999) / 1000) : 0;
  output[0] = '0' + remainingSeconds / 10;
  output[1] = '0' + remainingSeconds % 10;
  output[2] = '\0';
}

// This temporary line makes a stopped fetch distinguishable from an empty
// successful response: orange is the last accepted UTC minute:second, red is
// the seconds until the scheduler's next attempt.
void drawRefreshDebugLine() {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t orange = visibleColor(255, 128, 0);
  const uint16_t red = visibleColor(235, 40, 40);
  char refreshTime[6];
  char countdown[3];
  refreshClockMinutesSeconds(refreshTime);
  refreshCountdownSeconds(countdown);
  matrix.fillRect(34, 52, 60, 7, black);
  drawMinecraftMarquee(refreshTime, 34, 52, 29, orange);
  drawMinecraftMarquee(countdown, 66, 52, 12, red);
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
  matrix.fillRect(95, 37, 32, 7, black);
  matrix.fillRect(95, 47, 32, 7, black);
  matrix.fillRect(34, 16, 60, 7, black);
  matrix.fillRect(34, 28, 60, 7, black);
  matrix.fillRect(34, 40, 60, 7, black);
  matrix.fillRect(34, 52, 60, 7, black);
  drawRegistrationMarquee(registration, 95, 37, 32, secondaryWhite);
  drawMinecraftMarquee(displayAircraftType(route), 95, 47, 32, secondaryWhite);
  drawCityMarquee(originCity, destinationCity, 34, 16, 60, secondaryWhite);
  drawMinecraftMarquee(route.flightNumber[0] ? route.flightNumber : "-----", 34, 28, 60, tertiaryWhite);
  drawMinecraftMarquee(airlineName(route), 34, 40, 60, tertiaryWhite);
  if (route.specialFlight && route.specialFlightTitle[0]) {
    drawMinecraftMarquee(route.specialFlightTitle, 34, 52, 60, visibleColor(255, 128, 0));
  }
}

// The initial card render paints every label. Subsequent 75ms refreshes paint
// only fields which actually overflow their slot, preventing short labels from
// being cleared and re-drawn (the visible source of their flicker).
void refreshActiveMarquees(const Route& route) {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t secondaryWhite = matrix.color565(145, 145, 145);
  const uint16_t tertiaryWhite = matrix.color565(85, 85, 85);
  // Keep the unpainted badge/text gutter deterministic across all partial
  // marquee redraws, rather than relying on a future full-screen render.
  matrix.fillRect(33, 2, 1, matrix.height() - 3, black);
  matrix.fillRect(94, 2, 1, matrix.height() - 3, black);
  char registration[sizeof(route.registration)];
  strlcpy(registration, route.registration[0] ? route.registration : "UNKNOWN", sizeof(registration));
  for (char* character = registration; *character; ++character) *character = toupper(*character);
  const char* originCity = airportCity(route.origin);
  const char* destinationCity = airportCity(route.destination);
  const char* aircraftType = displayAircraftType(route);
  const char* flightNumber = route.flightNumber[0] ? route.flightNumber : "-----";
  const char* operatorName = airlineName(route);
  const char* specialTitle = route.specialFlight ? route.specialFlightTitle : "";
  if (registrationTextWidth(registration) > 32 && marqueeIsMoving(registrationTextWidth(registration), 32)) {
    matrix.fillRect(95, 37, 32, 7, black);
    drawRegistrationMarquee(registration, 95, 37, 32, secondaryWhite);
  }
  if (normalTextWidth(aircraftType) > 32 && marqueeIsMoving(normalTextWidth(aircraftType), 32)) {
    matrix.fillRect(95, 47, 32, 7, black);
    drawMinecraftMarquee(aircraftType, 95, 47, 32, secondaryWhite);
  }
  const int16_t cityWidth = cityTextWidth(originCity) + 3 + 6 + 3 + cityTextWidth(destinationCity);
  if (cityWidth > 60 && marqueeIsMoving(cityWidth, 60)) {
    matrix.fillRect(34, 16, 60, 7, black);
    drawCityMarquee(originCity, destinationCity, 34, 16, 60, secondaryWhite);
  }
  if (normalTextWidth(flightNumber) > 60 && marqueeIsMoving(normalTextWidth(flightNumber), 60)) {
    matrix.fillRect(34, 28, 60, 7, black);
    drawMinecraftMarquee(flightNumber, 34, 28, 60, tertiaryWhite);
  }
  if (normalTextWidth(operatorName) > 60 && marqueeIsMoving(normalTextWidth(operatorName), 60)) {
    matrix.fillRect(34, 40, 60, 7, black);
    drawMinecraftMarquee(operatorName, 34, 40, 60, tertiaryWhite);
  }
  if (specialTitle[0] && normalTextWidth(specialTitle) > 60 && marqueeIsMoving(normalTextWidth(specialTitle), 60)) {
    matrix.fillRect(34, 52, 60, 7, black);
    drawMinecraftMarquee(specialTitle, 34, 52, 60, visibleColor(255, 128, 0));
  }
  // Telemetry normally stays completely still. Only the exceptional values
  // wider than their 32-pixel slot are re-rendered as a marquee.
  char altitude[12], verticalSpeed[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt); else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);

  const uint16_t telemetry = matrix.color565(85, 85, 85);
  const struct { const char* value; TelemetryUnit unit; bool arrow; bool climbing; bool level; int16_t y; } lines[] = {
      {altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None, false, false, false, 37},
      {verticalSpeed, TelemetryUnit::None, route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0, route.verticalSpeedFpm == 0, 47},
  };
  for (const auto& line : lines) {
    const int16_t lineWidth = telemetryLineWidth(line.value, line.unit, line.arrow);
    if (lineWidth > 32 && marqueeIsMoving(lineWidth, 32)) {
      matrix.fillRect(1, line.y, 32, 7, black);
      const int16_t offset = telemetryMarqueeOffset(lineWidth, 32);
      const int16_t end = drawTelemetryValue(line.value, line.unit, 1 - offset, line.y, telemetry, 1, 33);
      if (line.arrow) {
        if (line.level) drawCityArrow(end + 1, line.y, 1, 33, telemetry);
        else drawVerticalRouteArrow(end + 1, line.y, line.climbing, telemetry, 1, 33);
      }
    }
  }
}

int16_t drawTelemetryLine(const char* value, TelemetryUnit unit, bool directionArrow,
                          bool climbing, bool level, int16_t left, int16_t right,
                          int16_t y, uint16_t color) {
  const int16_t width = right - left;
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
  matrix.fillRect(1, 37, 32, 7, black);
  matrix.fillRect(1, 47, 32, 7, black);
  // The former right-column telemetry rows are now occupied by the aircraft
  // panel and its two captions. Clear the remaining lower area on every card
  // paint so a prior speed, track, or ETA can never survive the layout swap.
  matrix.fillRect(95, 35, 32, 25, black);

  char altitude[12], verticalSpeed[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt);
  else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);
  drawTelemetryLine(altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None,
                    false, false, false, 1, 33, 37, text);
  drawTelemetryLine(verticalSpeed, TelemetryUnit::None,
                    route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0,
                    route.verticalSpeedFpm == 0, 1, 33, 47, text);
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

// The fallback library intentionally has only fifteen planform categories.
// At 33px, engine count, rotor layout, and broad wing geometry communicate
// more honestly than an over-specific model silhouette.
enum class AircraftSprite : uint8_t {
  SingleEngineProp,
  TwinEngineProp,
  FourEngineProp,
  TwinEngineJet,
  TriEngineJet,
  QuadEngineJet,
  SingleRotorHelicopter,
  DualRotorHelicopter,
  Stratofortress,
  Osprey,
  Glider,
  B2Dorito,
  FighterJet,
  GenericPlane,
  Balloon,
};

void drawPlaneSprite(int16_t x, int16_t y, uint16_t color);
void drawPlaneSpritePointingUp(int16_t x, int16_t y, uint16_t color);

bool typeContains(const char* type, const char* needle) {
  return type && strstr(type, needle) != nullptr;
}

AircraftSprite aircraftSpriteForType(const char* type) {
  if (typeContains(type, "V22") || typeContains(type, "CV22") || typeContains(type, "MV22")) {
    return AircraftSprite::Osprey;
  }
  if (typeContains(type, "B2A") || typeContains(type, "B2 ")) return AircraftSprite::B2Dorito;
  if (typeContains(type, "B52") || typeContains(type, "B-52")) return AircraftSprite::Stratofortress;
  if (typeContains(type, "GLID") || typeContains(type, "ASK") || typeContains(type, "ASW") ||
      typeContains(type, "DG1") || typeContains(type, "DG5") || typeContains(type, "LS") ||
      typeContains(type, "SZD")) return AircraftSprite::Glider;
  if (typeContains(type, "BALLOON") || typeContains(type, "BALL") || typeContains(type, "HBAL")) {
    return AircraftSprite::Balloon;
  }
  if (typeContains(type, "CH47") || typeContains(type, "CH46") || typeContains(type, "CH-47") ||
      typeContains(type, "CH-46")) return AircraftSprite::DualRotorHelicopter;
  // Helicopters first: some have manufacturer strings that otherwise resemble
  // fixed-wing types (for example Bell and Airbus Helicopters).
  if (typeContains(type, "H135") || typeContains(type, "H145") || typeContains(type, "H160") ||
      typeContains(type, "EC") || typeContains(type, "AS") || typeContains(type, "BK") ||
      typeContains(type, "B06") || typeContains(type, "BELL") || typeContains(type, "R22") ||
      typeContains(type, "R44") || typeContains(type, "S76") || typeContains(type, "AW")) {
    return AircraftSprite::SingleRotorHelicopter;
  }
  // Military fast jets have intentionally distinct delta planforms.
  if (typeContains(type, "F16") || typeContains(type, "F18") || typeContains(type, "FA18") ||
      typeContains(type, "F22") || typeContains(type, "F35") || typeContains(type, "F15") ||
      typeContains(type, "F14") || typeContains(type, "F5") || typeContains(type, "A10") ||
      typeContains(type, "MIG") || typeContains(type, "SU2") || typeContains(type, "SU3") ||
      typeContains(type, "RAFALE") || typeContains(type, "TYPN")) return AircraftSprite::FighterJet;
  // Four-engine turboprops are rare, but their prop discs are the defining cue.
  if (typeContains(type, "C130") || typeContains(type, "C-130") || typeContains(type, "P3") ||
      typeContains(type, "P-3") || typeContains(type, "L188") || typeContains(type, "DHC7") ||
      typeContains(type, "A400") || typeContains(type, "AN12") || typeContains(type, "AN22")) {
    return AircraftSprite::FourEngineProp;
  }
  // Piston singles and single-engine utility props, including the Cessna Caravan.
  if (typeContains(type, "C208") || typeContains(type, "C20") || typeContains(type, "C1") ||
      typeContains(type, "C2") || typeContains(type, "C3") || typeContains(type, "C4") ||
      typeContains(type, "C8") || typeContains(type, "PA") || typeContains(type, "SR2") ||
      typeContains(type, "DA4") || typeContains(type, "DA6")) return AircraftSprite::SingleEngineProp;
  // Dash 8, ATR, Saab, Beech and other commuter twins retain one-pixel prop discs.
  if (typeContains(type, "DH8") || typeContains(type, "DHC") || typeContains(type, "AT4") ||
      typeContains(type, "ATR") || typeContains(type, "SF3") || typeContains(type, "BE2") ||
      typeContains(type, "BE3") || typeContains(type, "PC1") || typeContains(type, "C40") ||
      typeContains(type, "C44") || typeContains(type, "SW4") || typeContains(type, "B350") ||
      typeContains(type, "B190")) {
    return AircraftSprite::TwinEngineProp;
  }
  if (typeContains(type, "B72") || typeContains(type, "B727") || typeContains(type, "MD1") ||
      typeContains(type, "DC10") || typeContains(type, "L101")) return AircraftSprite::TriEngineJet;
  // The 747/A340/A380/Il-96 and four-engined freighters retain four nacelles.
  if (typeContains(type, "B74") || typeContains(type, "A34") || typeContains(type, "A38") ||
      typeContains(type, "IL9") || typeContains(type, "IL7") || typeContains(type, "C17") ||
      typeContains(type, "C-17") || typeContains(type, "A124") || typeContains(type, "A225") ||
      typeContains(type, "C5A") || typeContains(type, "C5B") || typeContains(type, "C5M")) {
    return AircraftSprite::QuadEngineJet;
  }
  // All normal two-engined jets share one swept-wing planform at this scale.
  if (typeContains(type, "B77") || typeContains(type, "B78") || typeContains(type, "A33") ||
      typeContains(type, "A35") || typeContains(type, "B73") || typeContains(type, "A31") ||
      typeContains(type, "A32") || typeContains(type, "A21") || typeContains(type, "B75") ||
      typeContains(type, "B71") || typeContains(type, "MD8") || typeContains(type, "DC9") ||
      typeContains(type, "E1") || typeContains(type, "E7") || typeContains(type, "E9") ||
      typeContains(type, "CRJ") || typeContains(type, "ARJ") || typeContains(type, "SU9") ||
      typeContains(type, "F70") || typeContains(type, "F10") || typeContains(type, "BAE") ||
      typeContains(type, "C5") || typeContains(type, "C6") || typeContains(type, "C7") ||
      typeContains(type, "C68") || typeContains(type, "CL3") || typeContains(type, "CL6") ||
      typeContains(type, "GLF") || typeContains(type, "LJ") || typeContains(type, "FA") ||
      typeContains(type, "H25") || typeContains(type, "E35")) return AircraftSprite::TwinEngineJet;
  return AircraftSprite::GenericPlane;
}

void drawAircraftSprite(AircraftSprite sprite, int16_t x, int16_t y, uint16_t color) {
  const uint8_t spriteIndex = static_cast<uint8_t>(sprite);
  for (uint8_t row = 0; row < kAircraftSpriteSize; ++row) {
    const uint64_t pixels = kAircraftSpriteMasks[spriteIndex][row];
    for (uint8_t column = 0; column < kAircraftSpriteSize; ++column) {
      if (pixels & (1ULL << (32 - column))) matrix.drawPixel(x + column, y + row, color);
    }
  }
}

void drawPlaneSprite(int16_t x, int16_t y, uint16_t color) {
  // The source PNG is nose-up; rotate it clockwise for the moving no-planes scene.
  for (uint8_t row = 0; row < kAircraftSpriteSize; ++row) {
    const uint64_t pixels = kAircraftSpriteMasks[static_cast<uint8_t>(AircraftSprite::GenericPlane)][row];
    for (uint8_t column = 0; column < kAircraftSpriteSize; ++column) {
      if (pixels & (1ULL << (32 - column))) matrix.drawPixel(x + 32 - row, y + column, color);
    }
  }
}

void drawPlaneSpritePointingUp(int16_t x, int16_t y, uint16_t color) {
  drawAircraftSprite(AircraftSprite::GenericPlane, x, y, color);
}

void drawAircraftFallbackSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color) {
  drawAircraftSprite(aircraftSpriteForType(aircraftType), x, y, color);
}

// Aircraft source sprites are deliberately 33×33, with their nose and tail on
// the centreline. The upper-right art region bleeds into the display's outer
// edge, so use every original pixel without resampling or clipping.
void drawAircraftPanelSprite(const char* aircraftType, int16_t x, int16_t y, uint16_t color) {
  drawAircraftSprite(aircraftSpriteForType(aircraftType), x, y, color);
}

// Fallback badges deliberately stay top-down. The no-planes animation is
// different: it travels left-to-right, so rotate each top-down silhouette
// clockwise and put its nose in the direction of travel.
void drawNoPlanesSpritePixel(int16_t x, int16_t y, uint8_t sourceX, uint8_t sourceY,
                             uint16_t color) {
  matrix.drawPixel(x + 32 - sourceY, y + sourceX, color);
}

void drawNoPlanesAircraftSprite(AircraftSprite sprite, int16_t x, int16_t y, uint16_t color) {
  // Use the same editable source PNGs as the fallback badge, then rotate the
  // nose-up sprite clockwise so it faces in the direction of travel.
  const uint8_t spriteIndex = static_cast<uint8_t>(sprite);
  for (uint8_t row = 0; row < kAircraftSpriteSize; ++row) {
    const uint64_t pixels = kAircraftSpriteMasks[spriteIndex][row];
    for (uint8_t column = 0; column < kAircraftSpriteSize; ++column) {
      if (pixels & (1ULL << (32 - column))) {
        drawNoPlanesSpritePixel(x, y, column, row, color);
      }
    }
  }
}

void drawNoPlanesFrame() {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t dimWhite = matrix.color565(100, 100, 100);
  matrix.fillScreen(black);
  drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(noPlanesAircraftSprite), noPlanesSpriteX, 12, white);
  if (nextNoPlanesSpriteActive) {
    drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(nextNoPlanesAircraftSprite),
                               nextNoPlanesSpriteX, 12, white);
  }
  drawMinecraftCentered("no planes", 54, dimWhite);
}

void drawNoPlanes() {
  showingNoPlanes = true;
  bootTransitionPending = false;
  noPlanesSpriteX = -kNoPlanesSpriteWidth;
  noPlanesAircraftSprite = random(0, kAircraftSpriteCount);
  nextNoPlanesSpriteX = -kNoPlanesSpriteWidth;
  nextNoPlanesAircraftSprite = noPlanesAircraftSprite;
  nextNoPlanesSpriteActive = false;
  nextNoPlanesAnimationMillis = millis() + kNoPlanesAnimationStepMillis;
  drawNoPlanesFrame();
}

float bootChevronProgress(unsigned long elapsed, unsigned long delay) {
  if (elapsed <= delay) return 0.0f;
  return min(1.0f, static_cast<float>(elapsed - delay) / kBootChevronMillis);
}

// A filled 45-degree chevron rises from below the panel. Once its point has
// passed the top edge, every column is filled; drawing later chevrons last
// naturally creates the layered colour-cycle effect without a separate mask.
void drawBootChevron(float progress, uint16_t color) {
  if (progress <= 0.0f) return;
  const int16_t center = (matrix.width() - 1) / 2;
  const int16_t apexY = matrix.height() - static_cast<int16_t>(progress * (matrix.height() * 2));
  for (int16_t x = 0; x < matrix.width(); ++x) {
    const int16_t firstY = apexY + abs(x - center);
    if (firstY < matrix.height()) {
      matrix.fillRect(x, max<int16_t>(0, firstY), 1, matrix.height() - max<int16_t>(0, firstY), color);
    }
  }
}

// The boot animation never needs to repaint the settled area. Drawing only
// the advancing strip lets the panel's DMA scan stay visually calm while the
// leading V edge moves upward.
void drawBootChevronLeadingEdge(float previous, float current, uint16_t color) {
  if (current <= previous) return;
  const int16_t center = (matrix.width() - 1) / 2;
  const int16_t previousApex = matrix.height() - static_cast<int16_t>(previous * (matrix.height() * 2));
  const int16_t currentApex = matrix.height() - static_cast<int16_t>(current * (matrix.height() * 2));
  for (int16_t x = 0; x < matrix.width(); ++x) {
    const int16_t oldFirstY = previousApex + abs(x - center);
    const int16_t newFirstY = currentApex + abs(x - center);
    const int16_t top = max<int16_t>(0, newFirstY);
    const int16_t bottom = min<int16_t>(matrix.height(), oldFirstY);
    if (top < bottom) matrix.fillRect(x, top, 1, bottom - top, color);
  }
}

void drawBootName(uint8_t /*brightness*/) {
  const uint16_t black = matrix.color565(0, 0, 0);
  // Names normally fit on one bold line. Split a long name at a word boundary
  // so no configured display name is silently clipped at the panel edge.
  char first[25] = {};
  char second[25] = {};
  const size_t length = strlen(bootDisplayName);
  if (length <= 18) {
    strlcpy(first, bootDisplayName, sizeof(first));
    drawMinecraftBoldCapsCentered(first, 28, black);
    return;
  }
  size_t split = 18;
  while (split > 0 && bootDisplayName[split] != ' ') --split;
  if (split == 0) split = 18;
  memcpy(first, bootDisplayName, min(split, sizeof(first) - 1));
  first[min(split, sizeof(first) - 1)] = '\0';
  strlcpy(second, bootDisplayName + (bootDisplayName[split] == ' ' ? split + 1 : split), sizeof(second));
  drawMinecraftBoldCapsCentered(first, 23, black);
  drawMinecraftBoldCapsCentered(second, 34, black);
}

void drawBootHoldScreen(uint8_t brightness = 255) {
  const uint8_t purpleRed = static_cast<uint8_t>((178UL * brightness) / 255UL);
  const uint8_t purpleGreen = static_cast<uint8_t>((42UL * brightness) / 255UL);
  const uint8_t purpleBlue = static_cast<uint8_t>((220UL * brightness) / 255UL);
  matrix.fillScreen(matrix.color565(0, 0, 0));
  drawBootChevron(1.0f, visibleColor(purpleRed, purpleGreen, purpleBlue));
  drawBootName(brightness);
}

void playBootAnimation() {
  const unsigned long started = millis();
  constexpr unsigned long totalMillis = kBootChevronMillis + 2 * kBootChevronStaggerMillis;
  float redProgress = 0.0f;
  float blueProgress = 0.0f;
  float purpleProgress = 0.0f;
  matrix.fillScreen(matrix.color565(0, 0, 0));
  while (millis() - started < totalMillis) {
    const unsigned long elapsed = millis() - started;
    const float nextRed = bootChevronProgress(elapsed, 0);
    const float nextBlue = bootChevronProgress(elapsed, kBootChevronStaggerMillis);
    const float nextPurple = bootChevronProgress(elapsed, kBootChevronStaggerMillis * 2);
    drawBootChevronLeadingEdge(redProgress, nextRed, visibleColor(105, 12, 26));
    drawBootChevronLeadingEdge(blueProgress, nextBlue, visibleColor(22, 64, 190));
    drawBootChevronLeadingEdge(purpleProgress, nextPurple, visibleColor(178, 42, 220));
    redProgress = nextRed;
    blueProgress = nextBlue;
    purpleProgress = nextPurple;
    delay(16);
  }
  drawBootHoldScreen();
  bootTransitionPending = true;
}

void advanceNoPlanesAnimation() {
  // Keep the fixed elements on the panel. Only clear the old plane footprint,
  // then paint its next position, so the border and status label never blink.
  const uint16_t black = matrix.color565(0, 0, 0);
  matrix.fillRect(noPlanesSpriteX, 12, 33, 33, black);
  if (nextNoPlanesSpriteActive) matrix.fillRect(nextNoPlanesSpriteX, 12, 33, 33, black);

  ++noPlanesSpriteX;
  if (nextNoPlanesSpriteActive) ++nextNoPlanesSpriteX;
  // Dispatch the next aircraft as soon as this one's nose reaches the right
  // edge. The two silhouettes overlap briefly rather than leaving a dead gap.
  if (!nextNoPlanesSpriteActive && noPlanesSpriteX + 32 >= static_cast<int16_t>(matrix.width() - 1)) {
    const uint8_t spriteCount = kAircraftSpriteCount;
    uint8_t sprite = random(0, spriteCount - 1);
    if (sprite >= noPlanesAircraftSprite) ++sprite;
    nextNoPlanesAircraftSprite = sprite;
    nextNoPlanesSpriteX = -kNoPlanesSpriteWidth;
    nextNoPlanesSpriteActive = true;
  }
  // matrix.width() is unsigned; cast it so a negative off-screen x position
  // remains negative rather than turning into a very large positive number.
  if (noPlanesSpriteX > static_cast<int16_t>(matrix.width())) {
    noPlanesSpriteX = nextNoPlanesSpriteX;
    noPlanesAircraftSprite = nextNoPlanesAircraftSprite;
    nextNoPlanesSpriteActive = false;
  }
  drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(noPlanesAircraftSprite), noPlanesSpriteX, 12,
                              matrix.color565(255, 255, 255));
  if (nextNoPlanesSpriteActive) {
    drawNoPlanesAircraftSprite(static_cast<AircraftSprite>(nextNoPlanesAircraftSprite),
                               nextNoPlanesSpriteX, 12, matrix.color565(255, 255, 255));
  }
}

void showNoPlanesAfterDataFailure() {
  // Never let a transient transport failure erase a visible card. A later
  // successful refresh will decide whether the queue should gracefully leave.
  if (routeCount > 0 || showingNoPlanes) return;
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

// Apply a fresh snapshot to the card already on screen without restarting its
// 20-second scene or its progress/rotation animations. This is especially
// important for a one-aircraft queue: previously that card was intentionally
// kept as an immutable snapshot forever.
void redrawActiveRouteData() {
  if (routeCount == 0 || showingPairingQr) return;
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t primaryWhite = matrix.color565(255, 255, 255);
  const Route& route = routes[routeIndex];
  matrix.fillRect(34, 4, 60, 7, black);
  drawRouteChrome(route);
  drawAirportRoute(route, 34, 4, primaryWhite);
  drawTelemetryValues(route);
  drawRouteMarquees(route);
  drawRotationIndicator();
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
                            bool entering, bool compactHyphen = false, uint8_t startIndex = 0,
                            bool compactZero = false) {
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
    const uint8_t glyphWidth = compactZero && *character == '0' ? 5 : normalGlyphWidth(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = compactZero && *character == '0' ? pgm_read_byte(&kMinecraftTelemetryZero[row])
                                       : uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        const int16_t pixelX = x + column;
        if ((pixels & (1 << (glyphWidth - 1 - column))) && pixelX >= left && pixelX < left + width) {
          matrix.drawPixel(pixelX, pixelY, color);
        }
      }
    }
    x += normalGlyphAdvance(*character, compactZero);
  }
}

int16_t telemetryTextAdvance(const char* text) {
  int16_t advance = 0;
  for (const char* character = text; *character; ++character) {
    advance += (*character == '.' || *character == ':') ? 2 : (*character == '0' ? 5 : 6);
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
    if (*character == ':') {
      for (const int8_t dotY : {2, 5}) {
        const int16_t pixelY = y + dotY + offset;
        if (pixelY >= y && pixelY < y + 7 && x >= left && x < right) matrix.drawPixel(x, pixelY, color);
      }
      x += 2;
      continue;
    }
    const uint8_t glyph = minecraftGlyphIndex(*character);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = *character == '0'
                                 ? pgm_read_byte(&kMinecraftTelemetryZero[row])
                                 : pgm_read_byte(&kMinecraftFont[glyph][row]);
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
  static constexpr uint8_t kWidths[] = {5, 4, 5};
  for (uint8_t glyphNumber = 0; glyphNumber < 3; ++glyphNumber) {
    const int8_t offset = letterDropOffset(phase, line, startIndex + glyphNumber, entering);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      uint8_t pixels = pgm_read_byte(&kCompactKnotsGlyphs[glyphNumber][row]);
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
                                    bool climbing, bool level, int16_t left, int16_t right,
                                    int16_t y, uint8_t line,
                                    unsigned long phase, bool entering, uint16_t color) {
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
    if (glyphInfo.base == '-') {
      const int16_t pixelY = y + 3 + offset;
      if (pixelY >= y && pixelY < y + 7 && x + 4 >= left && x < right) {
        matrix.fillRect(x, pixelY, 5, 1, color);
      }
      x += 6;
      cursor += glyphInfo.bytes;
      ++glyphIndex;
      continue;
    }
    const bool uppercase = glyphInfo.base >= 'A' && glyphInfo.base <= 'Z';
    const uint8_t glyph = uppercase ? static_cast<uint8_t>(glyphInfo.base - 'A') : minecraftGlyphIndex(glyphInfo.base);
    const uint8_t glyphWidth = normalGlyphWidth(glyphInfo.base);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = uppercase ? pgm_read_byte(&kMinecraftNormalCaps[glyph][row])
                                       : pgm_read_byte(&kMinecraftFont[glyph][row]);
      for (uint8_t column = 0; column < glyphWidth; ++column) {
        if ((pixels & (1 << (glyphWidth - 1 - column))) && x + column >= left && x + column < right) {
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
    x += normalGlyphAdvance(glyphInfo.base);
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
  const int16_t arrowX = left + cityTextWidth(origin) + 3;
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
  drawAnimatedCityText(destination, arrowX + 9, y, left, left + width, color, 1,
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
    const bool isQuestion = *character == '?';
    const uint8_t glyph = (*character >= 'A' && *character <= 'Z')
                              ? static_cast<uint8_t>(*character - 'A') : 0;
    const uint8_t glyphWidth = isQuestion ? 5 : pgm_read_byte(&kMinecraftBoldCapsWidths[glyph]);
    for (uint8_t row = 0; row < 7; ++row) {
      const int16_t pixelY = y + row + offset;
      if (pixelY < y || pixelY >= y + 7) continue;
      const uint8_t pixels = isQuestion ? pgm_read_byte(&kMinecraftBoldQuestion[row])
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
  // Match drawMinecraftBoldCaps: callers position the next element from the
  // final occupied column, not from this glyph's trailing spacing column.
  // Without this adjustment, an animated unknown route (--- -> ---) gained a
  // fourth dark pixel before the arrow and remained misaligned after settling.
  return x - 1;
}

void drawAnimatedAirportRoute(const Route& route, unsigned long phase, bool entering) {
  constexpr int16_t left = 34;
  constexpr int16_t right = 94;
  constexpr int16_t y = 4;
  const uint16_t white = matrix.color565(255, 255, 255);
  int16_t x = drawAnimatedBoldLine(route.origin, left, y, left, right, white, 0, phase, entering);
  x += 3;
  const int8_t arrowOffset = letterDropOffset(phase, 0, strlen(route.origin), entering);
  for (uint8_t row = 0; row < 5; ++row) {
    const int16_t pixelY = y + row + 1 + arrowOffset;
    if (pixelY < y || pixelY >= y + 7) continue;
    for (uint8_t column = 0; column < 6; ++column) {
      if (kCityArrowSprite[row] & (1 << (5 - column))) matrix.drawPixel(x + column, pixelY, white);
    }
  }
  drawAnimatedBoldLine(route.destination, x + 9, y, left, right, white, 0, phase, entering,
                       strlen(route.origin) + 1);
}

void clearTransitionTextAreas() {
  const uint16_t black = matrix.color565(0, 0, 0);
  // Keep both permanent layout gutters dark during the otherwise incremental
  // letter-drop animation too.
  matrix.fillRect(33, 2, 1, matrix.height() - 3, black);
  matrix.fillRect(94, 2, 1, matrix.height() - 3, black);
  matrix.fillRect(34, 4, 60, 7, black);
  matrix.fillRect(34, 16, 60, 7, black);
  matrix.fillRect(34, 28, 60, 7, black);
  matrix.fillRect(34, 40, 60, 7, black);
  matrix.fillRect(34, 52, 60, 7, black);
  matrix.fillRect(1, 37, 32, 7, black);
  matrix.fillRect(1, 47, 32, 7, black);
  matrix.fillRect(95, 37, 32, 7, black);
  matrix.fillRect(95, 47, 32, 7, black);
}

void drawAnimatedRouteText(const Route& route, unsigned long phase, bool entering) {
  const uint16_t secondaryWhite = matrix.color565(145, 145, 145);
  const uint16_t tertiaryWhite = matrix.color565(85, 85, 85);
  char registration[sizeof(route.registration)];
  strlcpy(registration, route.registration[0] ? route.registration : "UNKNOWN", sizeof(registration));
  for (char* character = registration; *character; ++character) *character = toupper(*character);
  const char* originCity = airportCity(route.origin);
  const char* destinationCity = airportCity(route.destination);
  char altitude[12], verticalSpeed[12];
  if (route.altFt >= 0) snprintf(altitude, sizeof(altitude), "%d", route.altFt);
  else strlcpy(altitude, "--", sizeof(altitude));
  if (route.verticalSpeedFpm <= -99999) strlcpy(verticalSpeed, "--", sizeof(verticalSpeed));
  else snprintf(verticalSpeed, sizeof(verticalSpeed), "%d", route.verticalSpeedFpm < 0 ? -route.verticalSpeedFpm : route.verticalSpeedFpm);
  drawAnimatedAirportRoute(route, phase, entering);
  drawAnimatedCityLine(originCity, destinationCity, phase, entering, secondaryWhite);
  drawAnimatedNormalLine(route.flightNumber[0] ? route.flightNumber : "-----", 34, 28, 60,
                         tertiaryWhite, 2, phase, entering);
  drawAnimatedNormalLine(airlineName(route), 34, 40, 60, tertiaryWhite, 9, phase, entering);
  if (route.specialFlight && route.specialFlightTitle[0]) {
    drawAnimatedNormalLine(route.specialFlightTitle, 34, 52, 60, visibleColor(255, 128, 0), 10, phase, entering);
  }
  drawAnimatedNormalLine(registration, 95, 37, 32, secondaryWhite, 3, phase, entering, true, 0, true);
  drawAnimatedNormalLine(displayAircraftType(route), 95, 47, 32,
                         secondaryWhite, 4, phase, entering);
  drawAnimatedTelemetryNumber(altitude, 1, 37, 1, 33, tertiaryWhite, 5, phase, entering);
  drawAnimatedTelemetryNumber(verticalSpeed, 1, 47, 1, 33, tertiaryWhite, 6, phase, entering);
  drawAnimatedTelemetryAdornment(altitude, route.altFt >= 0 ? TelemetryUnit::Feet : TelemetryUnit::None,
                                 false, false, false, 1, 33, 37, 5, phase, entering, tertiaryWhite);
  drawAnimatedTelemetryAdornment(verticalSpeed,
                                 TelemetryUnit::None,
                                 route.verticalSpeedFpm > -99999, route.verticalSpeedFpm > 0, route.verticalSpeedFpm == 0,
                                 1, 33, 47, 6, phase, entering, tertiaryWhite);
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
    // The final radar frame is an overlay, not part of the logo itself. Paint
    // the settled incoming badge once as the transition hands off so no dim
    // ray (or a skipped final frame) can remain stuck on the display.
    matrix.fillRect(0, 3, 33, 33, matrix.color565(0, 0, 0));
    brandSweepLayer = BrandSweepLayer::None;
    drawOperatorLogo(routes[routeIndex], 1, 3);
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
      drawRouteProgressForRoute(routes[routeIndex], routes[routeIndex].flightProgressPercent);
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
  drawBrandRadarSweep(routes[routeIndex], routes[letterTransitionRouteIndex], elapsed);
  if (elapsed < kLetterExitMillis) {
    drawAnimatedRouteText(routes[routeIndex], elapsed, false);
  } else if (elapsed >= kLetterEnterStartMillis) {
    drawAnimatedRouteText(routes[letterTransitionRouteIndex], elapsed - kLetterEnterStartMillis, true);
  }
}

constexpr unsigned long kIdleRouteTransitionMillis = 2000;
constexpr unsigned long kIdleRouteCardRevealMillis = 650;

void updateBootToRouteTransition() {
  if (!bootToRouteTransitionActive || routeCount == 0) return;
  const unsigned long elapsed = millis() - idleToRouteTransitionStartMillis;
  // Fade-outs require repainting the whole purple card every frame, which can
  // briefly expose the matrix scan. Sweep a black V over it instead: each
  // frame changes only the advancing edge and leaves every settled LED alone.
  const float phase = min(1.0f, static_cast<float>(elapsed) / kBootExitChevronMillis);
  drawBootChevronLeadingEdge(bootExitChevronProgress, phase, matrix.color565(0, 0, 0));
  bootExitChevronProgress = phase;
  if (phase < 1.0f) return;

  const unsigned long rollInElapsed = elapsed - kBootExitChevronMillis;
  if (!idleToRouteCardDrawn) {
    showingNoPlanes = false;
    drawRouteChrome(routes[routeIndex]);
    idleToRouteCardDrawn = true;
  }
  clearTransitionTextAreas();
  drawAnimatedRouteText(routes[routeIndex], rollInElapsed, true);
  if (rollInElapsed < kBootRouteRollInMillis) return;

  bootToRouteTransitionActive = false;
  routeSceneStartMillis = millis();
  nextRouteAnimationMillis = routeSceneStartMillis + 2000;
  nextRotationIndicatorMillis = routeSceneStartMillis;
  drawRouteProgressForRoute(routes[routeIndex], routes[routeIndex].flightProgressPercent);
  drawRotationIndicator();
}

void updateIdleToRouteTransition() {
  if (!idleToRouteTransitionActive || routeCount == 0) return;
  const unsigned long elapsed = millis() - idleToRouteTransitionStartMillis;
  // Keep the current idle aircraft moving during the network-result handoff.
  if (elapsed < kIdleRouteCardRevealMillis) return;

  if (!idleToRouteCardDrawn) {
    showingNoPlanes = false;
    matrix.fillScreen(matrix.color565(0, 0, 0));
    drawRouteChrome(routes[routeIndex]);
    idleToRouteCardDrawn = true;
  }
  clearTransitionTextAreas();
  drawAnimatedRouteText(routes[routeIndex], elapsed - kIdleRouteCardRevealMillis, true);
  if (elapsed < kIdleRouteTransitionMillis) return;

  idleToRouteTransitionActive = false;
  routeSceneStartMillis = millis();
  nextRouteAnimationMillis = routeSceneStartMillis + 2000;
  nextRotationIndicatorMillis = routeSceneStartMillis;
  drawRouteProgressForRoute(routes[routeIndex], routes[routeIndex].flightProgressPercent);
  drawRotationIndicator();
}

void updateRouteToIdleTransition() {
  if (!routeToIdleTransitionActive || routeCount == 0) return;
  const unsigned long elapsed = millis() - routeToIdleTransitionStartMillis;
  clearTransitionTextAreas();
  if (elapsed < kLetterExitMillis) {
    drawAnimatedRouteText(routes[routeIndex], elapsed, false);
    return;
  }
  if (elapsed < kIdleRouteCardRevealMillis + 400) return;

  routeToIdleTransitionActive = false;
  routeCount = 0;
  routeIndex = 0;
  drawNoPlanes();
}

bool beginBackendRequest(HTTPClient& request, WiFiClientSecure& secureClient,
                         const String& url) {
  if (url.startsWith("https://")) {
    // The ESP32 Arduino build contains a current public root-certificate bundle.
    // This validates the Worker certificate instead of accepting any HTTPS server.
    secureClient.useBuiltinCACertBundle();
    // HTTPClient's regular timeout only covers read/write once a connection
    // exists. Bound the TLS handshake separately so an intermittent network
    // cannot strand the refresh task forever after its first successful poll.
    secureClient.setHandshakeTimeout(8);
    return request.begin(secureClient, url);
  }
  return request.begin(url);
}

String pairingEndpoint() {
  String endpoint(LED_PLANE_BACKEND_URL);
  const int finalSlash = endpoint.lastIndexOf('/');
  return finalSlash < 0 ? String() : endpoint.substring(0, finalSlash) + "/api/device/pairing";
}

String deviceConfigEndpoint() {
  String endpoint(LED_PLANE_BACKEND_URL);
  const int finalSlash = endpoint.lastIndexOf('/');
  return finalSlash < 0 ? String() : endpoint.substring(0, finalSlash) + "/api/device/config";
}

const char* activeDeviceKey() {
  return persistedDeviceKey;
}

// This is deliberately a small, open Wi-Fi QR payload: the ESP32's setup AP
// is only a local captive portal, and joining it is the one action needed to
// open WiFiManager's network picker.
void drawNetworkOfflineScreen() {
  constexpr uint8_t kQrVersion = 3;
  uint8_t qrBuffer[qrcode_getBufferSize(kQrVersion)];
  QRCode qr;
  const String payload = String("WIFI:T:nopass;S:") + setupAccessPoint + ";;";
  qrcode_initText(&qr, qrBuffer, kQrVersion, ECC_LOW, payload.c_str());

  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t dimWhite = matrix.color565(135, 135, 135);
  const int16_t left = (matrix.width() / 2) + ((matrix.width() / 2) - qr.size) / 2;
  const int16_t top = (matrix.height() - qr.size) / 2;
  showingNoPlanes = false;
  matrix.fillScreen(black);
  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      if (qrcode_getModule(&qr, x, y)) matrix.drawPixel(left + x, top + y, white);
    }
  }
  // Keep text entirely inside the left 64px half. The human-facing title uses
  // the full variable-width bold faces, while the instruction uses the normal
  // upper/lowercase body face.
  drawMinecraftBoldTextCentered("Network", 32, 15, white);
  drawMinecraftBoldTextCentered("Offline", 32, 25, white);
  const char* firstInstruction = "Scan to";
  const char* secondInstruction = "connect";
  drawCityText(firstInstruction, 32 - cityTextWidth(firstInstruction) / 2, 40, 0, 64, dimWhite);
  drawCityText(secondInstruction, 32 - cityTextWidth(secondInstruction) / 2, 50, 0, 64, dimWhite);
}

void drawPairingQrLabels() {
  const uint16_t white = matrix.color565(255, 255, 255);
  const uint16_t dimWhite = matrix.color565(135, 135, 135);
  // "Scan to adjust display" is deliberately broken at word boundaries so
  // every character keeps the normal proportional Minecraft face inside the
  // left 64px column.
  drawMinecraftBoldTextCentered("Configure", 32, 13, white);
  const char* firstInstruction = "Scan to";
  const char* secondInstruction = "adjust";
  const char* thirdInstruction = "display";
  drawCityText(firstInstruction, 32 - cityTextWidth(firstInstruction) / 2, 34, 0, 64, dimWhite);
  drawCityText(secondInstruction, 32 - cityTextWidth(secondInstruction) / 2, 42, 0, 64, dimWhite);
  drawCityText(thirdInstruction, 32 - cityTextWidth(thirdInstruction) / 2, 50, 0, 64, dimWhite);
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
  const int16_t left = (matrix.width() / 2) + ((matrix.width() / 2) - qr.size) / 2;
  const int16_t top = (matrix.height() - qr.size) / 2;
  showingNoPlanes = false;
  pairingQrLoadingPainted = false;
  matrix.fillScreen(black);
  drawPairingQrLabels();
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
  constexpr int16_t centerX = 96;
  constexpr int16_t centerY = 32;
  // Clockwise order is important: the old zig-zag ordering put equally bright
  // pixels on opposite sides of the ring, reading as two competing heads.
  constexpr int8_t kRing[][2] = {
      {0, -6}, {3, -5}, {5, -3}, {6, 0}, {5, 3}, {3, 5},
      {0, 6}, {-3, 5}, {-5, 3}, {-6, 0}, {-5, -3}, {-3, -5},
  };
  // Lead, then a smoothly diminishing tail. The final two LEDs deliberately
  // settle at 10% brightness rather than disappearing abruptly.
  constexpr uint8_t kBrightness[] = {255, 210, 170, 135, 105, 80,
                                     60, 45, 35, 28, 26, 26};
  const uint8_t lead = ((millis() - pairingQrSpinnerStartMillis) * 12UL / 2000UL) % 12;
  showingNoPlanes = false;
  if (!pairingQrLoadingPainted) {
    // Paint the static instructions once. Later frames touch only the 13×13
    // spinner area, so the lettering remains perfectly steady while loading.
    matrix.fillScreen(black);
    drawPairingQrLabels();
    pairingQrLoadingPainted = true;
  }
  matrix.fillRect(centerX - 6, centerY - 6, 13, 13, black);
  for (uint8_t tail = 0; tail < 12; ++tail) {
    const uint8_t index = (lead + 12 - tail) % 12;
    const uint8_t level = kBrightness[tail];
    matrix.drawPixel(centerX + kRing[index][0], centerY + kRing[index][1],
                     matrix.color565(level, level, level));
  }
}

void fetchPairingQrTask(void*) {
  int status = -1;
  char url[256] = {};

  {
    WiFiClientSecure secureClient;
    HTTPClient request;
    const String endpoint = pairingEndpoint();
    if (!endpoint.isEmpty() && beginBackendRequest(request, secureClient, endpoint)) {
      request.setTimeout(15000);
      request.addHeader("X-Device-Key", activeDeviceKey());
      status = request.POST("");
      if (status == HTTP_CODE_OK) {
        JsonDocument payload;
        const DeserializationError error = deserializeJson(payload, request.getStream());
        const char* responseUrl = payload["pairUrl"] | "";
        if (!error && responseUrl[0]) {
          strlcpy(url, responseUrl, sizeof(url));
        }
      } else {
        Serial.printf("Pairing request failed: HTTP %d\n", status);
      }
      request.end();
    }
  }

  // Publish the completed response last, after the URL buffer is complete.
  // The draw loop is then free to keep the spinner moving throughout TLS and
  // D1 latency instead of being held inside HTTPClient::POST().
  strlcpy(pairingQrFetchedUrl, url, sizeof(pairingQrFetchedUrl));
  pairingQrFetchStatus = status;
  pairingQrFetchFinished = true;
  pairingQrFetchInProgress = false;
  vTaskDelete(nullptr);
}

void processPairingQrResponse() {
  if (!pairingQrFetchFinished) return;
  pairingQrFetchFinished = false;
  pairingQrRequestStatus = pairingQrFetchStatus;
  if (pairingQrRequestStatus == HTTP_CODE_OK && pairingQrFetchedUrl[0]) {
    pairingUrl = pairingQrFetchedUrl;
    pairingUrlRefreshMillis = millis() + 4UL * 60UL * 1000UL;
    pairingQrRetryPending = false;
    if (showingPairingQr) drawPairingQr(pairingUrl);
    return;
  }
  Serial.printf("Pairing request will retry after status %d\n", pairingQrRequestStatus);
  pairingQrRetryMillis = millis() + 10000;
}

bool fetchAndDrawPairingQr() {
  // Pairing links are valid for five minutes. Reuse one for four minutes so a
  // temporarily unavailable flight feed does not fill the database with tokens.
  if (!pairingUrl.isEmpty() && millis() < pairingUrlRefreshMillis) {
    drawPairingQr(pairingUrl);
    return true;
  }

  // QR mode is a persistent intent, not a one-shot network action. The
  // loading spinner stays visible through Wi-Fi/TLS delays and retries.
  pairingQrRetryPending = true;
  drawPairingQrLoading();
  if (strlen(LED_PLANE_BACKEND_URL) == 0 || strlen(activeDeviceKey()) == 0 ||
      WiFi.status() != WL_CONNECTED || flightFetchInProgress || millis() < pairingQrRetryMillis) {
    return true;
  }

  // Pairing waits for an in-flight flight request above, then owns the one
  // TLS connection in its own task. Keeping HTTPClient off the draw loop
  // lets the spinner remain fluid until the exact frame the QR is available.
  if (!pairingQrFetchInProgress) {
    pairingQrSpinnerStartMillis = millis();
    pairingQrFetchedUrl[0] = '\0';
    pairingQrFetchInProgress = true;
    if (xTaskCreatePinnedToCore(fetchPairingQrTask, "pairing-qr", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
      pairingQrFetchInProgress = false;
      pairingQrRequestStatus = -3;
      pairingQrRetryMillis = millis() + 10000;
    }
  }
  return true;
}

void updatePairingQrRequest() {
  processPairingQrResponse();
  if (showingPairingQr && pairingQrRetryPending) fetchAndDrawPairingQr();
}

void fetchDeviceConfigTask(void*) {
  int status = -1;
  char name[sizeof(deviceConfigFetchedName)] = {};

  {
    WiFiClientSecure secureClient;
    HTTPClient request;
    const String endpoint = deviceConfigEndpoint();
    if (!endpoint.isEmpty() && beginBackendRequest(request, secureClient, endpoint)) {
      // This should be a sub-second Worker/D1 lookup. Keep it bounded so a
      // transient endpoint failure never delays the route display for long.
      request.setConnectTimeout(4000);
      request.setTimeout(6000);
      request.addHeader("X-Device-Key", activeDeviceKey());
      status = request.GET();
      if (status == HTTP_CODE_OK) {
        JsonDocument payload;
        const DeserializationError error = deserializeJson(payload, request.getStream());
        const char* responseName = payload["name"] | "";
        if (!error && responseName[0]) strlcpy(name, responseName, sizeof(name));
      } else {
        Serial.printf("Configuration request failed: HTTP %d\n", status);
      }
      request.end();
    }
  }

  // Publish last, so the draw loop never sees a partially written name.
  strlcpy(deviceConfigFetchedName, name, sizeof(deviceConfigFetchedName));
  deviceConfigFetchStatus = status;
  deviceConfigFetchFinished = true;
  deviceConfigFetchInProgress = false;
  vTaskDelete(nullptr);
}

void requestDeviceConfigRefresh() {
  deviceConfigRefreshPending = true;
  // Location changes need a fresh aircraft list, not the regular minute wait.
  deviceConfigRefreshFlightsPending = true;
}

void maybeStartDeviceConfigRefresh() {
  if (!deviceConfigRefreshPending || deviceConfigFetchInProgress ||
      strlen(LED_PLANE_BACKEND_URL) == 0 || strlen(activeDeviceKey()) == 0 ||
      WiFi.status() != WL_CONNECTED || flightFetchInProgress || pairingQrFetchInProgress) {
    return;
  }
  deviceConfigFetchInProgress = true;
  if (xTaskCreatePinnedToCore(fetchDeviceConfigTask, "device-config", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    deviceConfigFetchInProgress = false;
    deviceConfigFetchStatus = -3;
    deviceConfigFetchFinished = true;
  }
}

void processDeviceConfigResponse() {
  if (!deviceConfigFetchFinished) return;
  deviceConfigFetchFinished = false;
  deviceConfigRefreshPending = false;
  if (deviceConfigFetchStatus == HTTP_CODE_OK && deviceConfigFetchedName[0]) {
    cacheBootDisplayName(deviceConfigFetchedName);
    Serial.println("Display configuration refreshed.");
  } else {
    Serial.printf("Display configuration refresh completed with status %d\n", deviceConfigFetchStatus);
  }
  if (deviceConfigRefreshFlightsPending) {
    deviceConfigRefreshFlightsPending = false;
    // Start a flight fetch as soon as this request has released TLS.
    nextFlightPollMillis = millis();
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
    const bool enteringFromBoot = bootTransitionPending;
    const bool enteringFromIdle = showingNoPlanes;
    bootTransitionPending = false;
    memcpy(routes, refreshedRoutes, sizeof(Route) * refreshedCount);
    routeCount = refreshedCount;
    routeIndex = 0;
    routeProgressAnimationActive = false;
    rotationIndicatorResetActive = false;
    rotationPipTransitionActive = false;
    routeProgressTo = routes[routeIndex].flightProgressPercent;
    nextFlightRotationMillis = millis() + kFlightRotationMillis;
    if (enteringFromBoot) {
      // The boot title clears with a black leading-edge chevron, then uses
      // the familiar staggered drop-in for the first live aircraft.
      bootToRouteTransitionActive = true;
      bootExitChevronProgress = 0.0f;
      idleToRouteCardDrawn = false;
      idleToRouteTransitionStartMillis = millis();
    } else if (enteringFromIdle) {
      // Let the idle plane continue while the request result is applied, then
      // use the same staggered drop-in language as a route-to-route change.
      idleToRouteTransitionActive = true;
      idleToRouteCardDrawn = false;
      idleToRouteTransitionStartMillis = millis();
    } else {
      drawCurrentRoute();
    }
    return;
  }

  // A configured special flight owns the display while it is live. Replace an
  // ordinary rotation immediately rather than letting its existing queue
  // position defer the special card by several 20-second scenes. Once this
  // handoff is complete the queue contains only special flights, so the normal
  // stable merge below preserves their order as usual.
  const bool hasSpecialUpdate = refreshedCount > 0 && refreshedRoutes[0].specialFlight;
  bool queuedOrdinaryFlight = false;
  for (uint8_t index = 0; index < routeCount; ++index) {
    if (!routes[index].specialFlight) {
      queuedOrdinaryFlight = true;
      break;
    }
  }
  if (hasSpecialUpdate && queuedOrdinaryFlight) {
    memcpy(routes, refreshedRoutes, sizeof(Route) * refreshedCount);
    routeCount = refreshedCount;
    routeIndex = 0;
    letterTransitionActive = false;
    routeToIdleTransitionActive = false;
    idleToRouteTransitionActive = false;
    routeProgressAnimationActive = false;
    rotationIndicatorResetActive = false;
    rotationPipTransitionActive = false;
    routeProgressTo = routes[0].flightProgressPercent;
    nextFlightRotationMillis = millis() + kFlightRotationMillis;
    drawCurrentRoute();
    return;
  }

  bool consumed[kMaxRoutableFlights] = {};
  uint8_t mergedCount = 0;
  uint8_t activeMergedIndex = 0;
  uint8_t departedCount = 0;
  bool activeRouteRefreshed = false;
  for (uint8_t existingIndex = 0; existingIndex < routeCount; ++existingIndex) {
    int8_t refreshedIndex = -1;
    for (uint8_t candidate = 0; candidate < refreshedCount; ++candidate) {
      if (!consumed[candidate] && routesHaveSameIdentity(routes[existingIndex], refreshedRoutes[candidate])) {
        refreshedIndex = candidate;
        break;
      }
    }

    if (existingIndex == routeIndex) {
      // Keep the current card in its queue position and scene, but do not
      // freeze its live data. With only one aircraft in range, preserving the
      // old snapshot here meant altitude, speed, track, and every other field
      // could remain unchanged indefinitely.
      if (refreshedIndex >= 0) {
        mergedRoutes[mergedCount] = refreshedRoutes[refreshedIndex];
        mergedRoutes[mergedCount].pendingRemoval = false;
        activeRouteRefreshed = true;
      } else {
        // A departed active card stays only until this scene transitions.
        mergedRoutes[mergedCount] = routes[existingIndex];
        mergedRoutes[mergedCount].pendingRemoval = true;
      }
      activeMergedIndex = mergedCount++;
      if (refreshedIndex >= 0) consumed[refreshedIndex] = true;
    } else if (refreshedIndex >= 0) {
      mergedRoutes[mergedCount] = refreshedRoutes[refreshedIndex];
      mergedRoutes[mergedCount].pendingRemoval = false;
      consumed[refreshedIndex] = true;
      ++mergedCount;
    } else {
      // This aircraft is no longer in the latest in-range response. Only the
      // visible card is allowed to survive briefly; every other departed card
      // leaves the rotation immediately.
      ++departedCount;
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

  // Clear the unused tail as well as replacing the live queue. This matters
  // on a long-running panel: an interrupted handoff must never be able to
  // render an old Route object after the queue has compacted.
  memset(routes, 0, sizeof(routes));
  memcpy(routes, mergedRoutes, sizeof(Route) * mergedCount);
  routeCount = mergedCount;
  routeIndex = activeMergedIndex;
  Serial.printf("Queue refreshed: %u aircraft, removed %u, active index %u\n",
                routeCount, departedCount, routeIndex);
  if (activeRouteRefreshed) redrawActiveRouteData();
}

void recordSuccessfulFlightRefresh() {
  time_t now = 0;
  time(&now);
  struct tm localTime = {};
  if (now >= 1700000000 && localtime_r(&now, &localTime)) {
    snprintf(lastFlightRefreshTime, sizeof(lastFlightRefreshTime), "%02d:%02d:%02d",
             localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
  } else {
    strlcpy(lastFlightRefreshTime, "--:--:--", sizeof(lastFlightRefreshTime));
  }
}

void processFetchedFlightResponse() {
  if (!flightFetchFinished) return;
  // The exit and entrance share queue indices for two seconds. Applying a
  // snapshot in that interval could compact the target out from beneath the
  // animation, leaving an old aircraft in rotation (or reading a stale tail
  // slot). Hold the completed response until the handoff has settled, then
  // reconcile it in one atomic queue update.
  if (letterTransitionActive || idleToRouteTransitionActive || routeToIdleTransitionActive) {
    return;
  }
  flightFetchFinished = false;
  if (flightFetchStatus != HTTP_CODE_OK) {
    Serial.printf("Flight request failed: HTTP %d\n", flightFetchStatus);
    ++consecutiveFlightFetchFailures;
    // A transport failure must not make the display wait out the whole normal
    // minute. Reconnect the station after repeated connection-level errors,
    // then retry soon; ordinary successful polls remain one minute apart.
    if (flightFetchStatus < 0 && consecutiveFlightFetchFailures >= 2) {
      Serial.println("Refreshing Wi-Fi after repeated flight transport failures.");
      WiFi.disconnect(false, false);
      WiFi.reconnect();
    }
    nextFlightPollMillis = millis() + 5000;
    // A failed refresh must never erase a perfectly good on-screen card.
    if (routeCount == 0 && !showingNoPlanes) showNoPlanesAfterDataFailure();
    return;
  }
  JsonDocument payload;
  const DeserializationError error = deserializeJson(payload, flightFetchResponse, flightFetchLength);
  if (error) {
    Serial.printf("Flight JSON failed: %s\n", error.c_str());
    if (routeCount == 0 && !showingNoPlanes) showNoPlanesAfterDataFailure();
    return;
  }

  cacheBootDisplayName(payload["displayName"] | "");

  // HTTP success plus valid JSON is the moment a refresh becomes observable
  // to the display. Record it before reconciling the queue so the orange
  // debug line is updated along with the same snapshot.
  recordSuccessfulFlightRefresh();
  consecutiveFlightFetchFailures = 0;
  ++appliedFlightRefreshes;
  memset(refreshedRoutes, 0, sizeof(refreshedRoutes));

  uint8_t newRouteCount = 0;
  bool hasSpecialFlight = false;
  for (JsonObject flight : payload["flights"].as<JsonArray>()) {
    if (flight["specialFlight"] | false) {
      hasSpecialFlight = true;
      break;
    }
  }
  for (JsonObject flight : payload["flights"].as<JsonArray>()) {
    // Special flights exclusively occupy the display rotation. Normal flights
    // remain in the backend response for diagnostics, but cannot dilute the
    // on-panel counter or delay a configured aircraft.
    if (hasSpecialFlight && !(flight["specialFlight"] | false)) continue;
    const char* candidateOrigin = flight["origin"] | "";
    const char* candidateDestination = flight["destination"] | "";
    const char* candidateFlightNumber = flight["flightNumber"] | "";
    const char* candidateOperator = flight["airline"] | "";
    const char* candidateRegistration = flight["registration"] | "";
    const char* candidateIcao24 = flight["icao24"] | "";
    const bool hasKnownRoute = candidateOrigin[0] && candidateDestination[0] &&
        strcmp(candidateOrigin, "???") != 0 && strcmp(candidateDestination, "???") != 0;
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
      strlcpy(route.origin, candidateOrigin[0] ? candidateOrigin : "???", sizeof(route.origin));
      strlcpy(route.destination, candidateDestination[0] ? candidateDestination : "???", sizeof(route.destination));
      strlcpy(route.flightNumber, candidateFlightNumber, sizeof(route.flightNumber));
      strlcpy(route.operatorCode, candidateOperator, sizeof(route.operatorCode));
      strlcpy(route.registration, candidateRegistration, sizeof(route.registration));
      route.specialFlight = flight["specialFlight"] | false;
      strlcpy(route.specialFlightTitle, flight["trackedFlightTitle"] | "", sizeof(route.specialFlightTitle));
      route.altFt = flight["altFt"] | -1;
      route.speedKt = flight["speedKt"] | -1;
      route.verticalSpeedFpm = flight["verticalSpeedFpm"] | -100000;
      // Accept the former field for the brief lifetime of a cached response,
      // while all fresh backend responses use trackDeg exclusively.
      route.trackDeg = flight["trackDeg"] | (flight["headingDeg"] | -1);
      strlcpy(route.aircraftType, flight["aircraftType"] | "", sizeof(route.aircraftType));
      // Tail-number overrides deliberately affect branding only. The live
      // aircraft type remains authoritative for the type-specific silhouette.
      route.distanceKm = flight["distanceKm"] | -1.0f;
      JsonObject flightStatus = flight["flightStatus"].as<JsonObject>();
      strlcpy(route.etaDisplay, flightStatus["etaDisplay"] | "", sizeof(route.etaDisplay));
      route.estimatedArrivalEpoch = flightStatus["estimatedArrivalEpoch"] | 0U;
      route.arrivalDelayPercent = flightStatus["arrivalDelayPercent"] | -1.0f;
      route.flightProgressPercent = flight["flightProgressPercent"] | -1.0f;
      route.pendingRemoval = false;
      ++newRouteCount;
    }
  }
  if (newRouteCount == 0 && routeCount == 0) {
    routeCount = 0;
    // Preserve the moving idle scene across empty refreshes.
    if (!showingNoPlanes) drawNoPlanes();
    Serial.println("No routable flights in the current response.");
  } else {
    mergeRefreshedRouteQueue(newRouteCount);
    Serial.printf("Queued %u routable flights\n", routeCount);
  }
  // A valid response can legitimately contain no in-range aircraft. Keep the
  // diagnostic honest in that case too, even while the current card finishes
  // its scene before gracefully leaving the queue.
}

void fetchFlightsTask(void*) {
  int status = -1;

  {
    WiFiClientSecure secureClient;
    HTTPClient request;
    if (beginBackendRequest(request, secureClient, LED_PLANE_BACKEND_URL)) {
      // A pairing request and a flight request must not overlap: two TLS clients
      // can stall the ESP32 Wi-Fi stack. Keep a flight refresh bounded so QR mode
      // waits briefly at most, then receives the network to itself.
      request.setConnectTimeout(8000);
      request.setTimeout(12000);
      request.addHeader("X-Device-Key", activeDeviceKey());
      status = request.GET();
      if (status == HTTP_CODE_OK) {
        const String response = request.getString();
        if (response.length() >= kFlightResponseCapacity) {
          status = -2;
        } else {
          flightFetchLength = response.length();
          memcpy(flightFetchResponse, response.c_str(), flightFetchLength);
          flightFetchResponse[flightFetchLength] = '\0';
        }
      }
      request.end();
    }
  }
  flightFetchStatus = status;
  // This flag is written last: the display task will only read a complete
  // response buffer, while this background task has kept animation running.
  flightFetchFinished = true;
  flightFetchInProgress = false;
  flightFetchStartedMillis = 0;
  flightFetchCompletions = flightFetchCompletions + 1;
  vTaskDelete(nullptr);
}

bool fetchAndDrawFirstFlight() {
  if (strlen(LED_PLANE_BACKEND_URL) == 0 || strlen(activeDeviceKey()) == 0 ||
      WiFi.status() != WL_CONNECTED || flightFetchInProgress || flightFetchFinished) {
    return false;
  }
  flightFetchInProgress = true;
  flightFetchStartedMillis = millis();
  flightFetchAttempts = flightFetchAttempts + 1;
  if (xTaskCreatePinnedToCore(fetchFlightsTask, "flight-fetch", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    flightFetchStatus = -3;
    flightFetchFinished = true;
    flightFetchInProgress = false;
    flightFetchStartedMillis = 0;
    return false;
  }
  Serial.println("Flight refresh started.");
  return true;
}

void handleBootButton() {
  // On the Waveshare board the BOOT button is GPIO 0 and reads LOW when held.
  const bool pressed = digitalRead(kBootButtonPin) == LOW;
  if (pressed && !bootButtonWasPressed && millis() - lastBootButtonPressMillis > 300) {
    lastBootButtonPressMillis = millis();
    showingPairingQr = !showingPairingQr;
    if (showingPairingQr) {
      Serial.println("Display mode: pairing QR");
      fetchAndDrawPairingQr();
    } else {
      Serial.println("Display mode: flight route");
      pairingQrRetryPending = false;
      if (routeCount > 0) {
        drawCurrentRoute();
      } else {
        drawNoPlanes();
      }
      nextFlightRotationMillis = millis() + kFlightRotationMillis;
      // Settings are edited through the QR page. Pull them before refreshing
      // routes so a changed location is visible immediately.
      requestDeviceConfigRefresh();
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
  preferences.begin("led-plane", false);
  const String savedDeviceKey = preferences.getString("device-key", "");
  if (savedDeviceKey.length()) {
    strlcpy(persistedDeviceKey, savedDeviceKey.c_str(), sizeof(persistedDeviceKey));
  } else if (strlen(LED_PLANE_DEVICE_KEY)) {
    strlcpy(persistedDeviceKey, LED_PLANE_DEVICE_KEY, sizeof(persistedDeviceKey));
    preferences.putString("device-key", persistedDeviceKey);
  }
  hideUnknownRoutes = preferences.getBool("hide-unknown-routes", false);
  const String savedName = preferences.getString("display-name", "PLANE DISPLAY");
  strlcpy(bootDisplayName, savedName.c_str(), sizeof(bootDisplayName));
  uint32_t setupUuid = preferences.getULong("setup-uuid", 0);
  if (setupUuid == 0) {
    setupUuid = esp_random();
    preferences.putULong("setup-uuid", setupUuid);
  }
  // A random persisted UUID fragment avoids exposing a common factory AP name
  // while still leaving the network easy to recognise in a phone's Wi-Fi list.
  snprintf(setupAccessPoint, sizeof(setupAccessPoint), "Plane Display %04lX",
           static_cast<unsigned long>((setupUuid >> 16) & 0xFFFF));
  preferences.end();
}

void saveDisplayPreferences() {
  Preferences preferences;
  preferences.begin("led-plane", false);
  preferences.putBool("hide-unknown-routes", hideUnknownRoutes);
  preferences.end();
}

void cacheBootDisplayName(const char* name) {
  if (!name || !name[0]) return;
  char normalized[sizeof(bootDisplayName)] = {};
  size_t output = 0;
  for (const char* character = name; *character && output + 1 < sizeof(normalized); ++character) {
    const char value = toupper(static_cast<unsigned char>(*character));
    normalized[output++] = (value >= 'A' && value <= 'Z') || value == ' ' || value == '-' || value == '\'' ? value : ' ';
  }
  while (output > 0 && normalized[output - 1] == ' ') --output;
  normalized[output] = '\0';
  if (!normalized[0] || strcmp(normalized, bootDisplayName) == 0) return;
  strlcpy(bootDisplayName, normalized, sizeof(bootDisplayName));
  Preferences preferences;
  preferences.begin("led-plane", false);
  preferences.putString("display-name", bootDisplayName);
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

  // Kept behind the same local updater password. This is intentionally tiny:
  // enough to distinguish a stale source response, an in-flight HTTPS call,
  // and a genuinely wedged request without ever exposing device credentials.
  updateServer.on("/diagnostics", HTTP_GET, []() {
    if (!updateRequestIsAuthorized()) {
      return updateServer.requestAuthentication();
    }
    char countdown[3];
    refreshCountdownSeconds(countdown);
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"fetchInProgress\":%s,\"fetchFinished\":%s,\"lastHttpStatus\":%d,"
             "\"attempts\":%lu,\"completions\":%lu,\"appliedRefreshes\":%lu,"
             "\"consecutiveFailures\":%u,\"wifiStatus\":%d,\"freeHeap\":%lu,"
             "\"lastRefreshUtc\":\"%s\",\"nextPollSeconds\":\"%s\"}",
             flightFetchInProgress ? "true" : "false", flightFetchFinished ? "true" : "false",
             flightFetchStatus, static_cast<unsigned long>(flightFetchAttempts),
             static_cast<unsigned long>(flightFetchCompletions),
             static_cast<unsigned long>(appliedFlightRefreshes), consecutiveFlightFetchFailures,
             static_cast<int>(WiFi.status()), static_cast<unsigned long>(ESP.getFreeHeap()),
             lastFlightRefreshTime, countdown);
    updateServer.send(200, "application/json", payload);
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

void networkSetupPortalStarted(WiFiManager*) {
  networkSetupPortalActive = true;
  drawNetworkOfflineScreen();
  Serial.printf("Wi-Fi setup portal: %s\n", setupAccessPoint);
}

bool joinNetworkViaSetupPortal() {
  WiFiManager wifiManager;
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConnectTimeout(30);
  wifiManager.setTitle("Plane Display");
  wifiManager.setDarkMode(true);
  wifiManager.setShowInfoErase(false);
  wifiManager.setShowInfoUpdate(false);
  wifiManager.setCustomHeadElement(kPortalHead);
  wifiManager.setCustomMenuHTML(kPortalMenu);
  // The root page intentionally has exactly two actions. The connection and
  // information routes remain available, but router-style restart/erase/update
  // controls are not surfaced in this consumer setup flow.
  wifiManager.setMenu(kPortalMenuItems, 1);
  wifiManager.setAPCallback(networkSetupPortalStarted);
  networkSetupPortalActive = true;
  // The callback paints before the portal is ready for a phone to join, so
  // there is never a blank or stale flight card during configuration.
  const bool connected = wifiManager.autoConnect(setupAccessPoint);
  networkSetupPortalActive = false;
  if (connected) saveWifiCredentials();
  return connected;
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
    // Saved network credentials are stored in the ESP32's non-volatile memory.
    // If reconnecting fails (including the first boot), the QR-led captive
    // portal remains available until the owner supplies a working network.
    if (!joinNetworkViaSetupPortal()) {
      Serial.println("Wi-Fi setup portal closed without a connection; reopening.");
      delay(1000);
      ESP.restart();
    }
  }

  synchronizeClock();

  // The setup helper, when needed, is out of scope before the updater uses port 80.
  enableLocalUpdater();

  Serial.printf("Wi-Fi connected. Local updater: http://%s.local (%s)\n",
                kDeviceHostname, WiFi.localIP().toString().c_str());
}

void monitorWifiConnectivity() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiOfflineSinceMillis = 0;
    return;
  }
  if (wifiOfflineSinceMillis == 0) {
    wifiOfflineSinceMillis = millis();
    return;
  }
  if (networkSetupPortalActive || millis() - wifiOfflineSinceMillis < kWifiOfflinePortalMillis) return;

  Serial.println("Wi-Fi has been unavailable for one minute; opening setup portal.");
  if (joinNetworkViaSetupPortal()) {
    synchronizeClock();
    nextFlightPollMillis = millis();
    wifiOfflineSinceMillis = 0;
  }
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
  pinMode(kBootButtonPin, INPUT_PULLUP);
  loadDisplayPreferences();
  playBootAnimation();
  connectWifiAndEnableLocalUpdater();
  // A powered-off board may have been moved or renamed. Synchronize its tiny
  // settings payload before asking the comparatively slow flight providers.
  requestDeviceConfigRefresh();
  nextFlightPollMillis = millis();
}

void loop() {
  // HUB75 DMA refresh runs independently; handle local web update traffic here.
  updateServer.handleClient();
  monitorWifiConnectivity();
  if (networkSetupPortalActive) return;
  handleBootButton();
  updatePairingQrRequest();
  processDeviceConfigResponse();
  maybeStartDeviceConfigRefresh();
  if (flightFetchInProgress && flightFetchStartedMillis != 0 &&
      millis() - flightFetchStartedMillis > kFlightFetchWatchdogMillis) {
    // Restarting is safer than force-deleting a task while mbedTLS owns its
    // socket. It also guarantees one bad connection cannot freeze all later
    // telemetry updates for the lifetime of the display.
    Serial.println("Flight fetch watchdog fired; restarting network stack.");
    shouldRestart = true;
  }
  processFetchedFlightResponse();
  const bool transitionWasActive = letterTransitionActive;
  // If the only card disappeared during its scene, honour the full scene
  // duration before falling back to the idle animation.
  if (!showingPairingQr && routeCount == 1 && routes[0].pendingRemoval &&
      !letterTransitionActive && !routeToIdleTransitionActive && !idleToRouteTransitionActive &&
      !bootToRouteTransitionActive &&
      millis() >= nextFlightRotationMillis) {
    routeToIdleTransitionActive = true;
    routeToIdleTransitionStartMillis = millis();
  }
  if (!showingPairingQr && routeCount > 1 && !letterTransitionActive &&
      !routeToIdleTransitionActive && !idleToRouteTransitionActive && !bootToRouteTransitionActive &&
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
  if (bootToRouteTransitionActive) updateBootToRouteTransition();
  if (idleToRouteTransitionActive) updateIdleToRouteTransition();
  if (routeToIdleTransitionActive) updateRouteToIdleTransition();
  if (!deviceConfigRefreshPending && !deviceConfigFetchInProgress && millis() >= nextFlightPollMillis) {
    // Do not let a blocking network refresh erase the exit/enter frames. When
    // a refresh lands on a scene boundary, hold it until the new card has had
    // its initial marquee pause.
    if (!showingPairingQr && (letterTransitionActive || transitionWasActive)) {
      if (letterTransitionActive) flightRefreshDeferred = true;
      else nextFlightPollMillis = millis() + 2000;
    } else {
      // Never turn a temporarily unavailable fetch slot into a lost minute.
      // A completed response is normally held for the end of an animation;
      // retrying in one second means it is processed and the next request can
      // begin promptly instead of the panel appearing frozen.
      const bool started = showingPairingQr ? fetchAndDrawPairingQr() : fetchAndDrawFirstFlight();
      nextFlightPollMillis = millis() + (started ? 60000 : 1000);
    }
  }
  if (!showingPairingQr && routeCount > 0 && !letterTransitionActive &&
      !idleToRouteTransitionActive && !routeToIdleTransitionActive && !bootToRouteTransitionActive &&
      millis() >= nextRouteAnimationMillis) {
    refreshActiveMarquees(routes[routeIndex]);
    nextRouteAnimationMillis = millis() + 75;
  }
  if (!showingPairingQr && routeCount > 0 && !idleToRouteTransitionActive &&
      !routeToIdleTransitionActive && !bootToRouteTransitionActive && millis() >= nextRotationIndicatorMillis) {
    if (routeProgressAnimationActive) {
      const RouteProgressFrame progress = animatedRouteProgress();
      drawRouteProgressForRoute(routes[routeIndex], progress.percent, progress.opacity);
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
