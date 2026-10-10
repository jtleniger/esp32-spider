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
// Orange and red crossfade on a slow "breathing" curve: each colour holds at
// full brightness while the other sits fully dark, then eases across, so each
// cycle has a clear red-only stretch and a clear orange-only stretch. Every eye
// runs the same curve with its own phase offset (kSmolderEyePhasePermille), so
// they never pulse in lockstep, and a per-channel pseudo-random flicker is
// layered on top.
// A full cycle is: hold red, crossfade to orange, hold orange, crossfade back.
constexpr uint32_t kSmolderDwellMs = 7000;      // hold on one colour, bright
constexpr uint32_t kSmolderFadeMs = 7000;       // eased crossfade to the other
constexpr uint32_t kSmolderPeriodMs = 2 * (kSmolderDwellMs + kSmolderFadeMs);
constexpr uint16_t kSmolderLevels = 64;         // rungs the crossfade quantises to
// +/- ember noise at full brightness, scaled down with the channel level, so the
// lit colour flickers hard while the dark one stays exactly off.
constexpr uint16_t kSmolderFlickerPwm = 1400;
constexpr uint32_t kSmolderFlickerStepMs = 47;  // flicker refresh interval

// Per-eye phase advance in permille of kSmolderPeriodMs, spread around the
// cycle; index == eye index, so no two eyes breathe together.
constexpr uint16_t kSmolderEyePhasePermille[kEyeCount] = {
    0, 137, 271, 419, 563, 701, 839, 967};

// --- kModeSmolder (mode 0) sleep/wake machine ---
// Awake -> asleep: while the radar is quiet the chance to nod off rises from
// kSmolderSleepAfterMs up to a forced onset at kSmolderSleepDeadlineMs, rolled
// every kSmolderSleepCheckMs.
constexpr uint32_t kSmolderSleepAfterMs = 30000;      // earliest sleep onset
constexpr uint32_t kSmolderSleepDeadlineMs = 240000;  // forced onset by here
constexpr uint32_t kSmolderSleepCheckMs = 5000;       // hazard roll interval

// Asleep -> awake: a random nap length.
constexpr uint32_t kSmolderSleepMinMs = 30000;
constexpr uint32_t kSmolderSleepMaxMs = 180000;

// Falling asleep: each eye's eyelid gesture starts at a random point in this
// window after sleep onset, so the rig darkens eye by eye over 5..20 s.
constexpr uint32_t kSleepFallMinMs = 5000;
constexpr uint32_t kSleepFallMaxMs = 20000;

// Occasional half-opens while asleep: gaps between events and group size.
constexpr uint32_t kSleepOpenMinGapMs = 5000;
constexpr uint32_t kSleepOpenMaxGapMs = 60000;
constexpr uint16_t kSleepOpenGroupPermille = 350;  // chance an eye joins an event

// The eyelid gesture: the eye's ember dims, flares a little, then goes out
// ("can't quite keep the eyelid open").
constexpr uint32_t kSleepGestureMs = 900;
constexpr uint32_t kSleepDipEndMs = 250;
constexpr uint32_t kSleepFlareEndMs = 550;
constexpr uint16_t kSleepDipPermille = 400;
constexpr uint16_t kSleepFlarePermille = 750;

// --- kModeSmolder agitation (green only) ---
// On motion the rig runs one of kAgitationKindCount green animations chosen at
// random per episode; it stays until the radar has been quiet for
// kAgitationHoldMs.
constexpr uint32_t kAgitationHoldMs = 1500;
constexpr uint16_t kAgitationFlickerPwm = 1200;   // shared flicker amplitude
constexpr uint32_t kAgitationFlickerStepMs = 43;  // shared flicker refresh
// Sweep: a bright eye bounces left<->right across the face (kEyes order 0..7)
// with a trailing glow.
constexpr uint32_t kAgitationSweepStepMs = 130;        // ms per eye of travel
constexpr uint16_t kAgitationSweepTailPermille = 1500; // 1.5-eye trail
// Dart: single eyes snap green for a step at a time, hopping unpredictably.
constexpr uint32_t kAgitationDartStepMs = 90;
// Ripple: the original eye-by-eye green spread, looping without a gap.
constexpr uint32_t kGreenSpreadMs = 120;    // stagger between consecutive eyes
constexpr uint32_t kGreenAttackMs = 40;     // snap-on ramp, effectively instant
constexpr uint32_t kGreenHoldMs = 3000;     // full-brightness hold
constexpr uint32_t kGreenFadeMs = 800;      // fade back out
constexpr uint16_t kGreenFlickerPwm = 1200; // ember noise at full brightness
constexpr uint32_t kAgitationRippleLapMs =
    (kEyeCount - 1) * kGreenSpreadMs + kGreenAttackMs + kGreenHoldMs + kGreenFadeMs;

// --- kModeStalker (mode 3) tuning ---
// One dim eye at a time scans around the rig; each scanning eye lights a single
// pseudo-randomly chosen colour (reshuffled on every lap) and the next eye fades
// in as the head moves on, so one or two eyes are ever lit. Green is scaled down
// because those LEDs read brighter, and a radar level ramps the whole rig to a
// bright red pop that stutters.
constexpr uint32_t kStalkerStepMs = 900;         // dwell per eye while scanning
constexpr uint16_t kStalkerDimPwm = 700;         // peak of the dim scanning glow
constexpr uint16_t kStalkerGreenPermille = 620;  // green read-brightness trim
constexpr uint16_t kStalkerFlickerPwm = 600;     // shimmer on the scan (at full)
constexpr uint32_t kStalkerFlickerStepMs = 53;   // shimmer refresh interval
constexpr uint32_t kStalkerPopMs = 220;          // ramp up to the pop on motion
constexpr uint32_t kStalkerReleaseMs = 900;      // decay back to scanning
constexpr uint16_t kStalkerPopPwm = kMaxPwm;     // settled colour level (red)
constexpr uint16_t kStalkerPopFlickerPwm = 1000; // stutter on the wake-up (at full)

// --- kModeBlink (mode 4) tuning ---
// Each blink lights a random group of eyes for kBlinkOnMs and gives every
// blinking eye a single pseudo-randomly chosen colour; one blink in
// kBlinkDoubleOneIn is a double blink (a second pulse after kBlinkOffMs). The
// lit level sputters by kBlinkFlickerPwm, and the dark gaps between blinks are
// hashed from kBlinkMinGapMs up to kBlinkMaxGapMs (longer on average).
constexpr uint32_t kBlinkOnMs = 160;             // lit phase of one blink pulse
constexpr uint32_t kBlinkOffMs = 150;            // dark gap inside a double blink
constexpr uint32_t kBlinkMinGapMs = 420;         // minimum dark time between blinks
constexpr uint32_t kBlinkMaxGapMs = 2600;        // maximum dark time between blinks
constexpr uint16_t kBlinkGroupPermille = 450;    // chance an eye joins a group
constexpr uint16_t kBlinkDoubleOneIn = 5;        // 1 in N blinks is a double blink
constexpr uint16_t kBlinkLevelPwm = kMaxPwm;     // blink peak brightness
constexpr uint16_t kBlinkFlickerPwm = 900;       // sputter amplitude
constexpr uint32_t kBlinkFlickerStepMs = 37;     // sputter refresh interval

// --- kModeHeartbeat (mode 5) tuning ---
// A "lub-dub" pair of thumps per beat. Agitation ramps up while motion is seen
// and decays once it stops: calm beats are green (trimmed by
// kHeartbeatGreenPermille because green reads brighter), agitated beats are
// red, and the beat period shortens with agitation. Orange pulses as a warm
// share of the red beat, and every thump carries a flicker.
constexpr uint32_t kHeartbeatCalmPeriodMs = 1900;  // beat period with no motion
constexpr uint32_t kHeartbeatFastPeriodMs = 430;   // beat period at full agitation
constexpr uint32_t kHeartbeatEscalateMs = 2500;    // motion time to full agitation
constexpr uint32_t kHeartbeatCoolMs = 6000;        // calm time to shed agitation
constexpr uint32_t kHeartbeatThumpMs = 160;        // width of one thump (gradual)
constexpr uint32_t kHeartbeatDubDelayMs = 260;     // lub -> dub start spacing
constexpr uint16_t kHeartbeatLubGainPermille = 1000;
constexpr uint16_t kHeartbeatDubGainPermille = 650;
constexpr uint16_t kHeartbeatOrangePermille = 500; // orange share of the red beat
constexpr uint16_t kHeartbeatGreenPermille = 620;  // green read-brightness trim
constexpr uint16_t kHeartbeatFlickerPwm = 260;     // thump flicker amplitude
constexpr uint32_t kHeartbeatFlickerStepMs = 43;   // flicker refresh interval
constexpr uint32_t kHeartbeatLevels = 64;          // quantise the envelope

// --- kModeToxic (mode 6) tuning ---
// A dim green bubbling base with an isolated spark that runs around the eye
// ring; each revolution the spark picks orange or red at random. Motion (a
// level) ramps toward frantic: the spark travels faster, is brighter, and its
// flicker amplitude grows.
constexpr uint16_t kToxicGreenBasePwm = 700;       // steady green venom glow
constexpr uint16_t kToxicGreenFlickerPwm = 1000;   // bubble amplitude (at full)
constexpr uint32_t kToxicGreenStepMs = 61;         // bubble refresh interval
constexpr uint16_t kToxicSparkIdlePwm = 1600;      // spark head, idle
constexpr uint16_t kToxicSparkHeadPwm = kMaxPwm;   // spark head, frantic
constexpr uint16_t kToxicSparkFlickerPwm = 700;    // idle spark flicker (at full)
constexpr uint16_t kToxicSparkFranticPwm = 1400;   // spark flicker when frantic
constexpr uint32_t kToxicSparkIdleStepMs = 420;    // ms per eye, idle
constexpr uint32_t kToxicSparkFastStepMs = 130;    // ms per eye, frantic
constexpr uint16_t kToxicSparkTailPermille = 900;  // trailing glow: head only
constexpr uint32_t kToxicMotionRampMs = 600;       // ramp to/from frantic
constexpr uint32_t kToxicFlickerStepMs = 43;       // spark flicker refresh

// --- kModeHypnotic (mode 7) tuning ---
// A chase head rotates forward around the eye ring; every lit eye shows a
// single pseudo-randomly chosen colour (reshuffled each revolution) and a
// trailing glow sits behind the head. The per-eye rate is mixed with a triangle
// wave so the chase does not run at a constant speed, and the radar level
// selects the fast base step.
constexpr uint32_t kHypnoSlowStepMs = 260;         // ms per eye, idle
constexpr uint32_t kHypnoFastStepMs = 80;          // ms per eye, radar high
constexpr uint16_t kHypnoTailPermille = 2200;      // trailing glow (2.2 eyes)
constexpr uint16_t kHypnoHeadPwm = kMaxPwm;        // head brightness
constexpr uint16_t kHypnoFlickerPwm = 380;         // flicker on the chase
constexpr uint32_t kHypnoFlickerStepMs = 41;       // flicker refresh interval
constexpr uint32_t kHypnoSurgePeriodMs = 2600;     // speed-modulation period
constexpr uint16_t kHypnoSurgePermille = 350;      // +/- 35% rate swing

}  // namespace ledfx
