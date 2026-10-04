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

// Deterministic per-channel pseudo-random value in [-amplitude, +amplitude] that
// changes once per flicker step. A cheap integer hash keeps the engine off
// rand() (the host tests must not depend on libc randomness) and allocation-free.
int32_t flicker(uint8_t channel, uint32_t step, uint16_t amplitude) {
  uint32_t x = static_cast<uint32_t>(channel) * 2654435761u + step * 40503u +
               0x9E3779B9u;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  x *= 3266489917u;
  x ^= x >> 16;
  const uint32_t span = 2u * amplitude + 1u;
  return static_cast<int32_t>(x % span) - amplitude;
}

int32_t scalePermille(int32_t value, uint16_t permille) {
  return static_cast<int32_t>(static_cast<int64_t>(value) * permille / 1000);
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

}  // namespace ledfx
