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
// Content-Type, so the arguments are available even for a body-less POST.
void handleMode() {
  uint16_t mode = 0;
  if (!ledfx::parseModeArg(server.arg("m").c_str(), mode)) {
    server.send(400, "text/plain", "invalid mode\n");
    return;
  }

  uint16_t channel = 0;
  if (mode == ledfx::kModeSingleChannel &&
      !ledfx::parseChannelArg(server.arg("c").c_str(), channel)) {
    server.send(400, "text/plain", "invalid channel\n");
    return;
  }

  if (!engine.setMode(mode, channel, millis())) {
    server.send(400, "text/plain", "invalid mode\n");
    return;
  }
  engine.tick(millis(), radarActive());  // blank/park the frame at the new start
  pushFrame();
  server.send(200, "text/plain", "ok\n");
}

// Fire-and-forget: bring the STA interface up and queue the first connect
// attempt, but never wait for the AP. In arduino-esp32 2.0.17
// WiFi.waitForConnectResult() is a blocking delay() loop, so waiting here
// blacked out the rig (engine, radar, HTTP) for the whole timeout on every
// power cycle where the AP was absent or slow. Connection state is polled and
// retried from maintainWifi() instead.
//
// Must still run before WebServer::begin(): WiFi.mode() is what triggers
// esp_netif_init()/tcpip_init() (WiFiGeneric.cpp wifiLowLevelInit), and
// WiFiServer::begin() creates a lwip socket immediately, which panics with
// "tcpip_send_msg_wait_sem (Invalid mbox)" when the tcpip thread does not
// exist yet. Only the connect wait is safe to drop, not the ordering.
void beginWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("WiFi connecting in background");
}

// Required: WiFi.setAutoReconnect() is not wired up in arduino-esp32 2.0.17
// (the flag has no reader in WiFiSTA.cpp/WiFiGeneric.cpp), so nothing else
// re-establishes the link after an AP restart or a dropout. Do not delete.
// Also the only place the link is announced: begin() no longer waits, so the
// first connected poll in loop() is the first chance to report an IP.
void maintainWifi(uint32_t nowMs) {
  static uint32_t lastAttemptMs = 0;
  static bool reported = false;
  if (WiFi.status() == WL_CONNECTED) {
    if (!reported) {
      reported = true;
      Serial.print("WiFi up: http://");
      Serial.println(WiFi.localIP());
    }
    return;
  }
  if (nowMs - lastAttemptMs < kWifiRetryIntervalMs) {
    return;
  }
  lastAttemptMs = nowMs;
  WiFi.reconnect();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // HLK-LD1020 radar output: driven high on detection. Read on every loop tick;
  // the effect decides what to do with the level (see the mode list in
  // AGENTS.md). Pulled down so a disconnected module never reads as detection.
  pinMode(ledfx::kRadarPin, INPUT_PULLDOWN);

  if (!tlc.begin()) {
    Serial.println("TLC5947 init failed");
  }
  pushFrame();  // all channels off as soon as the driver is up

  // Order matters: this is what initializes the netif/tcpip stack that
  // WebServer::begin() needs, but it no longer waits for the AP, so the rig
  // animates on every power cycle even when the AP is missing.
  beginWifi();

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/mode", HTTP_POST, handleMode);
  server.begin();
  Serial.printf("HTTP server listening on port %d\n", kHttpPort);

  engine.setMode(ledfx::kModeSmolder, 0, millis());  // embers, then sleep, green agitation on motion
}

void loop() {
  server.handleClient();
  const uint32_t now = millis();
  maintainWifi(now);
  if (engine.tick(now, radarActive())) {
    pushFrame();
  }
}
