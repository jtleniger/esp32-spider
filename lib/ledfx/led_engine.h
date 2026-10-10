#pragma once

#include <cstdint>

#include "led_layout.h"
#include "modes.h"

namespace ledfx {

// kModeSmolder (mode 0) sleep/wake sub-state, observable for tests.
enum SmolderState : uint8_t {
  kSmolderAwake = 0,     // embers: the orange/red crossfade
  kSmolderAsleep = 1,    // dark, with occasional eye-opening gestures
  kSmolderAgitated = 2,  // one green agitation animation
};

// Green-only agitation animations kModeSmolder picks at random.
enum AgitationKind : uint8_t {
  kAgitationRipple = 0,  // original eye-by-eye green spread, looping
  kAgitationSweep = 1,   // bright green eye sweeping left<->right
  kAgitationDart = 2,    // random single eyes snapping green
};
constexpr uint8_t kAgitationKindCount = 3;

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

  SmolderState smolderState() const { return smolderState_; }
  uint8_t agitationKind() const { return agitationKind_; }

  // Recomputes every channel for time `nowMs`. `radarHigh` is the raw
  // HLK-LD1020 output level; it affects kModeSmolder (sleep/wake machine and
  // green agitation), kModePulse (cycle speed), kModeStalker (pop),
  // kModeHeartbeat (agitation), kModeToxic (frantic ramp) and kModeHypnotic
  // (chase speed); kModeSingleChannel and kModeBlink ignore it. Returns true
  // when any channel value changed since the previous tick, i.e. when the frame
  // must be pushed. Until the first setMode() the engine stays all-off and
  // returns false.
  bool tick(uint32_t nowMs, bool radarHigh);

  // kChannelCount raw 12-bit PWM values (0..kMaxPwm), index == channel number.
  const uint16_t *frame() const { return frame_; }

 private:
  // kModeSingleChannel: fills `out` with the selected channel at full PWM.
  void renderSingleChannel(uint16_t *out);

  // kModePulse: advances the cycle phase by the time since the previous tick
  // and fills `out` with the resulting level.
  void renderPulse(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeSmolder: dispatches on smolderState_, advancing the state machine on
  // radar motion, the sleep hazard, the nap timer and the agitation hold.
  void renderSmolder(uint32_t nowMs, bool radarHigh, uint16_t *out);
  // Renders the orange/red ember crossfade (no green) for every eye.
  void renderSmolderEmbers(uint32_t nowMs, uint16_t *out);
  // Renders one eye's ember levels from the shared crossfade (no flicker step
  // recompute by callers).
  void emberLevels(uint8_t eye, uint32_t nowMs, uint16_t &orange, uint16_t &red);
  // Renders the asleep state: progressive eyelid closures plus half-open events.
  void renderSmolderAsleep(uint32_t nowMs, uint16_t *out);
  // Renders the green-only agitation animation agitationKind_.
  void renderSmolderAgitated(uint32_t nowMs, uint16_t *out);
  void enterAwake(uint32_t nowMs);
  void enterAsleep(uint32_t nowMs);
  void enterAgitated(uint32_t nowMs);

  // kModeStalker: scans one dim eye (in a random colour) around the rig; a
  // radar level ramps every eye to the stuttering bright red pop.
  void renderStalker(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeBlink: lights a random group of eyes in single random colours for a
  // sputtering burst, occasionally a double blink, then darkness until the next
  // hashed interval.
  void renderBlink(uint32_t nowMs, uint16_t *out);

  // kModeHeartbeat: a flickery lub-dub pulse; calm beats are green, agitated
  // beats red, and motion both raises agitation and shortens the beat.
  void renderHeartbeat(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeToxic: dim green bubbling base with an isolated spark that is orange or
  // red (random per revolution); motion speeds the spark up and makes the
  // flicker frantic.
  void renderToxic(uint32_t nowMs, bool radarHigh, uint16_t *out);

  // kModeHypnotic: a random-coloured chase rotating around the eyes with a
  // periodically modulated rate and a flicker; the radar speeds it up.
  void renderHypnotic(uint32_t nowMs, bool radarHigh, uint16_t *out);

  uint16_t frame_[kChannelCount];
  uint16_t mode_;
  uint16_t channel_;        // kModeSingleChannel only: channel to light
  uint32_t phaseMs_;        // kModePulse only: position inside the current cycle
  uint32_t lastTickMs_;     // kModePulse only: previous tick() timestamp
  uint32_t pulsePeriodMs_;  // kModePulse only: period phaseMs_ is measured against
  uint32_t smolderStartMs_;  // kModeSmolder only: phase reference for the crossfade
  SmolderState smolderState_;    // kModeSmolder: current sleep/wake sub-state
  uint32_t smolderStateStartMs_; // kModeSmolder: when smolderState_ was entered
  uint32_t smolderSleepSeq_;     // kModeSmolder: sleep-episode counter (seeds maths)
  uint32_t sleepHazardStartMs_;  // kModeSmolder awake: hazard clock origin
  uint32_t sleepCheckMs_;        // kModeSmolder awake: next hazard roll time
  uint32_t sleepWakeMs_;         // kModeSmolder asleep: absolute wake time
  uint32_t sleepOpenStartMs_;    // kModeSmolder asleep: current half-open event start
  uint32_t sleepNextOpenMs_;     // kModeSmolder asleep: next half-open event time
  uint8_t sleepOpenMask_;        // kModeSmolder asleep: eyes in the current event
  uint32_t sleepOpenSeq_;        // kModeSmolder asleep: half-open event counter
  uint8_t agitationKind_;        // kModeSmolder agitated: current animation
  uint32_t agitationSeq_;        // kModeSmolder: agitation counter (picks the kind)
  uint32_t agitationStartMs_;    // kModeSmolder agitated: animation phase origin
  uint32_t lastRadarHighMs_;     // kModeSmolder agitated: last radar-high tick
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
  uint32_t toxicCycle_;         // kModeToxic: completed spark revolutions
  bool toxicSparkRed_;          // kModeToxic: current revolution rides red
  uint32_t toxicLastTickMs_;
  uint32_t hypnoPhase_;         // kModeHypnotic: chase position, eye-permille units
  uint32_t hypnoCycle_;         // kModeHypnotic: completed chase revolutions
  uint32_t hypnoLastTickMs_;
  bool active_;              // false until the first setMode()
};

}  // namespace ledfx
