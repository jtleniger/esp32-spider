#pragma once

#include <cstdint>

// All hardware and effect tuning for the LED rig live here. CHANNELS.md is the
// wiring source of truth: which channel drives which colour LED, which braid
// (physical cable) it sits on and which eye it belongs to. That mapping lives
// in kEyes below. kModeSingleChannel has no tuning: it drives one channel at
// full PWM.
namespace ledfx {

// --- TLC5947 breakout wiring (see PINS.md) ---
constexpr uint8_t kTlcClockPin = 25;  // GPIO25 / Pin 9
constexpr uint8_t kTlcDataPin = 27;   // GPIO27 / Pin 11
constexpr uint8_t kTlcLatchPin = 32;  // GPIO32 / Pin 7

// --- Adafruit TLC5947 breakout, not chained ---
constexpr uint8_t kChannelsPerDriver = 24;
constexpr uint8_t kTlcDriverCount = 1;
constexpr uint8_t kChannelCount = kChannelsPerDriver * kTlcDriverCount;

// Every channel 0..kChannelCount-1 now drives an LED, so the effects fill the
// whole frame; there is no longer an unfitted/empty range.

// --- HLK-LD1020 radar: output is driven high while motion is detected ---
constexpr uint8_t kRadarPin = 2;  // GPIO2 / Pin 24

// --- Braids and eyes ---
// The rig is two physical cables ("braids") with 12 LEDs each. Four eyes hang
// off each braid and every eye holds one LED of each colour, so there are
// kEyeCount = 8 eyes and kEyeCount * kLedsPerEye = 24 LEDs.
constexpr uint8_t kBraidsPerRig = 2;
constexpr uint8_t kEyesPerBraid = 4;
constexpr uint8_t kEyeCount = kBraidsPerRig * kEyesPerBraid;
constexpr uint8_t kLedsPerEye = 3;

// One physical eye: which braid it hangs off and the channel driving each of
// its three LEDs. Channels come from CHANNELS.md; within a braid the eyes are
// numbered by rank pairing - eye i takes the i-th orange, i-th red and i-th
// green channel in ascending channel order. The table order (the four eyes of
// braid 1, then braid 2) is also the order kModeSmolder spreads its green
// flash across the rig.
struct EyeWiring {
  uint8_t braid;
  uint8_t orange;
  uint8_t red;
  uint8_t green;
};

constexpr EyeWiring kEyes[kEyeCount] = {
    // Braid 1: channels 0-5 and 18-23.
    {0, 0, 1, 2},
    {0, 19, 18, 3},
    {0, 21, 20, 4},
    {0, 23, 22, 5},
    // Braid 2: channels 6-17.
    {1, 6, 9, 8},
    {1, 7, 12, 10},
    {1, 16, 13, 11},
    {1, 17, 14, 15},
};

// --- TLC5947 PWM resolution (12 bit) ---
constexpr uint16_t kMaxPwm = 4095;

// --- kModePulse (mode 2) tuning: a full in+out cycle is twice the half cycle ---
constexpr uint32_t kPulseHalfSlowMs = 1000;  // 2 s cycle while the radar is idle
constexpr uint32_t kPulseHalfFastMs = 250;   // 0.5 s cycle while it sees motion
constexpr uint16_t kPulseLevels = 64;        // brightness rungs per half cycle

// --- kModeSmolder (mode 0) tuning ---
// Orange and red crossfade on a slow raised-cosine "breathing" curve; both keep
// a floor glow so the embers never go fully dark. Every eye runs the same curve
// with its own phase offset (kSmolderEyePhasePermille), so they never pulse in
// lockstep, and a per-channel pseudo-random flicker is layered on top.
constexpr uint32_t kSmolderPeriodMs = 4000;     // full O<->R crossfade cycle
constexpr uint16_t kSmolderLevels = 64;         // rungs the crossfade quantises to
constexpr uint16_t kSmolderFloorPwm = 256;      // dimmest ember glow
constexpr uint16_t kSmolderFlickerPwm = 420;    // +/- ember noise amplitude
constexpr uint32_t kSmolderFlickerStepMs = 47;  // flicker refresh interval

// Per-eye phase advance in permille of kSmolderPeriodMs, spread around the
// cycle; index == eye index, so no two eyes breathe together.
constexpr uint16_t kSmolderEyePhasePermille[kEyeCount] = {
    0, 137, 271, 419, 563, 701, 839, 967};

// Green motion flash: the greens snap on one eye after another, hold with ember
// flicker, then fade out while orange and red resume.
constexpr uint32_t kGreenSpreadMs = 120;    // stagger between consecutive eyes
constexpr uint32_t kGreenAttackMs = 40;     // snap-on ramp, effectively instant
constexpr uint32_t kGreenHoldMs = 3000;     // full-brightness hold
constexpr uint32_t kGreenFadeMs = 800;      // fade back into the embers
constexpr uint16_t kGreenFlickerPwm = 900;  // ember noise amplitude while lit

}  // namespace ledfx
