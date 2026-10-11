#include <Adafruit_TLC5947.h>
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <health.h>
#include <led_engine.h>
#include <led_layout.h>
#include <modes.h>
#include <tuning.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h: copy include/secrets.h.example and fill in WIFI_SSID/WIFI_PASSWORD"
#endif

namespace {

constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kWifiRetryIntervalMs = 5000;
// mDNS name: the rig answers as spider.local, so the UI does not depend on
// whatever address the AP hands out.
constexpr const char *kMdnsHostname = "spider";

// NTP: the rig has no RTC, so the clock comes from the network. lwIP's SNTP
// client retries with backoff (15 s, doubling to 150 s) while it cannot reach a
// server and re-polls hourly after a sync, so starting it at boot covers the
// usual power cycle where the AP is not up yet - maintainWifi() needs no clock
// retry. UTC by default; put the local offset here if log stamps should be
// local rather than unambiguous.
constexpr const char *kNtpServer = "pool.ntp.org";
constexpr long kNtpGmtOffsetSec = 0;
constexpr int kNtpDaylightOffsetSec = 0;

// Persisted tuning overrides (POST /config). One blob under one key: the field
// table in tuning.h is a struct layout, so the version is bumped whenever the
// layout or a default changes and a blob from an older build is discarded
// rather than reinterpreted. The length check catches a truncated write.
constexpr const char *kNvsNamespace = "ledfx";
constexpr const char *kNvsTuningKey = "tuning";
constexpr const char *kNvsVersionKey = "version";
constexpr uint32_t kTuningVersion = 1;

Adafruit_TLC5947 tlc(ledfx::kTlcDriverCount, ledfx::kTlcClockPin, ledfx::kTlcDataPin,
                     ledfx::kTlcLatchPin);
ledfx::LedEngine engine;
WebServer server(kHttpPort);
Preferences preferences;

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

// Seconds since the Unix epoch, or 0 before SNTP has set the clock (the system
// clock reads 1970 until then, and clockIsSet() in lib/ledfx is what decides
// whether that counts as real time).
uint32_t epochSeconds() {
  const time_t now = time(nullptr);
  return now > 0 ? static_cast<uint32_t>(now) : 0;
}

// Status document for monitoring: the mode number currently running (the same
// value POST /mode takes), whole seconds since boot, and the clock. JSON,
// because this endpoint accumulates fields (log counts, free space) and a parser
// should not have to care which ones are present. The body builder lives in
// lib/ledfx so the host suite can assert it; only the millis()/time() sources
// are Arduino-side.
void handleHealth() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/plain", "");
    return;
  }
  ledfx::HealthStatus status = {};
  status.mode = engine.mode();
  status.uptimeMs = millis();
  status.epochSeconds = epochSeconds();
  char body[ledfx::kHealthBodyCapacity];
  ledfx::formatHealth(body, sizeof(body), status);
  server.send(200, "application/json", body);
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

// Restores the overrides saved by POST /config. Anything that no longer fits
// the field table - a stored blob whose version or length predates a firmware
// change, or a value a range change made illegal - falls back to the built-in
// defaults, so a stale blob can never put the engine into an invalid state.
void loadTuning() {
  if (!preferences.begin(kNvsNamespace, false)) {
    Serial.println("tuning: NVS unavailable, using defaults");
    return;
  }
  const bool stored =
      preferences.getUInt(kNvsVersionKey, 0) == kTuningVersion &&
      preferences.getBytesLength(kNvsTuningKey) == sizeof(ledfx::Tuning);
  if (!stored) {
    Serial.println("tuning: no stored overrides, using defaults");
    return;
  }
  ledfx::Tuning saved;
  preferences.getBytes(kNvsTuningKey, &saved, sizeof(saved));
  ledfx::clampTuning(saved);
  const char *offender = nullptr;
  if (!ledfx::validateTuning(saved, &offender)) {
    Serial.printf("tuning: stored overrides rejected (%s), using defaults\n",
                  offender != nullptr ? offender : "out of range");
    return;
  }
  engine.tuning() = saved;
  Serial.println("tuning: restored stored overrides");
}

void saveTuning() {
  preferences.putBytes(kNvsTuningKey, &engine.tuning(), sizeof(ledfx::Tuning));
  preferences.putUInt(kNvsVersionKey, kTuningVersion);
}

void sendConfigError(const char *reason, const char *field) {
  server.send(400, "text/plain",
              String(reason) + ": " + (field != nullptr ? field : "?") + "\n");
}

// GET /config dumps the whole tuning table as JSON, under the names POST
// /config takes, so it doubles as the list of knobs and their current values.
// Static: the body is over 1.5 KB and the handler runs on the loop task's stack.
void handleConfigGet() {
  static char body[ledfx::kTuningBodyCapacity];
  ledfx::formatTuning(body, sizeof(body), engine.tuning());
  server.send(200, "application/json", body);
}

// POST /config?<field>=<value>&... applies every query argument as a tuning
// field, e.g. POST /config?smolder_dwell_ms=5000&smolder_flicker_pwm=0.
// All or nothing: one unknown name, out-of-range value or inconsistent
// combination rejects the whole request, so a tuning session can never leave a
// half-applied state behind. What is accepted is written to NVS, because the
// rig is power-cycled daily and a knob that only lived in RAM would be gone.
void handleConfigPost() {
  constexpr size_t kMaxEdits = 16;
  const size_t count = static_cast<size_t>(server.args());
  if (count == 0) {
    server.send(400, "text/plain", "no tuning fields\n");
    return;
  }
  if (count > kMaxEdits) {
    server.send(400, "text/plain", "too many tuning fields\n");
    return;
  }
  // argName()/arg() hand back String temporaries, so keep the copies alive for
  // as long as the edits point into them.
  String names[kMaxEdits];
  String values[kMaxEdits];
  ledfx::TuningEdit edits[kMaxEdits];
  for (size_t i = 0; i < count; ++i) {
    names[i] = server.argName(i);
    values[i] = server.arg(i);
    edits[i] = {names[i].c_str(), values[i].c_str()};
  }

  ledfx::Tuning next = engine.tuning();
  const char *offender = nullptr;
  switch (ledfx::applyTuningEdits(next, edits, count, &offender)) {
    case ledfx::TuningResult::kOk:
      break;
    case ledfx::TuningResult::kUnknownField:
      sendConfigError("unknown tuning field", offender);
      return;
    case ledfx::TuningResult::kOutOfRange:
      sendConfigError("value out of range", offender);
      return;
    case ledfx::TuningResult::kInconsistent:
      sendConfigError("inconsistent with another field", offender);
      return;
  }
  engine.tuning() = next;
  saveTuning();
  server.send(200, "text/plain", "ok\n");
}

// POST /config/reset drops the stored overrides and returns to the built-in
// defaults, so a knob set badly over HTTP is recoverable without reflashing.
void handleConfigReset() {
  engine.tuning() = ledfx::Tuning{};
  preferences.remove(kNvsTuningKey);
  preferences.remove(kNvsVersionKey);
  server.send(200, "text/plain", "ok\n");
}

// Advertises the rig as spider.local plus an _http._tcp service record.
//
// Safe to run before the AP is reachable: mdns_init() registers its own
// IP_EVENT/WIFI_EVENT handlers and tracks the STA netif, so the responder
// announces the moment the link comes up - which matters because the rig is
// power-cycled daily with the AP not always up (see beginWifi).
//
// Still must run after beginWifi(): mdns_init() resolves the STA netif by
// ifkey, and WiFi.mode() is what creates it. Failing here is not fatal: the rig
// keeps animating and serving over the IP, only the name is missing.
void beginMdns() {
  if (!MDNS.begin(kMdnsHostname)) {
    Serial.println("mDNS init failed");
    return;
  }
  MDNS.addService("http", "tcp", kHttpPort);
  Serial.printf("mDNS: http://%s.local/\n", kMdnsHostname);
}

// Starts the SNTP client. Not gated on the link: configTime() only installs the
// server list and the time zone and lets lwIP's client retry on its own, which
// is what makes the daily power cycle (AP often missing at boot) safe.
//
// Runs after beginWifi() for the same reason the server does - sntp_init()
// sends through lwip, and WiFi.mode() is what brings the netif/tcpip stack up.
// Failing to reach a server is not fatal: the effects do not use the clock,
// only the stamps /health and the logs report.
void beginNtp() {
  configTime(kNtpGmtOffsetSec, kNtpDaylightOffsetSec, kNtpServer);
  Serial.printf("NTP: %s, UTC offset %ld s\n", kNtpServer, kNtpGmtOffsetSec);
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

  // Before the first setMode/tick, so the rig boots straight into the saved
  // tuning instead of flashing its defaults for a frame.
  loadTuning();

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.on("/config/reset", HTTP_POST, handleConfigReset);
  server.begin();
  Serial.printf("HTTP server listening on port %d\n", kHttpPort);

  // After beginWifi(), like the server: mDNS needs the STA netif that
  // WiFi.mode() creates.
  beginMdns();

  // Also needs the netif stack, but not the AP: SNTP retries until it gets out.
  beginNtp();

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
