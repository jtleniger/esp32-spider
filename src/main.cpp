#include <Adafruit_TLC5947.h>
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <led_engine.h>
#include <led_layout.h>
#include <modes.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h: copy include/secrets.h.example and fill in WIFI_SSID/WIFI_PASSWORD"
#endif

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kWifiRetryIntervalMs = 5000;

Adafruit_TLC5947 tlc(ledfx::kTlcDriverCount, ledfx::kTlcClockPin, ledfx::kTlcDataPin,
                     ledfx::kTlcLatchPin);
ledfx::LedEngine engine;
WebServer server(kHttpPort);

// One write per changed frame; the driver bit-bangs 24 x 12 bits per call.
void pushFrame() {
  const uint16_t *frame = engine.frame();
  for (uint8_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
    tlc.setPWM(channel, frame[channel]);
  }
  tlc.write();
}

// HLK-LD1020: driven high while motion is detected.
bool radarActive() { return digitalRead(ledfx::kRadarPin) == HIGH; }

void handleHealth() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain", "");
    return;
  }
  server.send(200, "text/plain", "");
}

// The ESP32 WebServer parses the query string independently of method and
// Content-Type, so the argument is available even for a body-less POST.
void handleMode() {
  uint16_t mode = 0;
  if (!ledfx::parseModeArg(server.arg("m").c_str(), mode) ||
      !engine.setMode(mode, millis())) {
    server.send(400, "text/plain", "invalid mode\n");
    return;
  }
  engine.tick(millis(), radarActive());  // blank the frame at the new start time
  pushFrame();
  server.send(200, "text/plain", "ok\n");
}

void beginWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (WiFi.waitForConnectResult(kWifiConnectTimeoutMs) == WL_CONNECTED) {
    Serial.print("WiFi up: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected, still retrying");
  }
}

// Required: WiFi.setAutoReconnect() is not wired up in arduino-esp32 2.0.17
// (the flag has no reader in WiFiSTA.cpp/WiFiGeneric.cpp), so nothing else
// re-establishes the link after an AP restart or a dropout. Do not delete.
void maintainWifi(uint32_t nowMs) {
  static uint32_t lastAttemptMs = 0;
  if (WiFi.status() == WL_CONNECTED || nowMs - lastAttemptMs < kWifiRetryIntervalMs) {
    return;
  }
  lastAttemptMs = nowMs;
  WiFi.reconnect();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // HLK-LD1020 radar output: driven high on detection. Read on every loop tick
  // and used only by mode 0. Pulled down so a disconnected module never reads
  // as detection.
  pinMode(ledfx::kRadarPin, INPUT_PULLDOWN);

  if (!tlc.begin()) {
    Serial.println("TLC5947 init failed");
  }
  pushFrame();  // all channels off as soon as the driver is up

  beginWifi();

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/mode", HTTP_POST, handleMode);
  server.begin();
  Serial.printf("HTTP server listening on port %d\n", kHttpPort);

  engine.setMode(ledfx::kModePulse, millis());  // breathe after boot, fast on motion
}

void loop() {
  server.handleClient();
  const uint32_t now = millis();
  maintainWifi(now);
  if (engine.tick(now, radarActive())) {
    pushFrame();
  }
}
