#include "led_engine.h"

#include <cstring>

namespace ledfx {
namespace {

static_assert(kPulseHalfSlowMs != 0 && kPulseHalfFastMs != 0 && kPulseLevels != 0,
              "kModePulse needs nonzero tuning");
static_assert(kSmolderFadeMs != 0 && kSmolderLevels != 0 &&
                  kSmolderFlickerStepMs != 0,
              "kModeSmolder needs nonzero tuning");
static_assert(kSmolderSleepAfterMs < kSmolderSleepDeadlineMs &&
                  kSmolderSleepCheckMs != 0 &&
                  kSmolderSleepMinMs < kSmolderSleepMaxMs &&
                  kSleepFallMinMs < kSleepFallMaxMs &&
                  kSleepOpenMinGapMs < kSleepOpenMaxGapMs &&
                  kSleepDipEndMs < kSleepFlareEndMs &&
                  kSleepFlareEndMs < kSleepGestureMs &&
                  kSleepDipPermille < kSleepFlarePermille &&
                  kSleepFlarePermille < 1000 && kSleepWakeSpreadMs != 0 &&
                  kSmolderWakeMs != 0,
              "kModeSmolder sleep machine needs consistent tuning");
static_assert(kAgitationSweepMinMs < kAgitationSweepMaxMs &&
                  kAgitationSweepPauseMinMs < kAgitationSweepPauseMaxMs &&
                  kAgitationSweepMinMs != 0 &&
                  kAgitationSweepPauseMinMs != 0 &&
                  kAgitationFlickerPwm < kMaxPwm &&
                  kAgitationSweepTailPermille != 0 &&
                  kAgitationSweepTailPermille <= (kEyeCount - 1) * 1000 &&
                  kAgitationDartOnMinMs >= 3 &&
                  kAgitationDartOnMinMs < kAgitationDartOnMaxMs &&
                  kAgitationDartOffMinMs != 0 &&
                  kAgitationDartOffMinMs < kAgitationDartOffMaxMs &&
                  kAgitationDartAttackMs != 0 &&
                  kAgitationDartFadeMs != 0 &&
                  kAgitationRippleExitMs != 0 && kAgitationExitMs != 0 &&
                  kAgitationKindCount == 3,
              "kModeSmolder agitation needs consistent tuning");
static_assert(kEyeCount * kLedsPerEye == kChannelCount,
              "every channel must belong to exactly one eye LED");
static_assert(kStalkerPopMs != 0 && kStalkerReleaseMs != 0 &&
                  kStalkerStepMs != 0 && kStalkerFlickerStepMs != 0,
              "kModeStalker needs nonzero tuning");
static_assert(kBlinkOnMs != 0 && kBlinkOffMs != 0 &&
                  kBlinkMinGapMs < kBlinkMaxGapMs && kBlinkDoubleOneIn != 0 &&
                  kBlinkLevelPwm > kBlinkFlickerPwm && kBlinkFlickerStepMs != 0,
              "kModeBlink needs nonzero tuning");
static_assert(kHeartbeatThumpMs >= 2 && kHeartbeatLevels != 0 &&
                  kHeartbeatCalmPeriodMs > kHeartbeatFastPeriodMs &&
                  kHeartbeatEscalateMs != 0 && kHeartbeatCoolMs != 0 &&
                  kHeartbeatFlickerStepMs != 0 &&
                  kHeartbeatFastPeriodMs >= kHeartbeatDubDelayMs + kHeartbeatThumpMs,
              "kModeHeartbeat needs consistent tuning");
static_assert(kToxicSparkIdleStepMs > kToxicSparkFastStepMs &&
                  kToxicSparkFastStepMs != 0 && kToxicSparkTailPermille != 0 &&
                  kToxicSparkTailPermille <= 1000 &&
                  kToxicGreenStepMs != 0 && kToxicFlickerStepMs != 0 &&
                  kToxicMotionRampMs != 0,
              "kModeToxic needs nonzero tuning");
static_assert(kHypnoSlowStepMs > kHypnoFastStepMs && kHypnoFastStepMs != 0 &&
                  kHypnoTailPermille != 0 &&
                  kHypnoTailPermille <= kEyeCount * 1000 &&
                  kHypnoFlickerStepMs != 0 && kHypnoSurgePeriodMs != 0 &&
                  kHypnoHeadPwm > kHypnoFlickerPwm &&
                  kHypnoSurgePermille < 1000,
              "kModeHypnotic needs consistent tuning");

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

// Adds per-channel flicker to a lit base level, scaling the amplitude by how
// bright the base is: a base at kMaxPwm gets the full `amplitude`, a dimmer one
// proportionally less, and an unlit channel is left at exactly zero.
int32_t withFlicker(int32_t base, uint8_t channel, uint32_t step,
                    uint16_t amplitude) {
  if (base <= 0) {
    return 0;
  }
  uint32_t permille = static_cast<uint32_t>(base) * 1000 / kMaxPwm;
  if (permille > 1000) {
    permille = 1000;
  }
  return base + scalePermille(flicker(channel, step, amplitude),
                              static_cast<uint16_t>(permille));
}

// Triangle wave in [-amp, amp] permille over `periodMs`, driven by absolute
// `nowMs`. Used to modulate the kModeHypnotic chase rate so it never runs at a
// constant speed.
int32_t trianglePermille(uint32_t nowMs, uint32_t periodMs, uint16_t amp) {
  const uint32_t pos = nowMs % periodMs;
  const uint32_t half = periodMs / 2;
  const uint32_t tri = (pos < half) ? pos : (periodMs - pos);  // 0..half
  return static_cast<int32_t>(
             static_cast<uint64_t>(tri) * (2 * amp) / periodMs) -
         amp;
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

// Eyelid-gesture envelope over kSleepGestureMs, in permille of the eye's ember
// brightness: starts open, dips to a slit, flares a little, then goes out.
uint16_t sleepGesturePermille(uint32_t elapsedMs) {
  if (elapsedMs < kSleepDipEndMs) {
    return static_cast<uint16_t>(
        1000 - static_cast<uint64_t>(elapsedMs) * (1000 - kSleepDipPermille) /
                   kSleepDipEndMs);
  }
  if (elapsedMs < kSleepFlareEndMs) {
    return static_cast<uint16_t>(
        kSleepDipPermille +
        static_cast<uint64_t>(elapsedMs - kSleepDipEndMs) *
            (kSleepFlarePermille - kSleepDipPermille) /
            (kSleepFlareEndMs - kSleepDipEndMs));
  }
  if (elapsedMs < kSleepGestureMs) {
    return static_cast<uint16_t>(
        kSleepFlarePermille -
        static_cast<uint64_t>(elapsedMs - kSleepFlareEndMs) *
            kSleepFlarePermille / (kSleepGestureMs - kSleepFlareEndMs));
  }
  return 0;
}

// The eyelid gesture run backwards: dark, up to a slit, a small flare, then
// fully open over kSleepGestureMs. Ends at 1000, the eye's full ember level.
uint16_t wakeGesturePermille(uint32_t elapsedMs) {
  if (elapsedMs < kSleepDipEndMs) {
    return static_cast<uint16_t>(
        static_cast<uint64_t>(elapsedMs) * kSleepDipPermille / kSleepDipEndMs);
  }
  if (elapsedMs < kSleepFlareEndMs) {
    return static_cast<uint16_t>(
        kSleepDipPermille +
        static_cast<uint64_t>(elapsedMs - kSleepDipEndMs) *
            (kSleepFlarePermille - kSleepDipPermille) /
            (kSleepFlareEndMs - kSleepDipEndMs));
  }
  if (elapsedMs < kSleepGestureMs) {
    return static_cast<uint16_t>(
        kSleepFlarePermille +
        static_cast<uint64_t>(elapsedMs - kSleepFlareEndMs) *
            (1000 - kSleepFlarePermille) /
            (kSleepGestureMs - kSleepFlareEndMs));
  }
  return 1000;
}

// How far eye `eye`'s staggered wake has got: 0 while it is still shut, 1000
// once its gesture has finished. Eyes start kSleepWakeSpreadMs apart.
uint16_t wakePermille(uint32_t nowMs, uint32_t wakeStartMs, uint8_t eye) {
  const uint32_t start =
      wakeStartMs + static_cast<uint32_t>(eye) * kSleepWakeSpreadMs;
  if (nowMs < start) {
    return 0;
  }
  const uint32_t elapsed = nowMs - start;
  return elapsed < kSleepGestureMs ? wakeGesturePermille(elapsed) : 1000;
}

// One dart pulse's envelope in permille: fade up, hold, fade back down. Each
// ramp is clamped to a third of the pulse so the briefest flash still eases
// rather than snapping. Callers pass elapsedMs in [0, durMs).
uint16_t dartPulsePermille(uint32_t elapsedMs, uint32_t durMs) {
  const uint32_t ramp = durMs / 3;
  const uint32_t attack = kAgitationDartAttackMs < ramp ? kAgitationDartAttackMs
                                                        : ramp;
  const uint32_t fade =
      kAgitationDartFadeMs < ramp ? kAgitationDartFadeMs : ramp;
  if (elapsedMs < attack) {
    return static_cast<uint16_t>(static_cast<uint64_t>(elapsedMs) * 1000 /
                                 attack);
  }
  if (elapsedMs + fade >= durMs) {
    return static_cast<uint16_t>(static_cast<uint64_t>(durMs - elapsedMs) *
                                 1000 / fade);
  }
  return 1000;
}

// Length of one sweep traverse / rest, hashed from its phase number.
uint32_t sweepTraverseMs(uint32_t seq) {
  return kAgitationSweepMinMs +
         hashRange(seq * 2654435761u + 0x5Au,
                   kAgitationSweepMaxMs - kAgitationSweepMinMs);
}

uint32_t sweepRestMs(uint32_t seq) {
  return kAgitationSweepPauseMinMs +
         hashRange(seq * 2654435761u + 0x93u,
                   kAgitationSweepPauseMaxMs - kAgitationSweepPauseMinMs);
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
      smolderState_(kSmolderAwake),
      smolderStateStartMs_(0),
      waking_(false),
      wakeFromAgitation_(false),
      wakeStartMs_(0),
      smolderSleepSeq_(0),
      sleepHazardStartMs_(0),
      sleepCheckMs_(0),
      sleepWakeMs_(0),
      sleepOpenStartMs_(0),
      sleepNextOpenMs_(0),
      sleepOpenMask_(0),
      sleepOpenSeq_(0),
      agitationKind_(0),
      agitationSeq_(0),
      agitationStartMs_(0),
      lastRadarHighMs_(0),
      sweepTraversing_(true),
      sweepForward_(true),
      sweepPhaseStartMs_(0),
      sweepPhaseEndMs_(0),
      sweepSeq_(0),
      dartOn_{},
      dartPhaseStartMs_{},
      dartPhaseEndMs_{},
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
      toxicCycle_(0),
      toxicSparkRed_(false),
      toxicLastTickMs_(0),
      hypnoPhase_(0),
      hypnoCycle_(0),
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
  smolderState_ = kSmolderAwake;
  smolderStateStartMs_ = nowMs;
  waking_ = false;
  wakeFromAgitation_ = false;
  wakeStartMs_ = nowMs;
  smolderSleepSeq_ = 0;
  sleepHazardStartMs_ = nowMs;
  sleepCheckMs_ = nowMs + kSmolderSleepAfterMs;
  sleepWakeMs_ = 0;
  sleepOpenStartMs_ = 0;
  sleepNextOpenMs_ = 0;
  sleepOpenMask_ = 0;
  sleepOpenSeq_ = 0;
  agitationKind_ = 0;
  agitationSeq_ = 0;
  sweepSeq_ = 0;
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    dartOn_[eye] = false;
    dartPhaseStartMs_[eye] = nowMs;
    dartPhaseEndMs_[eye] = nowMs;
  }
  agitationStartMs_ = nowMs;
  lastRadarHighMs_ = nowMs;
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
  toxicCycle_ = 0;
  toxicSparkRed_ = hashRange(0x9E3779B9u, 2) != 0;
  toxicLastTickMs_ = nowMs;
  hypnoPhase_ = 0;
  hypnoCycle_ = 0;
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
  // Motion always pulls the rig into agitation, from awake or asleep.
  if (radarHigh && smolderState_ != kSmolderAgitated) {
    enterAgitated(nowMs);
    renderSmolderAgitated(nowMs, radarHigh, out);
    return;
  }

  switch (smolderState_) {
    case kSmolderAgitated:
      if (radarHigh) {
        lastRadarHighMs_ = nowMs;  // hold is anchored to the last motion
      } else if (agitationKind_ == kAgitationRipple) {
        // Ripple is motion-driven: the greens hold while the radar is high and
        // fade back into the embers the moment it drops, so it ends on its own
        // schedule rather than the shared quiet hold.
        if (nowMs - lastRadarHighMs_ >= kAgitationRippleExitMs) {
          enterAwakeQuiet(nowMs);
          renderSmolderEmbers(nowMs, out);
          return;
        }
      } else if (nowMs - lastRadarHighMs_ >= kAgitationHoldMs) {
        enterAwake(nowMs, true);
        renderSmolderWaking(nowMs, out);
        return;
      }
      renderSmolderAgitated(nowMs, radarHigh, out);
      return;

    case kSmolderAsleep:
      if (nowMs >= sleepWakeMs_) {
        enterAwake(nowMs, false);
        renderSmolderWaking(nowMs, out);
        return;
      }
      renderSmolderAsleep(nowMs, out);
      return;

    default:  // kSmolderAwake
      {
        const uint32_t elapsed = nowMs - sleepHazardStartMs_;  // wrap intentional
        if (elapsed >= kSmolderSleepDeadlineMs) {
          enterAsleep(nowMs);
          renderSmolderAsleep(nowMs, out);
          return;
        }
        if (nowMs >= sleepCheckMs_) {
          if (elapsed >= kSmolderSleepAfterMs) {
            const uint16_t chance = static_cast<uint16_t>(
                static_cast<uint64_t>(elapsed - kSmolderSleepAfterMs) * 1000 /
                (kSmolderSleepDeadlineMs - kSmolderSleepAfterMs));
            if (hashRange(sleepHazardStartMs_ * 2654435761u + nowMs, 1000) <
                chance) {
              enterAsleep(nowMs);
              renderSmolderAsleep(nowMs, out);
              return;
            }
          }
          sleepCheckMs_ = nowMs + kSmolderSleepCheckMs;
        }
        if (waking_) {
          renderSmolderWaking(nowMs, out);
        } else {
          renderSmolderEmbers(nowMs, out);
        }
      }
      return;
  }
}

void LedEngine::enterAwake(uint32_t nowMs, bool fromAgitation) {
  enterAwakeQuiet(nowMs);
  waking_ = true;
  wakeFromAgitation_ = fromAgitation;
  wakeStartMs_ = nowMs;
}

void LedEngine::enterAwakeQuiet(uint32_t nowMs) {
  smolderState_ = kSmolderAwake;
  smolderStateStartMs_ = nowMs;
  waking_ = false;
  wakeFromAgitation_ = false;
  sleepHazardStartMs_ = nowMs;
  sleepCheckMs_ = nowMs + kSmolderSleepAfterMs;
}

void LedEngine::enterAsleep(uint32_t nowMs) {
  smolderState_ = kSmolderAsleep;
  smolderStateStartMs_ = nowMs;
  waking_ = false;
  ++smolderSleepSeq_;
  const uint32_t seed = smolderSleepSeq_ * 2654435761u + 0x9Eu;
  sleepWakeMs_ = nowMs + kSmolderSleepMinMs +
                 hashRange(seed, kSmolderSleepMaxMs - kSmolderSleepMinMs);
  sleepOpenStartMs_ = 0;
  sleepOpenMask_ = 0;
  sleepNextOpenMs_ =
      nowMs + kSleepOpenMinGapMs +
      hashRange(seed + 0x11u, kSleepOpenMaxGapMs - kSleepOpenMinGapMs);
}

void LedEngine::enterAgitated(uint32_t nowMs) {
  smolderState_ = kSmolderAgitated;
  smolderStateStartMs_ = nowMs;
  waking_ = false;
  agitationKind_ = static_cast<uint8_t>(
      hashRange(agitationSeq_ * 2654435761u + 0x6Bu, kAgitationKindCount));
  ++agitationSeq_;
  agitationStartMs_ = nowMs;
  lastRadarHighMs_ = nowMs;
  sweepTraversing_ = true;
  sweepForward_ = true;
  sweepPhaseStartMs_ = nowMs;
  sweepPhaseEndMs_ = nowMs + sweepTraverseMs(sweepSeq_);
  ++sweepSeq_;

  // Seed each eye's independent dart clock, staggered so the eyes never step
  // together: roughly half start already partway into a lit pulse, the rest
  // start dark for a random gap.
  bool anyOn = false;
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const uint32_t seed =
        nowMs * 2654435761u + static_cast<uint32_t>(eye) * 40503u + 0x1Fu;
    if (hashRange(seed + 0x9Bu, 2) != 0) {
      const uint32_t onMs =
          kAgitationDartOnMinMs +
          hashRange(seed, kAgitationDartOnMaxMs - kAgitationDartOnMinMs);
      const uint32_t into = hashRange(seed + 0x33u, onMs);
      dartOn_[eye] = true;
      dartPhaseStartMs_[eye] = nowMs - into;
      dartPhaseEndMs_[eye] = dartPhaseStartMs_[eye] + onMs;
      anyOn = true;
    } else {
      dartOn_[eye] = false;
      dartPhaseStartMs_[eye] = nowMs;
      dartPhaseEndMs_[eye] =
          nowMs + kAgitationDartOffMinMs +
          hashRange(seed + 0x57u,
                    kAgitationDartOffMaxMs - kAgitationDartOffMinMs);
    }
  }
  if (!anyOn) {
    // A darting face must never open all-dark: put one eye mid-pulse.
    const uint8_t eye = static_cast<uint8_t>(
        hashRange(nowMs * 2654435761u + 0x2Fu, kEyeCount));
    dartOn_[eye] = true;
    dartPhaseStartMs_[eye] = nowMs - kAgitationDartOnMaxMs / 2;
    dartPhaseEndMs_[eye] = dartPhaseStartMs_[eye] + kAgitationDartOnMaxMs;
  }
}

void LedEngine::emberLevels(uint8_t eye, uint32_t nowMs, uint16_t &orange,
                            uint16_t &red) {
  const EyeWiring &wiring = kEyes[eye];
  const uint32_t flickerStep = nowMs / kSmolderFlickerStepMs;
  const uint32_t offsetMs =
      static_cast<uint32_t>(kSmolderEyePhasePermille[eye]) * kSmolderPeriodMs /
      1000;
  const uint16_t breath = breathePermille(
      (nowMs - smolderStartMs_) % kSmolderPeriodMs + offsetMs);
  orange = saturatePwm(withFlicker(
      scalePermille(kMaxPwm, breath), wiring.orange, flickerStep,
      kSmolderFlickerPwm));
  red = saturatePwm(withFlicker(scalePermille(kMaxPwm, 1000 - breath),
                                wiring.red, flickerStep, kSmolderFlickerPwm));
}

void LedEngine::renderSmolderEmbers(uint32_t nowMs, uint16_t *out) {
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    uint16_t orange = 0;
    uint16_t red = 0;
    emberLevels(eye, nowMs, orange, red);
    out[kEyes[eye].orange] = orange;
    out[kEyes[eye].red] = red;
  }
}

void LedEngine::renderSmolderAsleep(uint32_t nowMs, uint16_t *out) {
  // Advance the occasional random half-open: one event at a time.
  if (sleepOpenMask_ != 0 && nowMs - sleepOpenStartMs_ >= kSleepGestureMs) {
    sleepOpenMask_ = 0;
  }
  if (sleepOpenMask_ == 0 && nowMs >= sleepNextOpenMs_) {
    const uint32_t seed = sleepOpenSeq_ * 2654435761u + 0x33u;
    uint8_t mask = 0;
    for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
      if (hashRange(seed + eye * 40503u, 1000) < kSleepOpenGroupPermille) {
        mask |= static_cast<uint8_t>(1u << eye);
      }
    }
    if (mask == 0) {  // never a fully dark half-open
      mask = static_cast<uint8_t>(
          1u << hashRange(seed + 0xA5u, kEyeCount));
    }
    sleepOpenStartMs_ = nowMs;
    sleepOpenMask_ = mask;
    ++sleepOpenSeq_;
    sleepNextOpenMs_ =
        nowMs + kSleepOpenMinGapMs +
        hashRange(seed + 0x77u, kSleepOpenMaxGapMs - kSleepOpenMinGapMs);
  }

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    // Falling asleep: this eye's eyelid closes at a random point in the spread.
    const uint32_t closeStart =
        smolderStateStartMs_ + kSleepFallMinMs +
        hashRange(smolderSleepSeq_ * 2654435761u + eye * 40503u + 0x51u,
                  kSleepFallMaxMs - kSleepFallMinMs);
    uint32_t elapsed = 0;
    bool gesturing = false;
    if (nowMs >= closeStart && nowMs - closeStart < kSleepGestureMs) {
      elapsed = nowMs - closeStart;
      gesturing = true;
    } else if (((sleepOpenMask_ >> eye) & 1u) != 0 &&
               nowMs - sleepOpenStartMs_ < kSleepGestureMs) {
      elapsed = nowMs - sleepOpenStartMs_;
      gesturing = true;
    }
    uint16_t orange = 0;
    uint16_t red = 0;
    if (!gesturing) {
      if (nowMs < closeStart) {  // not yet shut: full embers
        emberLevels(eye, nowMs, orange, red);
        out[kEyes[eye].orange] = orange;
        out[kEyes[eye].red] = red;
      }
      continue;  // after its close: dark
    }
    const uint16_t env = sleepGesturePermille(elapsed);
    emberLevels(eye, nowMs, orange, red);
    out[kEyes[eye].orange] = saturatePwm(scalePermille(orange, env));
    out[kEyes[eye].red] = saturatePwm(scalePermille(red, env));
  }
}

void LedEngine::renderSmolderAgitated(uint32_t nowMs, bool radarHigh,
                                      uint16_t *out) {
  if (agitationKind_ == kAgitationRipple) {
    renderSmolderRipple(nowMs, radarHigh, out);
    return;
  }
  uint16_t green[kEyeCount] = {};
  agitationFrame(nowMs, green);
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    out[kEyes[eye].green] = green[eye];
  }
}

void LedEngine::renderSmolderRipple(uint32_t nowMs, bool radarHigh,
                                    uint16_t *out) {
  const uint32_t sinceStart = nowMs - agitationStartMs_;  // wrap intentional
  const uint32_t sinceMotion = nowMs - lastRadarHighMs_;  // wrap intentional
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const uint32_t start = static_cast<uint32_t>(eye) * kGreenSpreadMs;
    uint16_t envelope = 0;
    if (sinceStart >= start) {
      const uint32_t age = sinceStart - start;
      if (age < kGreenAttackMs) {
        envelope = static_cast<uint16_t>(static_cast<uint64_t>(age) * 1000 /
                                         kGreenAttackMs);
      } else if (radarHigh || sinceMotion <= start) {
        envelope = 1000;  // holding: still moving, or this eye's stagger looms
      } else {
        // Motion has stopped: fade out kGreenFadeMs, staggered like the fade-in.
        const uint32_t fadeAge = sinceMotion - start;
        envelope = fadeAge >= kGreenFadeMs
                       ? 0
                       : static_cast<uint16_t>(
                             1000 - static_cast<uint64_t>(fadeAge) * 1000 /
                                        kGreenFadeMs);
      }
    }
    // The embers sit behind the flash: they dim as a green rises and return as
    // it fades, exactly like the pre-sleep-machine motion flash.
    uint16_t orange = 0;
    uint16_t red = 0;
    emberLevels(eye, nowMs, orange, red);
    const uint16_t lit = 1000 - envelope;
    out[kEyes[eye].orange] = saturatePwm(scalePermille(orange, lit));
    out[kEyes[eye].red] = saturatePwm(scalePermille(red, lit));
    out[kEyes[eye].green] = saturatePwm(withFlicker(
        scalePermille(kMaxPwm, envelope), kEyes[eye].green,
        nowMs / kSmolderFlickerStepMs, kGreenFlickerPwm));
  }
}

void LedEngine::renderSmolderWaking(uint32_t nowMs, uint16_t *out) {
  if (wakeFromAgitation_) {
    // Whole-face crossfade: the agitation keeps running while it fades out and
    // every eye's ember rises underneath it.
    const uint32_t elapsed = nowMs - wakeStartMs_;  // wrap intentional
    const uint16_t emberPermille =
        elapsed >= kAgitationExitMs
            ? 1000
            : static_cast<uint16_t>(elapsed * 1000 / kAgitationExitMs);
    uint16_t green[kEyeCount] = {};
    agitationFrame(nowMs, green);
    for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
      uint16_t orange = 0;
      uint16_t red = 0;
      emberLevels(eye, nowMs, orange, red);
      out[kEyes[eye].orange] = saturatePwm(scalePermille(orange, emberPermille));
      out[kEyes[eye].red] = saturatePwm(scalePermille(red, emberPermille));
      out[kEyes[eye].green] =
          saturatePwm(scalePermille(green[eye], 1000 - emberPermille));
    }
    if (emberPermille >= 1000) {
      waking_ = false;
    }
    return;
  }

  // Nap wake: the eyes open one at a time, kSleepWakeSpreadMs apart.
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const uint16_t open = wakePermille(nowMs, wakeStartMs_, eye);
    uint16_t orange = 0;
    uint16_t red = 0;
    emberLevels(eye, nowMs, orange, red);
    out[kEyes[eye].orange] = saturatePwm(scalePermille(orange, open));
    out[kEyes[eye].red] = saturatePwm(scalePermille(red, open));
  }
  if (nowMs - wakeStartMs_ >= kSmolderWakeMs) {
    waking_ = false;
  }
}

void LedEngine::agitationFrame(uint32_t nowMs, uint16_t *green) {
  const uint32_t flickerStep = nowMs / kAgitationFlickerStepMs;

  if (agitationKind_ == kAgitationSweep) {
    // A bright eye crosses the face, rests on each end, then crosses back, with
    // a trailing glow behind the head.
    advanceSweep(nowMs);
    const uint32_t span = (kEyeCount - 1) * 1000u;
    uint32_t pos = 0;  // head position in eye-permille
    if (sweepTraversing_) {
      const uint32_t dur = sweepPhaseEndMs_ - sweepPhaseStartMs_;
      const uint32_t t = nowMs - sweepPhaseStartMs_;
      pos = static_cast<uint32_t>(static_cast<uint64_t>(t) * span / dur);
      if (!sweepForward_) {
        pos = span - pos;
      }
    } else {
      pos = sweepForward_ ? span : 0;
    }
    for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
      const int32_t eyePos = static_cast<int32_t>(eye) * 1000;
      const int32_t tail = static_cast<int32_t>(kAgitationSweepTailPermille);
      const int32_t behind = sweepForward_ ? static_cast<int32_t>(pos) - eyePos
                                           : eyePos - static_cast<int32_t>(pos);
      if (behind < 0 || behind >= tail) {
        continue;
      }
      // Quadratic falloff: the head stays bright and the eyes behind it dim
      // away smoothly into the tail instead of all sharing a linear ramp.
      const int32_t remaining = tail - behind;
      const uint16_t intensity = static_cast<uint16_t>(
          static_cast<uint64_t>(remaining) * remaining * 1000 /
          (static_cast<uint32_t>(kAgitationSweepTailPermille) * kAgitationSweepTailPermille));
      const uint8_t channel = kEyes[eye].green;
      green[eye] = saturatePwm(withFlicker(scalePermille(kMaxPwm, intensity),
                                           channel, flickerStep,
                                           kAgitationFlickerPwm));
    }
    return;
  }

  if (agitationKind_ == kAgitationDart) {
    // Every eye runs its own on/off clock, so the lit set turns over
    // independently instead of stepping together.
    advanceDart(nowMs);
    for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
      if (!dartOn_[eye]) {
        continue;
      }
      const uint32_t elapsed = nowMs - dartPhaseStartMs_[eye];
      const uint32_t dur = dartPhaseEndMs_[eye] - dartPhaseStartMs_[eye];
      const uint16_t envelope = dartPulsePermille(elapsed, dur);
      const uint8_t channel = kEyes[eye].green;
      green[eye] = saturatePwm(withFlicker(scalePermille(kMaxPwm, envelope),
                                           channel, flickerStep,
                                           kAgitationFlickerPwm));
    }
    return;
  }
}

void LedEngine::advanceSweep(uint32_t nowMs) {
  while (nowMs >= sweepPhaseEndMs_) {
    sweepPhaseStartMs_ = sweepPhaseEndMs_;
    const uint32_t seq = sweepSeq_;
    ++sweepSeq_;
    if (sweepTraversing_) {
      sweepTraversing_ = false;
      sweepPhaseEndMs_ = sweepPhaseStartMs_ + sweepRestMs(seq);
    } else {
      sweepTraversing_ = true;
      sweepForward_ = !sweepForward_;
      sweepPhaseEndMs_ = sweepPhaseStartMs_ + sweepTraverseMs(seq);
    }
  }
}

// Advances every eye's dart clock to nowMs, flipping eyes between their lit and
// dark phases. Each phase length is hashed from the phase's own start time, so
// an eye's pulses are indefinite, unpredictable and independent of its
// neighbours'. Phase lengths are at least kAgitationDartOnMinMs /
// kAgitationDartOffMinMs, so the catch-up loop is bounded.
void LedEngine::advanceDart(uint32_t nowMs) {
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    while (nowMs >= dartPhaseEndMs_[eye]) {
      const uint32_t start = dartPhaseEndMs_[eye];
      const uint32_t seed =
          start * 2654435761u + static_cast<uint32_t>(eye) * 40503u + 0x7Au;
      dartPhaseStartMs_[eye] = start;
      if (dartOn_[eye]) {
        dartOn_[eye] = false;
        dartPhaseEndMs_[eye] =
            start + kAgitationDartOffMinMs +
            hashRange(seed, kAgitationDartOffMaxMs - kAgitationDartOffMinMs);
      } else {
        dartOn_[eye] = true;
        dartPhaseEndMs_[eye] =
            start + kAgitationDartOnMinMs +
            hashRange(seed, kAgitationDartOnMaxMs - kAgitationDartOnMinMs);
      }
    }
  }
}

void LedEngine::renderStalker(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - stalkerLastTickMs_;  // wraparound is intentional
  stalkerLastTickMs_ = nowMs;
  stalkerAlert_ = rampToward(stalkerAlert_, dt,
                             radarHigh ? kStalkerPopMs : kStalkerReleaseMs, radarHigh);
  const uint16_t alert = permilleOf(stalkerAlert_);

  const uint32_t scanMs = nowMs - stalkerStartMs_;  // wraparound is intentional
  const uint32_t pass =
      scanMs / (static_cast<uint32_t>(kStalkerStepMs) * kEyeCount);
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
    const EyeWiring &w = kEyes[eye];

    int32_t orange = 0;
    int32_t red = 0;
    int32_t green = 0;
    if (weight > 0) {
      // Each eye scans in one pseudo-random colour; the pass index reshuffles
      // the palette every lap so the rig does not settle into a pattern.
      const uint8_t slot = static_cast<uint8_t>(hashRange(
          static_cast<uint32_t>(eye) * 2654435761u + pass * 40503u + 0x5Au,
          kLedsPerEye));
      const uint8_t channel =
          slot == 0 ? w.orange : (slot == 1 ? w.red : w.green);
      int32_t glow = scalePermille(scalePermille(kStalkerDimPwm, weight),
                                   static_cast<uint16_t>(1000 - alert));
      if (slot == 2) {  // green reads brighter, so trim it
        glow = scalePermille(glow, kStalkerGreenPermille);
      }
      glow = withFlicker(glow, channel, flickerStep, kStalkerFlickerPwm);
      if (slot == 0) {
        orange = glow;
      } else if (slot == 1) {
        red = glow;
      } else {
        green = glow;
      }
    }

    // The motion wake-up is rig-wide red, and it stutters.
    if (alert > 0) {
      red = withFlicker(red + scalePermille(kStalkerPopPwm, alert), w.red,
                        flickerStep, kStalkerPopFlickerPwm);
    }

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

  const uint32_t flickerStep = nowMs / kBlinkFlickerStepMs;
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    if (((blinkMask_ >> eye) & 1u) == 0) {
      continue;
    }
    const EyeWiring &w = kEyes[eye];
    const uint8_t channels[kLedsPerEye] = {w.orange, w.red, w.green};
    // One pseudo-random colour per eye per blink, sputtering on the way.
    const uint8_t pick = static_cast<uint8_t>(hashRange(
        blinkSeq_ * 2654435761u + static_cast<uint32_t>(eye) * 40503u + 0x33u,
        kLedsPerEye));
    const uint8_t channel = channels[pick];
    out[channel] = saturatePwm(
        withFlicker(kBlinkLevelPwm, channel, flickerStep, kBlinkFlickerPwm));
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
  const int32_t green = scalePermille(
      scalePermille(beat, static_cast<uint16_t>(1000 - agitation)),
      kHeartbeatGreenPermille);
  const int32_t orange = scalePermille(red, kHeartbeatOrangePermille);
  const uint32_t flickerStep = nowMs / kHeartbeatFlickerStepMs;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const EyeWiring &w = kEyes[eye];
    out[w.red] = saturatePwm(
        withFlicker(red, w.red, flickerStep, kHeartbeatFlickerPwm));
    out[w.green] = saturatePwm(
        withFlicker(green, w.green, flickerStep, kHeartbeatFlickerPwm));
    out[w.orange] = saturatePwm(
        withFlicker(orange, w.orange, flickerStep, kHeartbeatFlickerPwm));
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
  const uint32_t before = toxicSparkPhase_;
  toxicSparkPhase_ = (toxicSparkPhase_ + static_cast<uint32_t>(
      static_cast<uint64_t>(dt) * 1000 / stepMs)) % total;
  if (toxicSparkPhase_ < before) {
    // A new lap: pick the spark's colour for this revolution.
    ++toxicCycle_;
    toxicSparkRed_ = hashRange(toxicCycle_ * 2654435761u + 0x7u, 2) != 0;
  }

  const uint16_t sparkPeak =
      static_cast<uint16_t>(lerpPermille(kToxicSparkIdlePwm, kMaxPwm, motion));
  const uint16_t sparkAmp =
      static_cast<uint16_t>(lerpPermille(kToxicSparkFlickerPwm,
                                         kToxicSparkFranticPwm, motion));
  const uint32_t greenStep = nowMs / kToxicGreenStepMs;
  const uint32_t flickerStep = nowMs / kToxicFlickerStepMs;

  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const EyeWiring &w = kEyes[eye];
    int32_t green = kToxicGreenBasePwm;
    green = withFlicker(green, w.green, greenStep, kToxicGreenFlickerPwm);
    green = withFlicker(green, w.green, flickerStep, kToxicGreenFlickerPwm / 2);
    out[w.green] = saturatePwm(green);

    // The tail is shorter than one eye spacing, so only the head eye carries
    // the spark - the glow is isolated rather than a smeared trail.
    const uint32_t behind =
        (toxicSparkPhase_ + total - static_cast<uint32_t>(eye) * 1000u) % total;
    if (behind >= kToxicSparkTailPermille) {
      continue;
    }
    const int32_t spark = scalePermille(sparkPeak, static_cast<uint16_t>(
        (kToxicSparkTailPermille - behind) * 1000 / kToxicSparkTailPermille));
    const uint8_t channel = toxicSparkRed_ ? w.red : w.orange;
    out[channel] =
        saturatePwm(withFlicker(spark, channel, flickerStep, sparkAmp));
  }
}

void LedEngine::renderHypnotic(uint32_t nowMs, bool radarHigh, uint16_t *out) {
  const uint32_t dt = nowMs - hypnoLastTickMs_;  // wraparound is intentional
  hypnoLastTickMs_ = nowMs;
  const uint32_t stepMs = radarHigh ? kHypnoFastStepMs : kHypnoSlowStepMs;
  const uint32_t total = kEyeCount * 1000u;

  // Mix the base rate with a triangle wave, so the chase surges and eases
  // instead of marching at one steady speed.
  const int32_t surge =
      trianglePermille(nowMs, kHypnoSurgePeriodMs, kHypnoSurgePermille);
  const uint32_t rate = static_cast<uint32_t>(1000 + surge);
  const uint32_t before = hypnoPhase_;
  const uint32_t advance = static_cast<uint32_t>(
      static_cast<uint64_t>(dt) * 1000 / stepMs * rate / 1000);
  hypnoPhase_ = (hypnoPhase_ + advance) % total;
  if (hypnoPhase_ < before) {
    ++hypnoCycle_;  // a new lap: reshuffle the colour palette
  }

  const uint32_t flickerStep = nowMs / kHypnoFlickerStepMs;
  for (uint8_t eye = 0; eye < kEyeCount; ++eye) {
    const uint32_t behind =
        (hypnoPhase_ + total - static_cast<uint32_t>(eye) * 1000u) % total;
    if (behind >= kHypnoTailPermille) {
      continue;
    }
    const uint16_t intensity = static_cast<uint16_t>(
        (kHypnoTailPermille - behind) * 1000 / kHypnoTailPermille);
    const uint8_t slot = static_cast<uint8_t>(hashRange(
        static_cast<uint32_t>(eye) * 2654435761u + hypnoCycle_ * 40503u + 0x2Bu,
        kLedsPerEye));
    const EyeWiring &w = kEyes[eye];
    const uint8_t channel =
        slot == 0 ? w.orange : (slot == 1 ? w.red : w.green);
    const int32_t level = scalePermille(kHypnoHeadPwm, intensity);
    out[channel] = saturatePwm(
        withFlicker(level, channel, flickerStep, kHypnoFlickerPwm));
  }
}

}  // namespace ledfx
