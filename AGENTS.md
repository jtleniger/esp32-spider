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

`lib/ledfx/led_engine.{h,cpp}` is the effect engine; `led_layout.h` holds pins, wiring and all tuning constants; `modes.h` parses modes/channels. `src/main.cpp` is ESP32-only glue (WiFi, `WebServer`, TLC5947, radar). `test/native/test_ledfx/` is the host Unity suite. `PINS.md`/`CHANNELS.md` document wiring; git-ignored `include/secrets.h` holds WiFi credentials.

## Why the code is shaped this way

- **Nothing in `lib/` includes Arduino**; logic is driven by `tick(nowMs, radarHigh)`. That separation makes behaviour testable in this container, and `loop()` must never `delay()` so HTTP serving coexists.
- **`tick` returns true only when a channel changed**, and `main.cpp` pushes a TLC5947 frame only then - the driver bit-bangs 24x12 bits per write, so never push unconditionally.
- **Mode numbers are the HTTP contract**: add an enumerator before `kModeCount` plus a `tick` branch; never renumber an existing mode.
- **No heap use in `lib/ledfx`**; the smolder flicker hashes integers instead of `rand()` so the engine stays deterministic for host assertions.
- **Tuning constants live only in `led_layout.h`** but tests hard-code derived values (peak 4095, one step = 63), so changing one means updating those expectations.
- **One `main()` per suite**: all files under `test/native/test_ledfx/` link into one binary (`test_led_engine.cpp` defines `main()` and runs `test_modes.cpp`'s cases).
- **`maintainWifi()` is load-bearing**: `WiFi.setAutoReconnect()` is a no-op in arduino-esp32 2.0.17, so nothing else reconnects after an AP restart.
- **GPIO2 (`kRadarPin`) is a strapping pin** that must be low at reset; a radar idling high can block boot mode. `INPUT_PULLDOWN` accommodates the hardware, it isn't a bug fix.
- Style: 2-space indent, `snake_case` locals, `kCamelCase` constants, trailing `_` members, C++17.

## platformio.ini quirks

`test_ignore = native/*` keeps a bare `pio test` from building the host suite for the ESP32; `test_build_src = no` keeps Arduino-only `src/` out of host binaries. `platform` is pinned to `espressif32@6.13.0`; the APIs used also work on 7.x, so bumping that line is the whole migration.

## Verification limits

Host tests plus a firmware compile are all that can be checked here; flashing, HTTP endpoints and visual effects need the board.
