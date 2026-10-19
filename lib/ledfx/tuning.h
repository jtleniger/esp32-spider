#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "led_layout.h"

namespace ledfx {

// Runtime tuning for every effect knob in led_layout.h. The constexprs stay the
// single source of truth for the defaults - each field's default is that
// constant - and this struct is what the engine actually reads, so POST /config
// can override a knob at runtime and Preferences can persist the result.
//
// Nothing structural is tunable: pins, channel/eye counts and kEyes are wiring,
// not tuning, and the derived values (a period, a whole wake sequence) are
// computed from the fields rather than stored, so they cannot disagree with
// their parts.
struct Tuning {
  // --- kModePulse (mode 2) ---
  uint32_t pulseHalfSlowMs = kPulseHalfSlowMs;
  uint32_t pulseHalfFastMs = kPulseHalfFastMs;
  uint16_t pulseLevels = kPulseLevels;

  // --- kModeSmolder (mode 0) embers ---
  uint32_t smolderDwellMs = kSmolderDwellMs;
  uint32_t smolderFadeMs = kSmolderFadeMs;
  uint16_t smolderLevels = kSmolderLevels;
  uint16_t smolderFlickerPwm = kSmolderFlickerPwm;
  uint32_t smolderFlickerStepMs = kSmolderFlickerStepMs;

  // --- kModeSmolder sleep/wake machine ---
  uint32_t smolderSleepAfterMs = kSmolderSleepAfterMs;
  uint32_t smolderSleepDeadlineMs = kSmolderSleepDeadlineMs;
  uint32_t smolderSleepCheckMs = kSmolderSleepCheckMs;
  uint32_t smolderSleepMinMs = kSmolderSleepMinMs;
  uint32_t smolderSleepMaxMs = kSmolderSleepMaxMs;
  uint32_t sleepFallMinMs = kSleepFallMinMs;
  uint32_t sleepFallMaxMs = kSleepFallMaxMs;
  uint32_t sleepOpenMinGapMs = kSleepOpenMinGapMs;
  uint32_t sleepOpenMaxGapMs = kSleepOpenMaxGapMs;
  uint16_t sleepOpenGroupPermille = kSleepOpenGroupPermille;
  uint32_t sleepGestureMs = kSleepGestureMs;
  uint32_t sleepDipEndMs = kSleepDipEndMs;
  uint32_t sleepFlareEndMs = kSleepFlareEndMs;
  uint16_t sleepDipPermille = kSleepDipPermille;
  uint16_t sleepFlarePermille = kSleepFlarePermille;
  uint32_t sleepWakeSpreadMs = kSleepWakeSpreadMs;

  // --- kModeSmolder awake blinks ---
  uint32_t smolderBlinkMinGapMs = kSmolderBlinkMinGapMs;
  uint32_t smolderBlinkMaxGapMs = kSmolderBlinkMaxGapMs;
  uint32_t smolderBlinkCloseMs = kSmolderBlinkCloseMs;
  uint32_t smolderBlinkHoldMs = kSmolderBlinkHoldMs;
  uint32_t smolderBlinkOpenMs = kSmolderBlinkOpenMs;

  // --- kModeSmolder agitation (green) ---
  uint32_t agitationHoldMs = kAgitationHoldMs;
  uint32_t agitationExitMs = kAgitationExitMs;
  uint16_t agitationFlickerPwm = kAgitationFlickerPwm;
  uint32_t agitationFlickerStepMs = kAgitationFlickerStepMs;
  uint32_t agitationSweepMinMs = kAgitationSweepMinMs;
  uint32_t agitationSweepMaxMs = kAgitationSweepMaxMs;
  uint32_t agitationSweepPauseMinMs = kAgitationSweepPauseMinMs;
  uint32_t agitationSweepPauseMaxMs = kAgitationSweepPauseMaxMs;
  uint16_t agitationSweepTailPermille = kAgitationSweepTailPermille;
  uint32_t greenSpreadMs = kGreenSpreadMs;
  uint32_t greenAttackMs = kGreenAttackMs;
  uint32_t greenFadeMs = kGreenFadeMs;
  uint16_t greenFlickerPwm = kGreenFlickerPwm;

  // --- kModeStalker (mode 3) ---
  uint32_t stalkerStepMs = kStalkerStepMs;
  uint16_t stalkerDimPwm = kStalkerDimPwm;
  uint16_t stalkerGreenPermille = kStalkerGreenPermille;
  uint16_t stalkerFlickerPwm = kStalkerFlickerPwm;
  uint32_t stalkerFlickerStepMs = kStalkerFlickerStepMs;
  uint32_t stalkerPopMs = kStalkerPopMs;
  uint32_t stalkerReleaseMs = kStalkerReleaseMs;
  uint16_t stalkerPopPwm = kStalkerPopPwm;
  uint16_t stalkerPopFlickerPwm = kStalkerPopFlickerPwm;

  // --- kModeToxic (mode 6) ---
  uint16_t toxicGreenBasePwm = kToxicGreenBasePwm;
  uint16_t toxicGreenFlickerPwm = kToxicGreenFlickerPwm;
  uint32_t toxicGreenStepMs = kToxicGreenStepMs;
  uint16_t toxicSparkIdlePwm = kToxicSparkIdlePwm;
  uint16_t toxicSparkHeadPwm = kToxicSparkHeadPwm;
  uint16_t toxicSparkFlickerPwm = kToxicSparkFlickerPwm;
  uint16_t toxicSparkFranticPwm = kToxicSparkFranticPwm;
  uint32_t toxicSparkIdleStepMs = kToxicSparkIdleStepMs;
  uint32_t toxicSparkFastStepMs = kToxicSparkFastStepMs;
  uint16_t toxicSparkTailPermille = kToxicSparkTailPermille;
  uint32_t toxicMotionRampMs = kToxicMotionRampMs;
  uint32_t toxicFlickerStepMs = kToxicFlickerStepMs;

  // Values the engine derives from the fields above. They replace
  // kSmolderPeriodMs / kSmolderWakeMs / kAgitationRippleExitMs, which stay in
  // led_layout.h as the documented default relationships (and are what the
  // engine's static_asserts check).
  constexpr uint32_t smolderPeriodMs() const {
    return 2 * (smolderDwellMs + smolderFadeMs);
  }
  constexpr uint32_t smolderWakeMs() const {
    return (kEyeCount - 1) * sleepWakeSpreadMs + sleepGestureMs;
  }
  constexpr uint32_t smolderBlinkSpanMs() const {
    return smolderBlinkCloseMs + smolderBlinkHoldMs + smolderBlinkOpenMs;
  }
  constexpr uint32_t agitationRippleExitMs() const {
    return (kEyeCount - 1) * greenSpreadMs + greenFadeMs;
  }
};

// --- The POST /config field table ---
// One row per knob: the HTTP name, which of the two member pointers carries the
// value, and the accepted range. The range is also what keeps the engine's
// arithmetic safe - every knob the engine divides by has a nonzero minimum, and
// the pairs it subtracts to build a span must stay ordered (see
// validateTuning).

constexpr uint32_t kTuningMaxMs = 3600000;    // 1 h ceiling on an ms knob
constexpr uint32_t kTuningMaxStepMs = 60000;  // 1 min ceiling on a refresh knob
constexpr uint32_t kTuningMaxPermille = 1000;
// The sweep tail spans at most one eye spacing per eye but one; longer would
// wrap past the head and light the whole face.
constexpr uint32_t kTuningMaxSweepTailPermille = (kEyeCount - 1) * 1000;

struct TuningField {
  const char *name;
  // Exactly one of these is set, and that is the knob's type: the uint16 fields
  // are PWM levels / permilles / rung counts, the uint32 ones milliseconds.
  uint16_t Tuning::*u16;
  uint32_t Tuning::*u32;
  uint32_t min;
  uint32_t max;
};

constexpr TuningField kTuningFields[] = {
    // --- kModePulse (mode 2) ---
    {"pulse_half_slow_ms", nullptr, &Tuning::pulseHalfSlowMs, 1, kTuningMaxMs},
    {"pulse_half_fast_ms", nullptr, &Tuning::pulseHalfFastMs, 1, kTuningMaxMs},
    {"pulse_levels", &Tuning::pulseLevels, nullptr, 1, kMaxPwm},
    // --- kModeSmolder (mode 0) embers ---
    {"smolder_dwell_ms", nullptr, &Tuning::smolderDwellMs, 0, kTuningMaxMs},
    {"smolder_fade_ms", nullptr, &Tuning::smolderFadeMs, 1, kTuningMaxMs},
    {"smolder_levels", &Tuning::smolderLevels, nullptr, 1, kMaxPwm},
    {"smolder_flicker_pwm", &Tuning::smolderFlickerPwm, nullptr, 0, kMaxPwm},
    {"smolder_flicker_step_ms", nullptr, &Tuning::smolderFlickerStepMs, 1,
     kTuningMaxStepMs},
    // --- kModeSmolder sleep/wake machine ---
    {"smolder_sleep_after_ms", nullptr, &Tuning::smolderSleepAfterMs, 1,
     kTuningMaxMs},
    {"smolder_sleep_deadline_ms", nullptr, &Tuning::smolderSleepDeadlineMs, 1,
     kTuningMaxMs},
    {"smolder_sleep_check_ms", nullptr, &Tuning::smolderSleepCheckMs, 1,
     kTuningMaxMs},
    {"smolder_sleep_min_ms", nullptr, &Tuning::smolderSleepMinMs, 1,
     kTuningMaxMs},
    {"smolder_sleep_max_ms", nullptr, &Tuning::smolderSleepMaxMs, 1,
     kTuningMaxMs},
    {"sleep_fall_min_ms", nullptr, &Tuning::sleepFallMinMs, 1, kTuningMaxMs},
    {"sleep_fall_max_ms", nullptr, &Tuning::sleepFallMaxMs, 1, kTuningMaxMs},
    {"sleep_open_min_gap_ms", nullptr, &Tuning::sleepOpenMinGapMs, 1,
     kTuningMaxMs},
    {"sleep_open_max_gap_ms", nullptr, &Tuning::sleepOpenMaxGapMs, 1,
     kTuningMaxMs},
    {"sleep_open_group_permille", &Tuning::sleepOpenGroupPermille, nullptr, 0,
     kTuningMaxPermille},
    {"sleep_gesture_ms", nullptr, &Tuning::sleepGestureMs, 1, kTuningMaxMs},
    {"sleep_dip_end_ms", nullptr, &Tuning::sleepDipEndMs, 1, kTuningMaxMs},
    {"sleep_flare_end_ms", nullptr, &Tuning::sleepFlareEndMs, 1, kTuningMaxMs},
    {"sleep_dip_permille", &Tuning::sleepDipPermille, nullptr, 0,
     kTuningMaxPermille - 1},
    {"sleep_flare_permille", &Tuning::sleepFlarePermille, nullptr, 0,
     kTuningMaxPermille - 1},
    {"sleep_wake_spread_ms", nullptr, &Tuning::sleepWakeSpreadMs, 1,
     kTuningMaxMs},
    // --- kModeSmolder awake blinks ---
    {"smolder_blink_min_gap_ms", nullptr, &Tuning::smolderBlinkMinGapMs, 1,
     kTuningMaxMs},
    {"smolder_blink_max_gap_ms", nullptr, &Tuning::smolderBlinkMaxGapMs, 1,
     kTuningMaxMs},
    {"smolder_blink_close_ms", nullptr, &Tuning::smolderBlinkCloseMs, 1,
     kTuningMaxMs},
    {"smolder_blink_hold_ms", nullptr, &Tuning::smolderBlinkHoldMs, 0,
     kTuningMaxMs},
    {"smolder_blink_open_ms", nullptr, &Tuning::smolderBlinkOpenMs, 1,
     kTuningMaxMs},
    // --- kModeSmolder agitation (green) ---
    {"agitation_hold_ms", nullptr, &Tuning::agitationHoldMs, 1, kTuningMaxMs},
    {"agitation_exit_ms", nullptr, &Tuning::agitationExitMs, 1, kTuningMaxMs},
    {"agitation_flicker_pwm", &Tuning::agitationFlickerPwm, nullptr, 0,
     kMaxPwm - 1},
    {"agitation_flicker_step_ms", nullptr, &Tuning::agitationFlickerStepMs, 1,
     kTuningMaxStepMs},
    {"agitation_sweep_min_ms", nullptr, &Tuning::agitationSweepMinMs, 1,
     kTuningMaxMs},
    {"agitation_sweep_max_ms", nullptr, &Tuning::agitationSweepMaxMs, 1,
     kTuningMaxMs},
    {"agitation_sweep_pause_min_ms", nullptr, &Tuning::agitationSweepPauseMinMs,
     1, kTuningMaxMs},
    {"agitation_sweep_pause_max_ms", nullptr, &Tuning::agitationSweepPauseMaxMs,
     1, kTuningMaxMs},
    {"agitation_sweep_tail_permille", &Tuning::agitationSweepTailPermille,
     nullptr, 1, kTuningMaxSweepTailPermille},
    {"green_spread_ms", nullptr, &Tuning::greenSpreadMs, 0, kTuningMaxMs},
    {"green_attack_ms", nullptr, &Tuning::greenAttackMs, 1, kTuningMaxStepMs},
    {"green_fade_ms", nullptr, &Tuning::greenFadeMs, 1, kTuningMaxMs},
    {"green_flicker_pwm", &Tuning::greenFlickerPwm, nullptr, 0, kMaxPwm},
    // --- kModeStalker (mode 3) ---
    {"stalker_step_ms", nullptr, &Tuning::stalkerStepMs, 1, kTuningMaxMs},
    {"stalker_dim_pwm", &Tuning::stalkerDimPwm, nullptr, 0, kMaxPwm},
    {"stalker_green_permille", &Tuning::stalkerGreenPermille, nullptr, 0,
     kTuningMaxPermille},
    {"stalker_flicker_pwm", &Tuning::stalkerFlickerPwm, nullptr, 0, kMaxPwm},
    {"stalker_flicker_step_ms", nullptr, &Tuning::stalkerFlickerStepMs, 1,
     kTuningMaxStepMs},
    {"stalker_pop_ms", nullptr, &Tuning::stalkerPopMs, 1, kTuningMaxMs},
    {"stalker_release_ms", nullptr, &Tuning::stalkerReleaseMs, 1, kTuningMaxMs},
    {"stalker_pop_pwm", &Tuning::stalkerPopPwm, nullptr, 0, kMaxPwm},
    {"stalker_pop_flicker_pwm", &Tuning::stalkerPopFlickerPwm, nullptr, 0,
     kMaxPwm},
    // --- kModeToxic (mode 6) ---
    {"toxic_green_base_pwm", &Tuning::toxicGreenBasePwm, nullptr, 0, kMaxPwm},
    {"toxic_green_flicker_pwm", &Tuning::toxicGreenFlickerPwm, nullptr, 0,
     kMaxPwm},
    {"toxic_green_step_ms", nullptr, &Tuning::toxicGreenStepMs, 1,
     kTuningMaxStepMs},
    {"toxic_spark_idle_pwm", &Tuning::toxicSparkIdlePwm, nullptr, 0, kMaxPwm},
    {"toxic_spark_head_pwm", &Tuning::toxicSparkHeadPwm, nullptr, 0, kMaxPwm},
    {"toxic_spark_flicker_pwm", &Tuning::toxicSparkFlickerPwm, nullptr, 0,
     kMaxPwm},
    {"toxic_spark_frantic_pwm", &Tuning::toxicSparkFranticPwm, nullptr, 0,
     kMaxPwm},
    {"toxic_spark_idle_step_ms", nullptr, &Tuning::toxicSparkIdleStepMs, 1,
     kTuningMaxMs},
    {"toxic_spark_fast_step_ms", nullptr, &Tuning::toxicSparkFastStepMs, 1,
     kTuningMaxMs},
    {"toxic_spark_tail_permille", &Tuning::toxicSparkTailPermille, nullptr, 1,
     kTuningMaxPermille},
    {"toxic_motion_ramp_ms", nullptr, &Tuning::toxicMotionRampMs, 1,
     kTuningMaxMs},
    {"toxic_flicker_step_ms", nullptr, &Tuning::toxicFlickerStepMs, 1,
     kTuningMaxStepMs},
};

constexpr size_t kTuningFieldCount =
    sizeof(kTuningFields) / sizeof(kTuningFields[0]);

// Room for every field at its maximum value: the longest body the table can
// produce is what formatTuning() needs, and the host suite pins that it fits.
constexpr size_t kTuningBodyCapacity = 2048;

constexpr uint32_t tuningValue(const Tuning &tuning, const TuningField &field) {
  return field.u16 != nullptr ? tuning.*(field.u16) : tuning.*(field.u32);
}

inline void setTuningValue(Tuning &tuning, const TuningField &field,
                           uint32_t value) {
  if (field.u16 != nullptr) {
    tuning.*(field.u16) = static_cast<uint16_t>(value);
  } else {
    tuning.*(field.u32) = value;
  }
}

inline const TuningField *findTuningField(const char *name) {
  if (name == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < kTuningFieldCount; ++i) {
    if (std::strcmp(kTuningFields[i].name, name) == 0) {
      return &kTuningFields[i];
    }
  }
  return nullptr;
}

// Parses a ?name=value argument for `field`: a non-empty run of decimal ASCII
// digits whose value lands in [min, max]. "+1", "-1", "0x1", "1 ", "" and
// anything past max are rejected, and the accumulator bails at max, so no
// overflow is possible for any range.
inline bool parseTuningValue(const TuningField &field, const char *text,
                             uint32_t &out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  uint64_t value = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint64_t>(*p - '0');
    if (value > field.max) {
      return false;
    }
  }
  if (value < field.min) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

// The relationships the engine's static_asserts require of the defaults,
// checked against whatever a caller wants to run. They are load-bearing, not
// cosmetic: the engine subtracts each min/max pair to get a random span (a zero
// span would divide by zero in hashRange) and divides by the *_end_ms / step
// knobs. Returns false and names the field that has to move when one is broken.
inline bool validateTuning(const Tuning &tuning, const char **offender) {
  const auto fail = [offender](const char *name) {
    if (offender != nullptr) {
      *offender = name;
    }
    return false;
  };
  if (tuning.smolderSleepAfterMs >= tuning.smolderSleepDeadlineMs) {
    return fail("smolder_sleep_after_ms");
  }
  if (tuning.smolderSleepMinMs >= tuning.smolderSleepMaxMs) {
    return fail("smolder_sleep_min_ms");
  }
  if (tuning.sleepFallMinMs >= tuning.sleepFallMaxMs) {
    return fail("sleep_fall_min_ms");
  }
  if (tuning.sleepOpenMinGapMs >= tuning.sleepOpenMaxGapMs) {
    return fail("sleep_open_min_gap_ms");
  }
  if (tuning.smolderBlinkMinGapMs >= tuning.smolderBlinkMaxGapMs) {
    return fail("smolder_blink_min_gap_ms");
  }
  if (tuning.sleepDipEndMs >= tuning.sleepFlareEndMs) {
    return fail("sleep_dip_end_ms");
  }
  if (tuning.sleepFlareEndMs >= tuning.sleepGestureMs) {
    return fail("sleep_flare_end_ms");
  }
  if (tuning.sleepDipPermille >= tuning.sleepFlarePermille) {
    return fail("sleep_dip_permille");
  }
  if (tuning.agitationSweepMinMs >= tuning.agitationSweepMaxMs) {
    return fail("agitation_sweep_min_ms");
  }
  if (tuning.agitationSweepPauseMinMs >= tuning.agitationSweepPauseMaxMs) {
    return fail("agitation_sweep_pause_min_ms");
  }
  if (tuning.toxicSparkIdleStepMs <= tuning.toxicSparkFastStepMs) {
    return fail("toxic_spark_idle_step_ms");
  }
  if (offender != nullptr) {
    *offender = nullptr;
  }
  return true;
}

// Pulls every field back into its table range. Used on values restored from
// NVS, which may predate a change of range.
inline void clampTuning(Tuning &tuning) {
  for (size_t i = 0; i < kTuningFieldCount; ++i) {
    const TuningField &field = kTuningFields[i];
    const uint32_t value = tuningValue(tuning, field);
    if (value < field.min) {
      setTuningValue(tuning, field, field.min);
    } else if (value > field.max) {
      setTuningValue(tuning, field, field.max);
    }
  }
}

// One query argument of POST /config.
struct TuningEdit {
  const char *name;
  const char *value;
};

enum class TuningResult : uint8_t {
  kOk,
  kUnknownField,
  kOutOfRange,
  kInconsistent,
};

// Applies every edit, or none: the request is validated on a copy and only
// committed when every name resolves, every value fits its range and the result
// still satisfies validateTuning(). `offender` (optional) names the field that
// rejected the request, so the caller can say which one to fix.
inline TuningResult applyTuningEdits(Tuning &tuning, const TuningEdit *edits,
                                     size_t count, const char **offender) {
  Tuning next = tuning;
  for (size_t i = 0; i < count; ++i) {
    const TuningField *field = findTuningField(edits[i].name);
    if (field == nullptr) {
      if (offender != nullptr) {
        *offender = edits[i].name;
      }
      return TuningResult::kUnknownField;
    }
    uint32_t value = 0;
    if (!parseTuningValue(*field, edits[i].value, value)) {
      if (offender != nullptr) {
        *offender = field->name;
      }
      return TuningResult::kOutOfRange;
    }
    setTuningValue(next, *field, value);
  }
  if (!validateTuning(next, offender)) {
    return TuningResult::kInconsistent;
  }
  tuning = next;
  return TuningResult::kOk;
}

// Writes the whole table as one JSON object - {"pulse_half_slow_ms":1000,...} -
// using the names POST /config takes, so GET /config doubles as the list of
// knobs. Returns the body length, or 0 (and an empty string) when `capacity` is
// too small for the widest possible body.
inline size_t formatTuning(char *out, size_t capacity, const Tuning &tuning) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  size_t used = 0;
  for (size_t i = 0; i < kTuningFieldCount; ++i) {
    const int written =
        snprintf(out + used, capacity - used, "%s\"%s\":%u", i == 0 ? "{" : ",",
                 kTuningFields[i].name, tuningValue(tuning, kTuningFields[i]));
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) {
      out[0] = '\0';
      return 0;
    }
    used += static_cast<size_t>(written);
  }
  if (used + 2 > capacity) {
    out[0] = '\0';
    return 0;
  }
  out[used++] = '}';
  out[used] = '\0';
  return used;
}

}  // namespace ledfx
