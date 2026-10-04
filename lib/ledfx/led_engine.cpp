#include "led_engine.h"

#include <cstring>

namespace ledfx {
namespace {

static_assert(kPulseHalfSlowMs != 0 && kPulseHalfFastMs != 0 && kPulseLevels != 0,
              "kModePulse needs nonzero tuning");
static_assert(kSmolderFadeMs != 0 && kSmolderLevels != 0 &&
                  kSmolderFlickerStepMs != 0,
              "kModeSmolder needs nonzero tuning");
static_assert(kEyeCount * kLedsPerEye == kChannelCount,
              "every channel must belong to exactly one eye LED");
static_assert(kStalkerPopMs != 0 && kStalkerReleaseMs != 0 &&
                  kStalkerStepMs != 0 && kStalkerFlickerStepMs != 0,
              "kModeStalker needs nonzero tuning");
static_assert(kBlinkOnMs != 0 && kBlinkOffMs != 0 &&
                  kBlinkMinGapMs < kBlinkMaxGapMs && kBlinkDoubleOneIn != 0 &&
                  kBlinkLevelPwm != 0,
              "kModeBlink needs nonzero tuning");
static_assert(kHeartbeatThumpMs >= 2 && kHeartbeatLevels != 0 &&
                  kHeartbeatCalmPeriodMs > kHeartbeatFastPeriodMs &&
                  kHeartbeatEscalateMs != 0 && kHeartbeatCoolMs != 0 &&
                  kHeartbeatFastPeriodMs >= kHeartbeatDubDelayMs + kHeartbeatThumpMs,
              "kModeHeartbeat needs consistent tuning");
static_assert(kToxicSparkIdleStepMs > kToxicSparkFastStepMs &&
                  kToxicSparkFastStepMs != 0 && kToxicSparkTailPermille != 0 &&
                  kToxicGreenStepMs != 0 && kToxicFlickerStepMs != 0 &&
                  kToxicMotionRampMs != 0,
              "kModeToxic needs nonzero tuning");
static_assert(kHypnoSlowStepMs > kHypnoFastStepMs && kHypnoFastStepMs != 0 &&
                  kHypnoTailPermille != 0 &&
                  kHypnoTailPermille <= kEyeCount * 1000,
              "kModeHypnotic needs consistent tuning");

// Sentinel for "no green flash has been triggered yet".
constexpr uint32_t kNoGreenTrigger = 0xFFFFFFFFu;

uint16_t saturatePwm(int32_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > kMaxPwm) {
    return kMaxPwm;
  }
  return static_cast<uint16_t>(value);
}

// PWM level at `up` (0..halfMs) through a half cycle, quantised to
// kPulseLevels rungs of equal width.
uint16_t levelForPulse(uint32_t up, uint32_t halfMs) {
  const uint32_t rung = up * kPulseLevels / halfMs;
  return static_cast<uint16_t>(rung * kMaxPwm / kPulseLevels);
}

// Orange/red crossfade parameter for a cycle position, in permille (0..1000),
// quantised to kSmolderLevels rungs so the silent stretches between rungs do not
// push a frame. The cycle holds at 0 for kSmolderDwellMs, eases up to 1000 over
// kSmolderFadeMs, holds, then eases back. The smoothstep easing leaves and
// enters each hold with zero slope, so the brightness has no kink where the
// fade meets the dwell.
uint16_t breathePermille(uint32_t cyclePosMs) {
  const uint32_t pos = cyclePosMs % kSmolderPeriodMs;
  const uint32_t riseEnd = kSmolderDwellMs + kSmolderFadeMs;
  const uint32_t fallStart = 2 * kSmolderDwellMs + kSmolderFadeMs;
  const uint64_t fadeCubed =
      static_cast<uint64_t>(kSmolderFadeMs) * kSmolderFadeMs * kSmolderFadeMs;
  uint32_t permille;
  if (pos < kSmolderDwellMs) {
    permille = 0;
  } else if (pos < riseEnd) {
    const uint32_t t = pos - kSmolderDwellMs;
    permille = static_cast<uint32_t>(
        static_cast<uint64_t>(t) * t * (3 * kSmolderFadeMs - 2 * t) * 1000 /
        fadeCubed);
  } else if (pos < fallStart) {
    permille = 1000;
  } else {
    const uint32_t t = pos - fallStart;
    permille = 1000 - static_cast<uint32_t>(
                           static_cast<uint64_t>(t) * t *
                           (3 * kSmolderFadeMs - 2 * t) * 1000 / fadeCubed);
  }
  return static_cast<uint16_t>(permille * kSmolderLevels / 1000) * 1000 /
         kSmolderLevels;
}

// Cheap deterministic integer hash. Keeps the engine off rand() (host tests
// must not depend on libc randomness) and allocation-free.
uint32_t hash32(uint32_t x) {
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  x *= 3266489917u;
  x ^= x >> 16;
  return x;
}

// Deterministic per-channel pseudo-random value in [-amplitude, +amplitude]
// that changes once per flicker step.
int32_t flicker(uint8_t channel, uint32_t step, uint16_t amplitude) {
  const uint32_t span = 2u * amplitude + 1u;
  return static_cast<int32_t>(
             hash32(static_cast<uint32_t>(channel) * 2654435761u +
                    step * 40503u + 0x9E3779B9u) %
             span) -
         amplitude;
}

// Deterministic value in [0, span) from a seed. span must be nonzero.
uint32_t hashRange(uint32_t seed, uint32_t span) { return hash32(seed) % span; }

int32_t scalePermille(int32_t value, uint16_t permille) {
  return static_cast<int32_t>(static_cast<int64_t>(value) * permille / 1000);
}

// Linear interpolation: `from` at permille 0, `to` at permille 1000.
int32_t lerpPermille(int32_t from, int32_t to, uint16_t permille) {
  return from + static_cast<int32_t>(
                    static_cast<int64_t>(to - from) * permille / 1000);
}

// Q16 ramp value: kRampScale means fully on. `up` advances by dtMs/rateMs of
// full scale; the fixed point keeps per-millisecond increments from rounding
// to zero (e.g. a 6 s decay still moves at a 1 ms tick).
constexpr uint32_t kRampScale = 1u << 16;

uint32_t rampToward(uint32_t value, uint32_t dtMs, uint32_t rateMs, bool up) {
  const uint32_t step = static_cast<uint32_t>(
      static_cast<uint64_t>(dtMs) * kRampScale / rateMs);
  if (up) {
    const uint64_t sum = static_cast<uint64_t>(value) + step;
    return sum > kRampScale ? kRampScale : static_cast<uint32_t>(sum);
  }
  return step >= value ? 0 : value - step;
}

uint16_t permilleOf(uint32_t ramp) {
  return static_cast<uint16_t>(static_cast<uint64_t>(ramp) * 1000 / kRampScale);
}

// Green flash envelope for one eye at `elapsedMs` since that eye's flash
// started, in permille (0..1000): fast attack, flat hold, then a linear fade.
uint16_t greenEnvelopePermille(uint32_t elapsedMs) {
  if (elapsedMs < kGreenAttackMs) {
    return static_cast<uint16_t>(static_cast<uint64_t>(elapsedMs) * 1000 /
                                 kGreenAttackMs);
  }
  const uint32_t fadeStart = kGreenAttackMs + kGreenHoldMs;
  if (elapsedMs < fadeStart) {
    return 1000;
  }
  if (elapsedMs < fadeStart + kGreenFadeMs) {
    return static_cast<uint16_t>(
        1000 - static_cast<uint64_t>(elapsedMs - fadeStart) * 1000 /
                   kGreenFadeMs);
  }
  return 0;
}

// One triangular thump starting at `startMs` inside the beat, scaled by
// `gainPermille` (0..1000). Zero outside its kHeartbeatThumpMs window.
uint16_t thumpPermille(uint32_t phaseMs, uint32_t startMs, uint16_t gainPermille) {
  if (phaseMs < startMs) {
    return 0;
  }
  const uint32_t t = phaseMs - startMs;
  if (t >= kHeartbeatThumpMs) {
    return 0;
  }
  const uint32_t half = kHeartbeatThumpMs / 2;
  const uint32_t level = (t < half) ? (t * 1000 / half)
                                    : ((kHeartbeatThumpMs - t) * 1000 / half);
  return static_cast<uint16_t>(level * gainPermille / 1000);
}

// Lub + dub envelope of a beat, quantised to kHeartbeatLevels so the silent
// stretch between beats does not push a frame every tick.
uint16_t thumpEnergyPermille(uint32_t phaseMs) {
  const uint16_t lub = thumpPermille(phaseMs, 0, kHeartbeatLubGainPermille);
  const uint16_t dub = thumpPermille(phaseMs, kHeartbeatDubDelayMs,
                                     kHeartbeatDubGainPermille);
  const uint32_t peak = lub > dub ? lub : dub;
  return static_cast<uint16_t>(peak * kHeartbeatLevels / 1000) * 1000 /
         kHeartbeatLevels;
}

}  // namespace

LedEngine::LedEngine()
    : frame_{},
      mode_(kModeSmolder),
      channel_(0),
      phaseMs_(0),
      lastTickMs_(0),
      pulsePeriodMs_(2 * kPulseHalfSlowMs),
      smolderStartMs_(0),
      greenTriggerMs_(kNoGreenTrigger),
      prevRadarHigh_(false),
      stalkerAlert_(0),
      stalkerLastTickMs_(0),
      stalkerStartMs_(0),
      blinkActive_(false),
      blinkStartMs_(0),
      blinkNextMs_(0),
      blinkSeq_(0),
      blinkPulses_(0),
      blinkMask_(0),
      agitation_(0),
      beatPhaseMs_(0),
      beatPeriodMs_(kHeartbeatCalmPeriodMs),
      heartbeatLastTickMs_(0),
      toxicMotion_(0),
      toxicSparkPhase_(0),
      toxicLastTickMs_(0),
      hypnoPhase_(0),
      hypnoLastTickMs_(0),
      active_(false) {}

bool LedEngine::setMode(uint16_t mode, uint16_t channel, uint32_t nowMs) {
  if (mode >= kModeCount) {
    return false;
  }
  if (mode == kModeSingleChannel && channel >= kChannelCount) {
    return false;
  }
  mode_ = mode;
  channel_ = channel;
  phaseMs_ = 0;
  lastTickMs_ = nowMs;
  smolderStartMs_ = nowMs;
  greenTriggerMs_ = kNoGreenTrigger;
  prevRadarHigh_ = false;
  stalkerAlert_ = 0;
  stalkerLastTickMs_ = nowMs;
  stalkerStartMs_ = nowMs;
  blinkActive_ = false;
  blinkStartMs_ = nowMs;
  blinkNextMs_ = nowMs;  // first blink starts on the next tick
  blinkSeq_ = 0;
  blinkPulses_ = 0;
  blinkMask_ = 0;
  agitation_ = 0;
  beatPhaseMs_ = 0;
  beatPeriodMs_ = kHeartbeatCalmPeriodMs;
  heartbeatLastTickMs_ = nowMs;
  toxicMotion_ = 0;
  toxicSparkPhase_ = 0;
  toxicLastTickMs_ = nowMs;
  hypnoPhase_ = 0;
  hypnoLastTickMs_ = nowMs;
  active_ = true;
  std::memset(frame_, 0, sizeof(frame_));
  return true;
}

bool LedEngine::tick(uint32_t nowMs, bool radarHigh) {
  uint16_t next[kChannelCount];
  std::memset(next, 0, sizeof(next));

  if (active_) {
    switch (mode_) {
      case kModeSingleChannel:
        renderSingleChannel(next);
        break;
      case kModePulse:
        renderPulse(nowMs, radarHigh, next);
        break;
      case kModeStalker:
        renderStalker(nowMs, radarHigh, next);
        break;
      case kModeBlink:
        renderBlink(nowMs, next);
        break;
      case kModeHeartbeat:
        renderHeartbeat(nowMs, radarHigh, next);
        break;
      case kModeToxic:
        renderToxic(nowMs, radarHigh, next);
        break;
      case kModeHypnotic:
        renderHypnotic(nowMs, radarHigh, next);
        break;
      default:  // kModeSmolder
        renderSmolder(nowMs, radarHigh, next);
        break;
    }
  }

  const bool changed = std::memcmp(next, frame_, sizeof(frame_)) != 0;
  std::memcpy(frame_, next, sizeof(frame_));
  return changed;
}

void LedEngine::renderSingleChannel(uint16_t *out) {
  out[channel_] = kMaxPwm;
}

void LedEngine::renderPulse(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t periodMs =
      2 * (radarHigh ? kPulseHalfFastMs : kPulseHalfSlowMs);

  // Integrating the elapsed time (rather than taking nowMs modulo a fixed
  // period) keeps the phase continuous when the radar switches the period.
  const uint32_t dt = nowMs - lastTickMs_;  // uint32 wraparound is intentional
  lastTickMs_ = nowMs;
  if (periodMs != pulsePeriodMs_) {
    // Rescale the accumulated phase into the new period, so switching speed
    // does not jump the brightness.
    phaseMs_ = static_cast<uint32_t>(
        static_cast<uint64_t>(phaseMs_) * periodMs / pulsePeriodMs_);
    pulsePeriodMs_ = periodMs;
  }
  phaseMs_ = (phaseMs_ + dt) % periodMs;

  const uint32_t halfMs = periodMs / 2;
  const uint32_t up = (phaseMs_ < halfMs) ? phaseMs_ : (periodMs - phaseMs_);
  const uint16_t level = levelForPulse(up, halfMs);

  for (uint16_t channel = 0; channel < kChannelCount; ++channel) {
    out[channel] = level;
  }
}

void LedEngine::renderSmolder(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  // Motion is a one-shot trigger on the radar's rising edge; the flash then runs
  // to completion even if the pin drops again.
  if (radarHigh && !prevRadarHigh_) {
    greenTriggerMs_ = nowMs;
  }
  prevRadarHigh_ = radarHigh;

  const uint32_t flickerStep = nowMs / kSmolderFlickerStepMs;
  const uint32_t cycleMs = nowMs - smolderStartMs_;  // wraparound is intentional
  constexpr uint32_t kGreenTotalMs = kGreenAttackMs + kGreenHoldMs + kGreenFadeMs;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const EyeWiring &wiring = kEyes[eye];

    const uint32_t offsetMs =
        static_cast<uint32_t>(kSmolderEyePhasePermille[eye]) * kSmolderPeriodMs /
        1000;
    const uint16_t breath =
        breathePermille((cycleMs % kSmolderPeriodMs) + offsetMs);

    const int32_t orangeBase =
        kSmolderFloorPwm +
        static_cast<int32_t>(kMaxPwm - kSmolderFloorPwm) * breath / 1000;
    const int32_t redBase =
        kSmolderFloorPwm +
        static_cast<int32_t>(kMaxPwm - kSmolderFloorPwm) * (1000 - breath) / 1000;

    uint16_t green = 0;
    uint16_t litPermille = 1000;  // scales O/R down while the green flash fades
    if (greenTriggerMs_ != kNoGreenTrigger) {
      const uint32_t eyeStart =
          greenTriggerMs_ + static_cast<uint32_t>(eye) * kGreenSpreadMs;
      // Eyes that have not started yet wrap to a huge elapsed value and are
      // skipped, which is exactly the spread.
      const uint32_t elapsed = nowMs - eyeStart;
      if (elapsed < kGreenTotalMs) {
        const uint16_t envelope = greenEnvelopePermille(elapsed);
        litPermille = static_cast<uint16_t>(1000 - envelope);
        green = saturatePwm(
            scalePermille(kMaxPwm, envelope) +
            scalePermille(flicker(wiring.green, flickerStep, kGreenFlickerPwm),
                          envelope));
      }
    }

    out[wiring.orange] = saturatePwm(
        scalePermille(orangeBase, litPermille) +
        scalePermille(
            flicker(wiring.orange, flickerStep, kSmolderFlickerPwm), litPermille));
    out[wiring.red] = saturatePwm(
        scalePermille(redBase, litPermille) +
        scalePermille(flicker(wiring.red, flickerStep, kSmolderFlickerPwm),
                      litPermille));
    out[wiring.green] = green;
  }
}

void LedEngine::renderStalker(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - stalkerLastTickMs_;  // wraparound is intentional
  stalkerLastTickMs_ = nowMs;
  stalkerAlert_ = rampToward(stalkerAlert_, dt,
                             radarHigh ? kStalkerPopMs : kStalkerReleaseMs, radarHigh);
  const uint16_t alert = permilleOf(stalkerAlert_);

  const uint32_t scanMs = nowMs - stalkerStartMs_;  // wraparound is intentional
  const uint8_t head = static_cast<uint8_t>((scanMs / kStalkerStepMs) % kEyeCount);
  const uint8_t next = static_cast<uint8_t>((head + 1) % kEyeCount);
  const uint16_t frac = static_cast<uint16_t>(
      (scanMs % kStalkerStepMs) * 1000 / kStalkerStepMs);
  const uint32_t flickerStep = nowMs / kStalkerFlickerStepMs;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    uint16_t weight = 0;
    if (eye == head) {
      weight = static_cast<uint16_t>(1000 - frac);
    } else if (eye == next) {
      weight = frac;
    }

    // The dim scan only reaches the head/next eyes, but the pop is rig-wide:
    // every eye ramps to red on motion. An unlit eye (weight 0, alert 0) stays
    // exactly 0.
    const int32_t glow =
        scalePermille(scalePermille(kStalkerDimPwm, weight),
                      static_cast<uint16_t>(1000 - alert));
    const int32_t pop = scalePermille(kStalkerPopPwm, alert);
    const EyeWiring &w = kEyes[eye];

    const int32_t orange = glow + (glow > 0
        ? flicker(w.orange, flickerStep, kStalkerFlickerPwm) : 0);
    const int32_t red = glow + pop + ((glow + pop) > 0
        ? flicker(w.red, flickerStep, kStalkerFlickerPwm) : 0);
    const int32_t green = glow + (glow > 0
        ? flicker(w.green, flickerStep, kStalkerFlickerPwm) : 0);
    out[w.orange] = saturatePwm(orange);
    out[w.red] = saturatePwm(red);
    out[w.green] = saturatePwm(green);
  }
}

void LedEngine::renderBlink(uint32_t nowMs, uint16_t *out) {
  if (!blinkActive_) {
    if (nowMs < blinkNextMs_) {
      return;  // still in the dark gap
    }
    blinkStartMs_ = nowMs;
    blinkActive_ = true;
    blinkPulses_ =
        (hashRange(blinkSeq_ * 2654435761u + 0x9E3779B9u, kBlinkDoubleOneIn) == 0)
            ? 2
            : 1;
    uint8_t mask = 0;
    for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
      const uint32_t seed =
          blinkSeq_ * 2654435761u + static_cast<uint32_t>(eye) * 40503u + 0x51u;
      if (hashRange(seed, 1000) < kBlinkGroupPermille) {
        mask |= static_cast<uint8_t>(1u << eye);
      }
    }
    if (mask == 0) {  // never a fully dark blink
      const uint8_t forced = static_cast<uint8_t>(
          hashRange(blinkSeq_ * 2654435761u + 0xA5u, kEyeCount));
      mask = static_cast<uint8_t>(1u << forced);
    }
    blinkMask_ = mask;
  }

  const uint32_t period = kBlinkOnMs + kBlinkOffMs;
  const uint32_t lastOnEnd =
      static_cast<uint32_t>(blinkPulses_ - 1) * period + kBlinkOnMs;
  const uint32_t elapsed = nowMs - blinkStartMs_;  // wraparound is intentional
  if (elapsed >= lastOnEnd) {
    blinkActive_ = false;
    blinkNextMs_ = nowMs + kBlinkMinGapMs +
                   hashRange(blinkSeq_ * 2654435761u + 0x77u,
                             kBlinkMaxGapMs - kBlinkMinGapMs);
    ++blinkSeq_;
    return;
  }
  if (elapsed % period >= kBlinkOnMs) {
    return;  // dark gap inside a double blink
  }

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    if (((blinkMask_ >> eye) & 1u) == 0) {
      continue;
    }
    const EyeWiring &w = kEyes[eye];
    const uint8_t channels[kLedsPerEye] = {w.orange, w.red, w.green};
    bool anyLit = false;
    for (uint8_t i = 0; i < kLedsPerEye; ++i) {
      const uint32_t seed = blinkSeq_ * 2654435761u +
                            static_cast<uint32_t>(eye) * 40503u +
                            static_cast<uint32_t>(i) * 0x9E37u + 0x33u;
      if (hashRange(seed, 1000) < kBlinkLedPermille) {
        out[channels[i]] = kBlinkLevelPwm;
        anyLit = true;
      }
    }
    if (!anyLit) {  // every blinking eye shows at least one colour
      const uint8_t pick = static_cast<uint8_t>(
          hashRange(blinkSeq_ * 2654435761u + static_cast<uint32_t>(eye) * 40503u + 0x99u,
                    kLedsPerEye));
      out[channels[pick]] = kBlinkLevelPwm;
    }
  }
}

void LedEngine::renderHeartbeat(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - heartbeatLastTickMs_;  // wraparound is intentional
  heartbeatLastTickMs_ = nowMs;
  agitation_ = rampToward(agitation_, dt,
                          radarHigh ? kHeartbeatEscalateMs : kHeartbeatCoolMs,
                          radarHigh);
  const uint16_t agitation = permilleOf(agitation_);

  const uint32_t period = static_cast<uint32_t>(lerpPermille(
      static_cast<int32_t>(kHeartbeatCalmPeriodMs),
      static_cast<int32_t>(kHeartbeatFastPeriodMs), agitation));
  if (period != beatPeriodMs_) {
    beatPhaseMs_ = static_cast<uint32_t>(
        static_cast<uint64_t>(beatPhaseMs_) * period / beatPeriodMs_);
    beatPeriodMs_ = period;
  }
  beatPhaseMs_ = (beatPhaseMs_ + dt) % period;

  const int32_t beat = scalePermille(kMaxPwm, thumpEnergyPermille(beatPhaseMs_));
  const int32_t red = scalePermille(beat, agitation);
  const int32_t green = scalePermille(beat, static_cast<uint16_t>(1000 - agitation));
  const int32_t orange = scalePermille(red, kHeartbeatOrangePermille);

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const EyeWiring &w = kEyes[eye];
    out[w.red] = saturatePwm(red);
    out[w.green] = saturatePwm(green);
    out[w.orange] = saturatePwm(orange);
  }
}

void LedEngine::renderToxic(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - toxicLastTickMs_;  // wraparound is intentional
  toxicLastTickMs_ = nowMs;
  toxicMotion_ = rampToward(toxicMotion_, dt, kToxicMotionRampMs, radarHigh);
  const uint16_t motion = permilleOf(toxicMotion_);

  const uint32_t stepMs = static_cast<uint32_t>(lerpPermille(
      static_cast<int32_t>(kToxicSparkIdleStepMs),
      static_cast<int32_t>(kToxicSparkFastStepMs), motion));
  const uint32_t total = kEyeCount * 1000u;
  toxicSparkPhase_ = (toxicSparkPhase_ + static_cast<uint32_t>(
      static_cast<uint64_t>(dt) * 1000 / stepMs)) % total;

  const uint16_t sparkPeak =
      static_cast<uint16_t>(lerpPermille(kToxicSparkIdlePwm, kMaxPwm, motion));
  const uint16_t orangeAmp =
      static_cast<uint16_t>(lerpPermille(kToxicOrangeFlickerPwm,
                                         kToxicOrangeFranticPwm, motion));
  const uint32_t greenStep = nowMs / kToxicGreenStepMs;
  const uint32_t flickerStep = nowMs / kToxicFlickerStepMs;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const EyeWiring &w = kEyes[eye];
    out[w.green] = saturatePwm(
        static_cast<int32_t>(kToxicGreenBasePwm) +
        flicker(w.green, greenStep, kToxicGreenFlickerPwm) +
        flicker(w.green, flickerStep, kToxicGreenFlickerPwm / 2));

    const uint32_t behind =
        (toxicSparkPhase_ + total - static_cast<uint32_t>(eye) * 1000u) % total;
    int32_t spark = 0;
    if (behind < kToxicSparkTailPermille) {
      spark = scalePermille(sparkPeak, static_cast<uint16_t>(
          (kToxicSparkTailPermille - behind) * 1000 / kToxicSparkTailPermille));
    }
    out[w.orange] = saturatePwm(spark +
        flicker(w.orange, flickerStep, orangeAmp));
  }
}

void LedEngine::renderHypnotic(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - hypnoLastTickMs_;  // wraparound is intentional
  hypnoLastTickMs_ = nowMs;
  const uint32_t stepMs = radarHigh ? kHypnoFastStepMs : kHypnoSlowStepMs;
  const uint32_t total = kEyeCount * 1000u;
  hypnoPhase_ = (hypnoPhase_ + static_cast<uint32_t>(
      static_cast<uint64_t>(dt) * 1000 / stepMs)) % total;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const uint32_t behind =
        (hypnoPhase_ + total - static_cast<uint32_t>(eye) * 1000u) % total;
    if (behind >= kHypnoTailPermille) {
      continue;
    }
    const uint16_t intensity = static_cast<uint16_t>(
        (kHypnoTailPermille - behind) * 1000 / kHypnoTailPermille);
    const uint16_t level =
        static_cast<uint16_t>(scalePermille(kHypnoHeadPwm, intensity));
    const EyeWiring &w = kEyes[eye];
    switch (eye % kLedsPerEye) {
      case 0:
        out[w.orange] = level;
        break;
      case 1:
        out[w.red] = level;
        break;
      default:
        out[w.green] = level;
        break;
    }
  }
}

}  // namespace ledfx
