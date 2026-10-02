#include "led_engine.h"

#include <cstring>

namespace ledfx {
namespace {

static_assert(kPulseHalfSlowMs != 0 && kPulseHalfFastMs != 0 && kPulseLevels != 0,
              "kModePulse needs nonzero tuning");
static_assert(kLastChannel >= kFirstChannel && kChannelCount > kLastChannel,
              "fitted channel range must fit in frame_");

// PWM level at `up` (0..halfMs) through a half cycle, quantised to
// kPulseLevels rungs of equal width.
uint16_t levelForPulse(uint32_t up, uint32_t halfMs) {
  const uint32_t rung = up * kPulseLevels / halfMs;
  return static_cast<uint16_t>(rung * kMaxPwm / kPulseLevels);
}

}  // namespace

LedEngine::LedEngine()
    : frame_{},
      mode_(kModePulse),
      channel_(0),
      phaseMs_(0),
      lastTickMs_(0),
      pulsePeriodMs_(2 * kPulseHalfSlowMs),
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
  active_ = true;
  std::memset(frame_, 0, sizeof(frame_));
  return true;
}

bool LedEngine::tick(uint32_t nowMs, bool radarHigh) {
  uint16_t next[kChannelCount];
  std::memset(next, 0, sizeof(next));

  if (active_) {
    if (mode_ == kModePulse) {
      renderPulse(nowMs, radarHigh, next);
    } else {
      renderSingleChannel(next);
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

  for (uint8_t channel = kFirstChannel; channel <= kLastChannel; ++channel) {
    out[channel] = level;
  }
}

}  // namespace ledfx
