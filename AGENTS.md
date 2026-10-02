# AGENTS.md

ESP32 firmware for a TLC5947-driven LED rig with an HLK-LD1020 radar input.
PlatformIO + Arduino framework, board `esp32dev`.

## Commands

PlatformIO Core lives in `~/.platformio/penv` on this machine:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio test -e native        # host unit tests, no hardware needed
pio run -e esp32dev       # compile the firmware (first run downloads ~1 GB toolchain)
pio run -e esp32dev -t upload   # flash - requires USB, not possible in this container
pio device monitor -b 115200
```

`pio test` and `pio run` use the same checkout; there is no separate test project.

## Layout

|Path|Role|
|---|---|
|`lib/ledfx/led_layout.h`|single source of truth: pins, fitted channel range, PWM range, all fade/pulse tuning|
|`lib/ledfx/modes.h`|`Mode` enum (`kModePulse` = 0, `kModeSequentialFade` = 1), `kModeCount`, `parseModeArg`|
|`lib/ledfx/led_engine.{h,cpp}`|pure effect engine - no Arduino headers, fully host-testable|
|`src/main.cpp`|ESP32-only glue: WiFi, `WebServer`, `Adafruit_TLC5947`, radar read|
|`test/native/test_ledfx/`|host Unity suite `native/ledfx`|
|`include/secrets.h(.example)`|WiFi credentials; the real header is git-ignored|
|`PINS.md`|physical wiring|

## Architecture

- All effect logic lives in `ledfx::LedEngine` and is driven entirely by
  `tick(nowMs, radarHigh)`; nothing in `lib/` includes Arduino. Keep it that way -
  that is what makes the behaviour testable in this container.
- `LedEngine::tick` returns true only when a channel value changed; `main.cpp`
  pushes a TLC5947 frame on true and otherwise does nothing. Do not push frames
  unconditionally: the driver bit-bangs 24x12 bits per write.
- Both modes drive only the fitted channels (`kFirstChannel..kLastChannel`);
  the rest of the frame stays 0. Which channel carries which colour is still
  unmapped, so all fitted channels behave identically.
- `loop()` has no `delay()`; the fade and HTTP serving must keep coexisting.

### Modes (`POST /mode?m=<n>`)

- **0 `kModePulse`** - every fitted channel breathes in lockstep, forever. Cycle is
  `2*kPulseHalfSlowMs` (2 s) while the radar pin is low and `2*kPulseHalfFastMs`
  (0.5 s) while it is high. The phase is carried across a speed switch, so the
  brightness does not jump.
- **1 `kModeSequentialFade`** - one globally synchronised fade where channel `c`
  goes dark after its `c`-th full cycle (channel 6 fades 6 times, channel 17
  ends the run at ~17.4 s). Then the engine reports `finished()` and holds all
  channels off. Ignores the radar.
- Mode numbers are the enum values and are part of the HTTP contract; adding a
  mode means adding an enumerator before `kModeCount` and a branch in
  `LedEngine::tick`. Never renumber an existing mode.
- Request validation is only `parseModeArg`: digits-only, value < `kModeCount`.
  No separate range check in the handler.

### HTTP

- `GET /health` -> 200 (empty body) when WiFi is connected, 503 otherwise.
- `POST /mode?m=<n>` -> 200 `ok\n` on success, 400 `invalid mode\n` otherwise.
  The query string is parsed for any method/content type, so request bodies are
  ignored entirely; there is no JSON handling.

## Conventions & pitfalls

- Style: 2-space indent, `snake_case` for local helpers, `kCamelCase` constants,
  members with a trailing underscore. C++17. No heap use in `lib/ledfx`.
- Tuning constants live only in `led_layout.h`; the tests hard-code derived
  values from them (e.g. peak level 4095, one step = 63), so changing a constant
  means re-deriving and updating the corresponding test expectations.
- `test/native/test_ledfx/` is one PlatformIO suite: all files there link into a
  single binary, so exactly **one** file may define `main()` (currently
  `test_led_engine.cpp`, which runs the cases declared in `test_modes.cpp`).
- `platformio.ini`: `default_envs = esp32dev`; `test_ignore = native/*` keeps a
  bare `pio test` from compiling the host suite for the ESP32; `test_build_src = no`
  keeps Arduino-only `src/` out of the host binaries. `platform` is pinned to
  `espressif32@6.13.0` so the build matches; the APIs used are also valid on 7.x
  (changing that one line is the whole migration).
- WiFi credentials: copy `include/secrets.h.example` to `include/secrets.h` and
  fill it in. `include/secrets.h` is git-ignored - never commit it, and never
  add credentials to tracked files.
- `maintainWifi()` in `main.cpp` is load-bearing: `WiFi.setAutoReconnect()` is a
  no-op in arduino-esp32 2.0.17, so nothing else reconnects after an AP restart.
- GPIO2 (`kRadarPin`) is an ESP32 strapping pin: it must be low at reset for
  flashing, and a radar module that idles high can interfere with the boot mode.
  The code uses `INPUT_PULLDOWN`; this is a hardware note, not a code fix.
- Verification here is host tests + a firmware compile only. Flashing, the HTTP
  endpoints and the visual fade must be checked on the real board.
