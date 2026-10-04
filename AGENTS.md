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
|`lib/ledfx/led_layout.h`|single source of truth: pins, braid/eye wiring (`kEyes`), PWM range, and the tuning constants for every effect besides `kModeSingleChannel`|
|`lib/ledfx/modes.h`|`Mode` enum (`kModeSmolder` = 0, `kModeSingleChannel` = 1, `kModePulse` = 2, `kModeStalker` = 3, `kModeBlink` = 4, `kModeHeartbeat` = 5, `kModeToxic` = 6, `kModeHypnotic` = 7), `kModeCount`, `parseModeArg`, `parseChannelArg`|
|`lib/ledfx/led_engine.{h,cpp}`|pure effect engine - no Arduino headers, fully host-testable|
|`src/main.cpp`|ESP32-only glue: WiFi, `WebServer`, `Adafruit_TLC5947`, radar read|
|`test/native/test_ledfx/`|host Unity suite `native/ledfx`|
|`include/secrets.h(.example)`|WiFi credentials; the real header is git-ignored|
|`PINS.md`|physical wiring|
|`CHANNELS.md`|hand-maintained channel -> braid/colour wiring; `kEyes` mirrors it|

## Architecture

- All effect logic lives in `ledfx::LedEngine` and is driven entirely by
  `tick(nowMs, radarHigh)`; nothing in `lib/` includes Arduino. Keep it that way -
  that is what makes the behaviour testable in this container.
- `LedEngine::tick` returns true only when a channel value changed; `main.cpp`
  pushes a TLC5947 frame on true and otherwise does nothing. Do not push frames
  unconditionally: the driver bit-bangs 24x12 bits per write.
- Every channel 0..kChannelCount-1 now drives an LED, so the effects fill the
  whole frame. Each channel belongs to exactly one eye (`kEyes`): one braid (a
  physical cable) with one LED of each colour. `kModeSmolder` renders per eye;
  `kModePulse` ignores the table and drives every channel in lockstep;
  `kModeSingleChannel` drives exactly the one channel named by the request.
  `kModeStalker`, `kModeToxic` and `kModeHypnotic` also work per eye (the toxic
  and hypnotic effects leave the red or non-chase LEDs dark), and `kModeBlink`
  lights a hashed subset of eyes; all are stitched together through `kEyes`.
- `loop()` has no `delay()`; the effect and HTTP serving must keep coexisting.

### Modes (`POST /mode?m=<n>`)

- **0 `kModeSmolder`** - the idle look, booted into by `setup()`. Each eye
  crossfades its orange and red LEDs on a slow "breathing" curve that dwells on
  one colour (`kSmolderDwellMs`, 7 s), eases across to the other over
  `kSmolderFadeMs` (7 s) and repeats, for a `kSmolderPeriodMs` full cycle of 28 s.
  It keeps a floor glow (`kSmolderFloorPwm`),
  with a deterministic per-channel flicker (`kSmolderFlickerPwm`) layered on like
  embers. Every eye gets its own phase offset (`kSmolderEyePhasePermille`), so the
  eyes never pulse in lockstep. A radar rising edge starts the green flash: the
  greens snap on one eye at a time (`kGreenSpreadMs`), hold `kGreenHoldMs` with
  flicker, then fade over `kGreenFadeMs` while orange and red resume. The radar
  level matters only for that rising edge.
- **1 `kModeSingleChannel`** - lights exactly the channel named by `?c=<channel>`
  at full PWM (`kMaxPwm`) and holds it; every other channel stays off. Ignores
  the radar. Used to identify a physical LED during wiring checks: issue `c=6`,
  note the LED, then `c=7`, and so on.
- **2 `kModePulse`** - every channel breathes in lockstep, forever. Cycle is
  `2*kPulseHalfSlowMs` (2 s) while the radar pin is low and `2*kPulseHalfFastMs`
  (0.5 s) while it is high. The phase is carried across a speed switch, so the
  brightness does not jump.
- **3 `kModeStalker`** - one dim eye at a time scans around the rig (a
  `kStalkerStepMs` dwell, crossfaded to the next eye), with a subtle shimmer.
  The radar is a level, not an edge: while it is high an alert integrator ramps
  every eye to the bright red `kStalkerPopPwm` pop (`kStalkerPopMs` up,
  `kStalkerReleaseMs` down), which fades the dim scan out and back in.
- **4 `kModeBlink`** - ignores the radar. A hashed subset of eyes
  (`kBlinkGroupPermille`) flashes for `kBlinkOnMs`; one blink in
  `kBlinkDoubleOneIn` is a double blink (`kBlinkOffMs` dark gap between pulses).
  Within a blinking eye each of its three LEDs lights independently
  (`kBlinkLedPermille`), so the eyes show random colour mixes. Gaps between
  blinks are hashed from `kBlinkMinGapMs`..`kBlinkMaxGapMs`.
- **5 `kModeHeartbeat`** - a "lub-dub" pair of thumps per beat, in green while
  calm and red as motion persists. The radar drives an agitation integrator
  (`kHeartbeatEscalateMs` up, `kHeartbeatCoolMs` down) that both recolours and
  shortens the beat (`kHeartbeatCalmPeriodMs`..`kHeartbeatFastPeriodMs`). Orange
  pulses as `kHeartbeatOrangePermille` of the red beat.
- **6 `kModeToxic`** - a green bubbling base (`kToxicGreenBasePwm` with a per-
  channel flicker) with a travelling orange spark that runs around the eye ring
  at `kToxicSparkIdleStepMs` per eye and leaves a trailing glow
  (`kToxicSparkTailPermille`). The radar ramps the spark toward frantic: faster
  (`kToxicSparkFastStepMs`), brighter and a wider flicker. The red LEDs stay off.
- **7 `kModeHypnotic`** - a chase head rotates forward around the eye ring at
  `kHypnoSlowStepMs` per eye; each eye lights one LED by `eye % kLedsPerEye`, so
  the ring reads orange, red, green, orange, ... A trailing glow
  (`kHypnoTailPermille`) sits behind the head. The radar level selects the fast
  step (`kHypnoFastStepMs`); the direction stays forward.
- The smolder flicker uses an integer hash (`flicker()` in `led_engine.cpp`)
  rather than `rand()`: the engine must stay deterministic so the host tests can
  assert bounds, and `lib/ledfx` stays allocation-free.
- Mode numbers are the enum values and are part of the HTTP contract; adding a
  mode means adding an enumerator before `kModeCount` and a branch in
  `LedEngine::tick`. Never renumber an existing mode.
- Request validation is `parseModeArg` (digits-only, value < `kModeCount`) plus,
  for `kModeSingleChannel`, `parseChannelArg` (digits-only, value <
  `kChannelCount`). `LedEngine::setMode` re-checks both bounds.

### HTTP

- `GET /health` -> 200 (empty body) when WiFi is connected, 503 otherwise.
- `POST /mode?m=<n>` -> 200 `ok\n` on success. `c=<channel>` selects the channel
  for `m=1` and is required there; it is rejected with 400 `invalid channel\n`
  when missing or out of range. A bad `m` is 400 `invalid mode\n`. The query
  string is parsed for any method/content type, so request bodies are ignored
  entirely; there is no JSON handling.

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
  endpoints and the visual smolder/pulse/channel selection must be checked on
  the board.
