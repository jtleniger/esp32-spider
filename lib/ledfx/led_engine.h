#pragma once

#include <cstdint>

#include "led_layout.h"
#include "modes.h"

namespace ledfx {

// Pure effect engine. No Arduino/hardware dependency: every state change is
// driven by the caller-supplied millisecond timestamp and radar level, so it is
// host-testable. Channel values are raw 12-bit PWM (0..kMaxPwm); there is no RGB
// notion here, matching the single-colour LEDs on the board.
class LedEngine {
 public:
  LedEngine();

  // Restarts `mode` at `nowMs`. Returns false and changes nothing for any
  // value >= kModeCount. Re-issuing the current mode restarts it.
  bool setMode(uint16_t mode, uint32_t nowMs);

  uint16_t mode() const { return mode_; }

  // True before the first setMode() and once a kModeSequentialFade run is over.
  // kModePulse never finishes.
  bool finished() const { return finished_; }

  // Recomputes every channel for time `nowMs`. `radarHigh` is the raw
  // HLK-LD1020 output level; it only affects kModePulse. Returns true when any
  // channel value changed since the previous tick, i.e. when the frame must be
  // pushed.
  bool tick(uint32_t nowMs, bool radarHigh);

  // kChannelCount raw 12-bit PWM values (0..kMaxPwm), index == channel number.
  const uint16_t *frame() const { return frame_; }

 private:
  // kModeSequentialFade: fills `out` (kChannelCount entries) at `elapsedMs`
  // after the mode started; sets finished_ when the run is over.
  void renderSequentialFade(uint32_t elapsedMs, uint16_t *out);

  // kModePulse: advances the cycle phase by the time since the previous tick
  // and fills `out` with the resulting level. Never sets finished_.
  void renderPulse(uint32_t nowMs, bool radarHigh, uint16_t *out);

  uint16_t frame_[kChannelCount];
  uint16_t mode_;
  uint32_t startMs_;        // kModeSequentialFade only
  uint32_t phaseMs_;        // kModePulse only: position inside the current cycle
  uint32_t lastTickMs_;     // kModePulse only: previous tick() timestamp
  uint32_t pulsePeriodMs_;  // kModePulse only: period phaseMs_ is measured against
  bool finished_;
};

}  // namespace ledfx
