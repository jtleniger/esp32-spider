#pragma once

#include <cstdint>

#include "led_layout.h"
#include "modes.h"

namespace ledfx {

// Pure effect engine. No Arduino/hardware dependency: every state change is
// driven by the caller-supplied millisecond timestamp and radar level, so it is
// host-testable. Channel values are raw 12-bit PWM (0..kMaxPwm); there is no RGB
// notion here, matching the single-colour LEDs on the board. Which channel and
// eye each LED belongs to comes from kEyes in led_layout.h.
class LedEngine {
 public:
  LedEngine();

  // Starts `mode` at `nowMs` and selects `channel` for kModeSingleChannel.
  // Returns false and changes nothing for any mode >= kModeCount and for a
  // kModeSingleChannel request whose channel is >= kChannelCount; `channel` is
  // ignored by the other modes. Re-issuing the current mode restarts it.
  bool setMode(uint16_t mode, uint16_t channel, uint32_t nowMs);

  uint16_t mode() const { return mode_; }

  // Recomputes every channel for time `nowMs`. `radarHigh` is the raw
  // HLK-LD1020 output level; it only affects kModeSmolder (green flash) and
  // kModePulse (cycle speed). Returns true when any channel value changed since
  // the previous tick, i.e. when the frame must be pushed. Until the first
  // setMode() the engine stays all-off and returns false.
  bool tick(uint32_t nowMs, bool radarHigh);

  // kChannelCount raw 12-bit PWM values (0..kMaxPwm), index == channel number.
  const uint16_t *frame() const { return frame_; }

 private:
  // kModeSingleChannel: fills `out` with the selected channel at full PWM.
  void renderSingleChannel(uint16_t *out);

  // kModePulse: advances the cycle phase by the time since the previous tick
  // and fills `out` with the resulting level.
  void renderPulse(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeSmolder: renders the per-eye orange/red crossfade with flicker, and the
  // green motion flash, into `out`. `radarHigh` starts a flash on its rising edge.
  void renderSmolder(uint32_t nowMs, bool radarHigh, uint16_t *out);

  uint16_t frame_[kChannelCount];
  uint16_t mode_;
  uint16_t channel_;        // kModeSingleChannel only: channel to light
  uint32_t phaseMs_;        // kModePulse only: position inside the current cycle
  uint32_t lastTickMs_;     // kModePulse only: previous tick() timestamp
  uint32_t pulsePeriodMs_;  // kModePulse only: period phaseMs_ is measured against
  uint32_t smolderStartMs_;  // kModeSmolder only: phase reference for the crossfade
  uint32_t greenTriggerMs_;  // kModeSmolder only: start of the current green flash
  bool prevRadarHigh_;       // kModeSmolder only: radar rising-edge detector
  bool active_;              // false until the first setMode()
};

}  // namespace ledfx
