#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <WiFiManager.h>

#if __has_include("local_secrets.h")
#include "local_secrets.h"
#endif

#ifndef LED_PLANE_OTA_PASSWORD
#define LED_PLANE_OTA_PASSWORD ""
#endif

namespace {

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 64;
constexpr uint8_t kPanelChain = 2;
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

void drawCentered(const char* text, int16_t y, uint16_t color) {
  constexpr uint8_t kTextSize = 3;
  const int16_t width = strlen(text) * 6 * kTextSize;
  matrix.setTextSize(kTextSize);
  matrix.setTextColor(color);
  matrix.setCursor((matrix.width() - width) / 2, y);
  matrix.print(text);
}

void drawGreeting() {
  const uint16_t black = matrix.color565(0, 0, 0);
  const uint16_t deepPurple = matrix.color565(78, 42, 132);
  const uint16_t purple = matrix.color565(132, 88, 190);
  const uint16_t lilac = matrix.color565(202, 172, 244);

  matrix.fillScreen(black);

  // A bright frame and divider make the 128x64 canvas obvious at a glance.
  matrix.drawRect(0, 0, matrix.width(), matrix.height(), deepPurple);
  matrix.drawRect(2, 2, matrix.width() - 4, matrix.height() - 4, purple);
  matrix.drawFastHLine(8, 32, matrix.width() - 16, deepPurple);

  // Shadow then foreground gives the small LED letters more definition.
  drawCentered("HELLO", 7, deepPurple);
  drawCentered("HELLO", 5, lilac);
  drawCentered("KENNY", 39, deepPurple);
  drawCentered("KENNY", 37, lilac);
}

bool updateRequestIsAuthorized() {
  return strlen(LED_PLANE_OTA_PASSWORD) == 0 ||
         updateServer.authenticate("admin", LED_PLANE_OTA_PASSWORD);
}

void enableLocalUpdater() {
  updateServer.on("/", HTTP_GET, []() {
    if (!updateRequestIsAuthorized()) {
      return updateServer.requestAuthentication();
    }
    updateServer.send(
        200, "text/html",
        "<!doctype html><html><body style='font-family:sans-serif;max-width:42rem;"
        "margin:3rem auto'><h1>LED Plane firmware update</h1>"
        "<form method='post' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='update' accept='.bin' required> "
        "<button>Install update</button></form></body></html>");
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

  // The setup helper, when needed, is out of scope before the updater uses port 80.
  enableLocalUpdater();

  Serial.printf("Wi-Fi connected. Local updater: http://%s.local (%s)\n",
                kDeviceHostname, WiFi.localIP().toString().c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  matrix.begin();
  // Keep the first panel test deliberately dim; raise only after the external
  // 5 V panel supply is verified.
  matrix.setBrightness8(24);
  drawGreeting();
  connectWifiAndEnableLocalUpdater();
  Serial.println("Hello Kenny rendered on 128x64 HUB75 display.");
}

void loop() {
  // HUB75 DMA refresh runs independently; handle local web update traffic here.
  updateServer.handleClient();
  if (shouldRestart) {
    delay(500);
    ESP.restart();
  }
}
