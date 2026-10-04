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
// Orange and red crossfade on a slow "breathing" curve that dwells on each
// colour before easing across to the other; both keep a floor glow so the embers
// never go fully dark. Every eye runs the same curve with its own phase offset
// (kSmolderEyePhasePermille), so they never pulse in lockstep, and a per-channel
// pseudo-random flicker is layered on top.
// A full cycle is: hold red, crossfade to orange, hold orange, crossfade back.
constexpr uint32_t kSmolderDwellMs = 7000;      // hold on one colour, bright
constexpr uint32_t kSmolderFadeMs = 7000;       // eased crossfade to the other
constexpr uint32_t kSmolderPeriodMs = 2 * (kSmolderDwellMs + kSmolderFadeMs);
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

// --- kModeStalker (mode 3) tuning ---
// One dim eye at a time scans around the rig; the next eye fades in as the head
// moves on, so one or two eyes are ever lit. A radar level ramps the whole rig
// to the bright red pop and lets it fall back to scanning.
constexpr uint32_t kStalkerStepMs = 900;         // dwell per eye while scanning
constexpr uint16_t kStalkerDimPwm = 700;         // peak of the dim scanning glow
constexpr uint16_t kStalkerFlickerPwm = 90;      // subtle shimmer on the scan
constexpr uint32_t kStalkerFlickerStepMs = 53;   // shimmer refresh interval
constexpr uint32_t kStalkerPopMs = 220;          // ramp up to the pop on motion
constexpr uint32_t kStalkerReleaseMs = 900;      // decay back to scanning
constexpr uint16_t kStalkerPopPwm = kMaxPwm;     // settled colour level (red)

// --- kModeBlink (mode 4) tuning ---
// Each blink lights a random group of eyes for kBlinkOnMs; one blink in
// kBlinkDoubleOneIn is a double blink (a second pulse after kBlinkOffMs). Within
// a blinking eye each of its three LEDs lights independently, so the eyes show
// random colour mixes. Gaps between blinks are hashed from kBlinkMinGapMs up to
// kBlinkMaxGapMs.
constexpr uint32_t kBlinkOnMs = 90;              // lit phase of one blink pulse
constexpr uint32_t kBlinkOffMs = 120;            // dark gap inside a double blink
constexpr uint32_t kBlinkMinGapMs = 220;         // minimum dark time between blinks
constexpr uint32_t kBlinkMaxGapMs = 1500;        // maximum dark time between blinks
constexpr uint16_t kBlinkGroupPermille = 450;    // chance an eye joins a group
constexpr uint16_t kBlinkLedPermille = 650;      // chance an LED lights within a blinking eye
constexpr uint16_t kBlinkDoubleOneIn = 5;        // 1 in N blinks is a double blink
constexpr uint16_t kBlinkLevelPwm = kMaxPwm;     // full brightness

// --- kModeHeartbeat (mode 5) tuning ---
// A "lub-dub" pair of thumps per beat in red. Agitation ramps up while motion is
// seen and decays once it stops: calm beats are green, agitated beats are red,
// and the beat period shortens with agitation. Orange pulses as a warm share of
// the red beat.
constexpr uint32_t kHeartbeatCalmPeriodMs = 1400;  // beat period with no motion
constexpr uint32_t kHeartbeatFastPeriodMs = 320;   // beat period at full agitation
constexpr uint32_t kHeartbeatEscalateMs = 2500;    // motion time to full agitation
constexpr uint32_t kHeartbeatCoolMs = 6000;        // calm time to shed agitation
constexpr uint32_t kHeartbeatThumpMs = 90;         // width of one thump
constexpr uint32_t kHeartbeatDubDelayMs = 170;     // lub -> dub start spacing
constexpr uint16_t kHeartbeatLubGainPermille = 1000;
constexpr uint16_t kHeartbeatDubGainPermille = 650;
constexpr uint16_t kHeartbeatOrangePermille = 500; // orange share of the red beat
constexpr uint32_t kHeartbeatLevels = 64;          // quantise the envelope

// --- kModeToxic (mode 6) tuning ---
// A green bubbling base with a travelling orange spark that runs around the eye
// ring. Motion (a level) ramps toward frantic: the spark travels faster, is
// brighter, and its flicker amplitude grows. The red LEDs stay off.
constexpr uint16_t kToxicGreenBasePwm = 1400;      // steady green venom glow
constexpr uint16_t kToxicGreenFlickerPwm = 500;    // bubble amplitude
constexpr uint32_t kToxicGreenStepMs = 61;         // bubble refresh interval
constexpr uint16_t kToxicSparkIdlePwm = 1600;      // spark head, idle
constexpr uint16_t kToxicSparkHeadPwm = kMaxPwm;   // spark head, frantic
constexpr uint16_t kToxicOrangeFlickerPwm = 250;   // idle orange flicker
constexpr uint16_t kToxicOrangeFranticPwm = 700;   // extra orange flicker when frantic
constexpr uint32_t kToxicSparkIdleStepMs = 420;    // ms per eye, idle
constexpr uint32_t kToxicSparkFastStepMs = 130;    // ms per eye, frantic
constexpr uint16_t kToxicSparkTailPermille = 2400; // trailing glow length
constexpr uint32_t kToxicMotionRampMs = 600;       // ramp to/from frantic
constexpr uint32_t kToxicFlickerStepMs = 43;       // orange flicker refresh

// --- kModeHypnotic (mode 7) tuning ---
// A chase head rotates forward around the eye ring; each eye lights one LED by
// eye%kLedsPerEye, so the ring reads orange, red, green, orange, ... A trailing
// glow sits behind the head. The radar level selects the fast per-eye step.
constexpr uint32_t kHypnoSlowStepMs = 260;         // ms per eye, idle
constexpr uint32_t kHypnoFastStepMs = 80;          // ms per eye, radar high
constexpr uint16_t kHypnoTailPermille = 2200;      // trailing glow (2.2 eyes)
constexpr uint16_t kHypnoHeadPwm = kMaxPwm;        // head brightness

}  // namespace ledfx
