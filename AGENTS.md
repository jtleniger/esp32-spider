# AGENTS.md

ESP32 firmware (PlatformIO + Arduino, `esp32dev`) for a TLC5947 LED rig with an HLK-LD1020 radar input.

## Commands

PlatformIO Core lives in `~/.platformio/penv`; no USB in the container, so upload/monitor cannot run here.

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio test -e native   # host unit tests
pio run -e esp32dev  # compile (first run downloads ~1 GB)
```

## Layout

`lib/ledfx/led_engine.{h,cpp}` is the effect engine; `led_layout.h` holds pins, wiring and all tuning constants; `modes.h` parses modes/channels; `health.h` builds the `GET /health` body (`{"mode":M,"uptime_s":U,"utc":"...","clock_set":B,"log_files":N,"free_bytes":F}`, JSON because the endpoint keeps gaining fields); `tuning.h` is the knob table behind `POST /config`; `clock.h` formats the UTC stamps the status document reports; `logs.h` formats the motion-log names, activation lines, listing and validates a requested log name. `src/main.cpp` is ESP32-only glue (WiFi, mDNS, `WebServer`, LittleFS, TLC5947, radar). `test/native/test_ledfx/` is the host Unity suite. `PINS.md`/`CHANNELS.md` document wiring and `API.md` documents the HTTP surface; git-ignored `include/secrets.h` holds WiFi credentials.

## Why the code is shaped this way

- **Nothing in `lib/` includes Arduino**; logic is driven by `tick(nowMs, radarHigh)`. That separation makes behaviour testable in this container, and `loop()` must never `delay()` so HTTP serving coexists.
- **Tuning knobs are runtime state**: `led_layout.h` still owns the defaults, but the engine reads `ledfx::Tuning` (`tuning.h`), so `POST /config?<knob>=<value>` overrides a knob live and Preferences persists it (`POST /config/reset` drops the blob). Each knob's HTTP name, member and accepted range are one table row; the ranges are load-bearing, because the engine subtracts the min/max pairs into random spans and divides by the step knobs, so `validateTuning()` rejects an inverted pair instead of letting the engine divide by zero. Adding a knob means a `Tuning` field (defaulted to the constexpr), a table row, and swapping the engine's use of the constant - nothing else.
- **`tick` returns true only when a channel changed**, and `main.cpp` pushes a TLC5947 frame only then - the driver bit-bangs 24x12 bits per write, so never push unconditionally.
- **Mode numbers are the HTTP contract**: a new effect gets an enumerator with an explicit value, a `tick` branch and an `isValidMode()` case; never renumber or reuse a retired mode's number (4, 5 and 7 are dead holes left by removed effects). `kModeCount` is only the parser's numeric bound, not the live-mode count.
- **No heap use in `lib/ledfx`**; the smolder flicker hashes integers instead of `rand()` so the engine stays deterministic for host assertions.
- **Tuning constants live only in `led_layout.h`** but tests hard-code derived values (peak 4095, one step = 63), so changing one means updating those expectations.
- **One `main()` per suite**: all files under `test/native/test_ledfx/` link into one binary (`test_led_engine.cpp` defines `main()` and runs `test_modes.cpp`'s cases).
- **`maintainWifi()` is load-bearing**: `WiFi.setAutoReconnect()` is a no-op in arduino-esp32 2.0.17, so nothing else reconnects after an AP restart. It also owns the connect poll: `beginWifi()` deliberately does not wait, because `WiFi.waitForConnectResult()` is a blocking `delay()` loop and the rig is power-cycled daily with the AP not always up. WiFi must never gate the engine - the connect wait is gone and loop() only polls/retries - but `beginWifi()` must still precede `server.begin()`, since `WiFi.mode()` is what runs `esp_netif_init()`/`tcpip_init()` while `WiFiServer::begin()` opens a lwip socket immediately (dropping netif init first panics with `tcpip_send_msg_wait_sem (Invalid mbox)`).
- **Motion telemetry is one file per boot, named for the boot stamp**: `loop()` logs a line per radar rising edge; `beginLogs()` mounts LittleFS (the default table's `spiffs` partition, formatted on first use) and `/logs`, `/logs/<name>` and `/logs/clear` serve it. The file is created lazily, because its name needs the boot stamp (`epoch - uptime`, see the NTP bullet): activations before the clock is real are queued (newest 32) and written with their true stamps the moment it lands, so a power cycle with the AP missing still logs correctly. `/logs/<name>` only accepts names `isLogName()` recognises, which is what keeps the route inside the log directory - the bundled WebServer has no pattern routes, so it is dispatched from `onNotFound()`. The listing keeps the newest `kMaxLogEntries` (names are fixed-width stamps, so a text sort is a chronological sort) and flags `truncated`.
- **The clock is NTP-only and only the stamps depend on it**: there is no RTC, so `beginNtp()` starts lwIP's SNTP client after `beginWifi()`. It is deliberately not gated on the link - the client retries with backoff (15 s doubling to 150 s) while it cannot get out and re-polls hourly after a sync, which is what makes the usual power cycle (AP missing at boot) safe, and why `maintainWifi()` has no clock retry. `clockIsSet()` (a 2021 threshold in `clock.h`) is how `/health` reports whether the stamp is real; the SNTP sync-status flag cannot, because it is reset right after each update. No effect reads the clock.
- **`spider.local` is mDNS, not a static IP**: `beginMdns()` runs after `beginWifi()` because `mdns_init()` resolves the STA netif by ifkey, which `WiFi.mode()` is what creates - but it does not need the AP to be up, since the responder registers its own netif/IP event handlers and announces whenever the link comes up (the AP is often missing at boot, see above). Init failure is logged and non-fatal; the rig still serves on its IP.
- **GPIO2 (`kRadarPin`) is a strapping pin** that must be low at reset; a radar idling high can block boot mode. `INPUT_PULLDOWN` accommodates the hardware, it isn't a bug fix.
- Style: 2-space indent, `snake_case` locals, `kCamelCase` constants, trailing `_` members, C++17.

## platformio.ini quirks

`test_ignore = native/*` keeps a bare `pio test` from building the host suite for the ESP32; `test_build_src = no` keeps Arduino-only `src/` out of host binaries. `platform` is pinned to `espressif32@6.13.0`; the APIs used also work on 7.x, so bumping that line is the whole migration.

## Verification limits

Host tests plus a firmware compile are all that can be checked here; flashing, HTTP endpoints and visual effects need the board.
