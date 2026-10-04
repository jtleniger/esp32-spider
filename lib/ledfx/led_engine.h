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
  // HLK-LD1020 output level; it affects kModeSmolder (green flash), kModePulse
  // (cycle speed), kModeStalker (pop), kModeHeartbeat (agitation), kModeToxic
  // (frantic ramp) and kModeHypnotic (chase speed); kModeSingleChannel and
  // kModeBlink ignore it. Returns true when any channel value changed since
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

  // kModeStalker: scans one dim eye around the rig; a radar level ramps every
  // eye to the bright red pop.
  void renderStalker(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeBlink: lights a random group of eyes for a short burst, occasionally a
  // double blink, then darkness until the next hashed interval.
  void renderBlink(uint32_t nowMs, uint16_t *out);

  // kModeHeartbeat: a lub-dub pulse; calm beats are green, agitated beats red,
  // and motion both raises agitation and shortens the beat.
  void renderHeartbeat(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeToxic: green bubbling base with a travelling orange spark; motion
  // speeds the spark up and makes the flicker frantic.
  void renderToxic(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeHypnotic: a three-colour chase rotating around the eyes; the radar
  // speeds it up.
  void renderHypnotic(uint32_t nowMs, bool radarHigh, uint16_t *out);

  uint16_t frame_[kChannelCount];
  uint16_t mode_;
  uint16_t channel_;        // kModeSingleChannel only: channel to light
  uint32_t phaseMs_;        // kModePulse only: position inside the current cycle
  uint32_t lastTickMs_;     // kModePulse only: previous tick() timestamp
  uint32_t pulsePeriodMs_;  // kModePulse only: period phaseMs_ is measured against
  uint32_t smolderStartMs_;  // kModeSmolder only: phase reference for the crossfade
  uint32_t greenTriggerMs_;  // kModeSmolder only: start of the current green flash
  bool prevRadarHigh_;       // kModeSmolder only: radar rising-edge detector
  uint32_t stalkerAlert_;       // kModeStalker: Q16 ramp toward the pop (0..1<<16)
  uint32_t stalkerLastTickMs_;  // kModeStalker: previous tick() timestamp
  uint32_t stalkerStartMs_;     // kModeStalker: scan phase reference
  bool blinkActive_;            // kModeBlink: a burst is in progress
  uint32_t blinkStartMs_;       // kModeBlink: start of the current burst
  uint32_t blinkNextMs_;        // kModeBlink: earliest time the next burst may start
  uint32_t blinkSeq_;           // kModeBlink: burst counter, seeds the pattern
  uint8_t blinkPulses_;         // kModeBlink: 1 normal, 2 double
  uint8_t blinkMask_;           // kModeBlink: bit set = eye is in this burst
  uint32_t agitation_;          // kModeHeartbeat: Q16 calm(0)..agitated(1<<16)
  uint32_t beatPhaseMs_;        // kModeHeartbeat: position inside the beat
  uint32_t beatPeriodMs_;       // kModeHeartbeat: period beatPhaseMs_ is against
  uint32_t heartbeatLastTickMs_;
  uint32_t toxicMotion_;        // kModeToxic: Q16 idle(0)..frantic(1<<16)
  uint32_t toxicSparkPhase_;    // kModeToxic: spark position, eye-permille units
  uint32_t toxicLastTickMs_;
  uint32_t hypnoPhase_;         // kModeHypnotic: chase position, eye-permille units
  uint32_t hypnoLastTickMs_;
  bool active_;              // false until the first setMode()
};

}  // namespace ledfx
