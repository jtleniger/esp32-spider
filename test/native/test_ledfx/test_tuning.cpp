// Tuning table, POST /config edit path and formatTuning tests. No main() here:
// test_led_engine.cpp owns the only one in the `native/ledfx` suite and runs
// these cases too.
#include <led_engine.h>
#include <led_layout.h>
#include <tuning.h>
#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Field-wise equality: the struct has padding, so memcmp() would compare bytes
// the table does not own.
bool sameTuning(const ledfx::Tuning &a, const ledfx::Tuning &b) {
  for (size_t i = 0; i < ledfx::kTuningFieldCount; ++i) {
    const ledfx::TuningField &field = ledfx::kTuningFields[i];
    if (ledfx::tuningValue(a, field) != ledfx::tuningValue(b, field)) {
      return false;
    }
  }
  return true;
}

const char *resultName(ledfx::TuningResult result) {
  switch (result) {
    case ledfx::TuningResult::kOk:
      return "kOk";
    case ledfx::TuningResult::kUnknownField:
      return "kUnknownField";
    case ledfx::TuningResult::kOutOfRange:
      return "kOutOfRange";
    default:
      return "kInconsistent";
  }
}

ledfx::TuningResult applyOne(ledfx::Tuning &tuning, const char *name,
                             const char *value, const char **offender) {
  const ledfx::TuningEdit edit = {name, value};
  return ledfx::applyTuningEdits(tuning, &edit, 1, offender);
}

}  // namespace

// The defaults are what the engine's static_asserts and every other test assume,
// and the derived accessors must agree with the constants they replace.
void testTuningDefaultsAreValid() {
  ledfx::Tuning defaults;
  ledfx::Tuning clamped = defaults;
  ledfx::clampTuning(clamped);
  TEST_ASSERT_TRUE(sameTuning(defaults, clamped));  // every default is in range

  const char *offender = nullptr;
  TEST_ASSERT_TRUE(ledfx::validateTuning(defaults, &offender));
  TEST_ASSERT_NULL(offender);

  TEST_ASSERT_EQUAL_UINT32(ledfx::kSmolderPeriodMs, defaults.smolderPeriodMs());
  TEST_ASSERT_EQUAL_UINT32(ledfx::kSmolderWakeMs, defaults.smolderWakeMs());
  TEST_ASSERT_EQUAL_UINT32(
      ledfx::kSmolderBlinkCloseMs + ledfx::kSmolderBlinkHoldMs +
          ledfx::kSmolderBlinkOpenMs,
      defaults.smolderBlinkSpanMs());
  TEST_ASSERT_EQUAL_UINT32(ledfx::kAgitationRippleExitMs,
                           defaults.agitationRippleExitMs());
}

// Every row must name a real field exactly once, carry the member pointer that
// matches its kind, and bracket its default.
void testTuningTableIsWellFormed() {
  for (size_t i = 0; i < ledfx::kTuningFieldCount; ++i) {
    const ledfx::TuningField &field = ledfx::kTuningFields[i];
    TEST_ASSERT_TRUE_MESSAGE(field.name != nullptr && field.name[0] != '\0',
                             "field name must not be empty");
    TEST_ASSERT_TRUE_MESSAGE(field.min <= field.max, field.name);
    TEST_ASSERT_TRUE_MESSAGE(ledfx::findTuningField(field.name) == &field,
                             field.name);  // unique and findable
    TEST_ASSERT_TRUE_MESSAGE(field.u16 != nullptr || field.u32 != nullptr,
                             field.name);
    // Exactly one member pointer carries the value; that is the knob's type.
    TEST_ASSERT_TRUE_MESSAGE((field.u16 != nullptr) != (field.u32 != nullptr),
                             field.name);
    const uint32_t value = ledfx::tuningValue(ledfx::Tuning{}, field);
    TEST_ASSERT_TRUE_MESSAGE(value >= field.min && value <= field.max,
                             field.name);
  }
}

// Writing each field by name must touch that field and nothing else: this is
// what catches a row pointing at the wrong member or two rows sharing one.
void testTuningEveryFieldEditsOnlyItself() {
  for (size_t i = 0; i < ledfx::kTuningFieldCount; ++i) {
    const ledfx::TuningField &field = ledfx::kTuningFields[i];
    ledfx::Tuning tuning;
    const uint32_t before = ledfx::tuningValue(tuning, field);
    // Nudge by one in whichever direction stays inside the range.
    const uint32_t value = before < field.max ? before + 1 : before - 1;
    char text[12];
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));

    const char *offender = nullptr;
    const ledfx::TuningResult result = applyOne(tuning, field.name, text, &offender);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("kOk", resultName(result), field.name);
    TEST_ASSERT_NULL(offender);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(value, ledfx::tuningValue(tuning, field),
                                     field.name);
    for (size_t j = 0; j < ledfx::kTuningFieldCount; ++j) {
      if (j == i) {
        continue;
      }
      const ledfx::TuningField &other = ledfx::kTuningFields[j];
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(ledfx::tuningValue(ledfx::Tuning{}, other),
                                       ledfx::tuningValue(tuning, other),
                                       field.name);
    }
  }
}

void testTuningRejectsUnknownField() {
  ledfx::Tuning tuning;
  const char *offender = nullptr;
  const ledfx::TuningResult result =
      applyOne(tuning, "smolder_fade", "10", &offender);  // no _ms suffix
  TEST_ASSERT_EQUAL_STRING("kUnknownField", resultName(result));
  TEST_ASSERT_EQUAL_STRING("smolder_fade", offender);
  TEST_ASSERT_TRUE(sameTuning(ledfx::Tuning{}, tuning));
  TEST_ASSERT_NULL(ledfx::findTuningField(nullptr));
}

void testTuningRejectsBadValues() {
  ledfx::Tuning tuning;
  const char *offender = nullptr;
  // Lexically invalid, then out of range at both ends. pulse_half_slow_ms is
  // [1, 3600000].
  const char *bad[] = {"", "abc", "-1", "+1", "1.5", "1e3", " 1", "1 ", "0",
                       "3600001", "99999999999999999999"};
  for (const char *text : bad) {
    const ledfx::TuningResult result =
        applyOne(tuning, "pulse_half_slow_ms", text, &offender);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("kOutOfRange", resultName(result), text);
    TEST_ASSERT_EQUAL_STRING("pulse_half_slow_ms", offender);
    TEST_ASSERT_TRUE(sameTuning(ledfx::Tuning{}, tuning));
  }
  // The range bounds themselves are accepted.
  TEST_ASSERT_EQUAL_STRING("kOk", resultName(applyOne(
                                       tuning, "pulse_half_slow_ms", "1", &offender)));
  TEST_ASSERT_EQUAL_STRING("kOk",
                           resultName(applyOne(tuning, "pulse_half_slow_ms",
                                               "3600000", &offender)));
}

// The min/max pairs are subtracted to build random spans and the step knobs are
// divisors, so an inverted pair has to be rejected rather than run.
void testTuningRejectsInconsistent() {
  ledfx::Tuning tuning;
  const char *offender = nullptr;
  const ledfx::TuningResult result =
      applyOne(tuning, "smolder_sleep_min_ms", "999999", &offender);
  TEST_ASSERT_EQUAL_STRING("kInconsistent", resultName(result));
  TEST_ASSERT_EQUAL_STRING("smolder_sleep_min_ms", offender);
  TEST_ASSERT_TRUE(sameTuning(ledfx::Tuning{}, tuning));

  const ledfx::TuningResult blink =
      applyOne(tuning, "smolder_blink_min_gap_ms", "12000", &offender);
  TEST_ASSERT_EQUAL_STRING("kInconsistent", resultName(blink));
  TEST_ASSERT_EQUAL_STRING("smolder_blink_min_gap_ms", offender);
  TEST_ASSERT_TRUE(sameTuning(ledfx::Tuning{}, tuning));
}

// Several fields in one request: applied together, and rejected together when
// the combination ends inconsistent - including when one edit is what fixes the
// other, so a pair can be swapped in a single call.
void testTuningMultiEditIsAtomic() {
  ledfx::Tuning tuning;
  const char *offender = nullptr;
  const ledfx::TuningEdit fix[] = {{"smolder_sleep_min_ms", "100"},
                                   {"smolder_sleep_max_ms", "200"}};
  TEST_ASSERT_EQUAL_STRING(
      "kOk", resultName(ledfx::applyTuningEdits(tuning, fix, 2, &offender)));
  TEST_ASSERT_EQUAL_UINT32(100, tuning.smolderSleepMinMs);
  TEST_ASSERT_EQUAL_UINT32(200, tuning.smolderSleepMaxMs);

  ledfx::Tuning before = tuning;
  const ledfx::TuningEdit broken[] = {{"smolder_sleep_max_ms", "500"},
                                      {"smolder_sleep_min_ms", "600"}};
  TEST_ASSERT_EQUAL_STRING(
      "kInconsistent",
      resultName(ledfx::applyTuningEdits(tuning, broken, 2, &offender)));
  TEST_ASSERT_EQUAL_STRING("smolder_sleep_min_ms", offender);
  TEST_ASSERT_TRUE(sameTuning(before, tuning));  // neither edit landed

  // A bad name anywhere rejects the whole request too.
  const ledfx::TuningEdit mixed[] = {{"stalker_dim_pwm", "900"},
                                     {"nope", "1"}};
  TEST_ASSERT_EQUAL_STRING(
      "kUnknownField",
      resultName(ledfx::applyTuningEdits(tuning, mixed, 2, &offender)));
  TEST_ASSERT_TRUE(sameTuning(before, tuning));
}

// Values restored from NVS are clamped into the table's ranges.
void testTuningClampPullsValuesIntoRange() {
  ledfx::Tuning tuning;
  tuning.smolderFadeMs = 0;        // below min
  tuning.stalkerDimPwm = 65535;    // above max
  tuning.pulseLevels = 4095;       // already at max
  ledfx::clampTuning(tuning);
  TEST_ASSERT_EQUAL_UINT32(1, tuning.smolderFadeMs);
  TEST_ASSERT_EQUAL_UINT32(ledfx::kMaxPwm, tuning.stalkerDimPwm);
  TEST_ASSERT_EQUAL_UINT32(ledfx::kMaxPwm, tuning.pulseLevels);
}

// GET /config is the knob list: every name must appear with its value, and the
// widest possible body must fit the buffer the handler serves from.
void testTuningFormatListsEveryField() {
  static char body[ledfx::kTuningBodyCapacity];
  const ledfx::Tuning defaults;
  const size_t len = ledfx::formatTuning(body, sizeof(body), defaults);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len < ledfx::kTuningBodyCapacity);
  TEST_ASSERT_EQUAL_CHAR('{', body[0]);
  TEST_ASSERT_EQUAL_CHAR('}', body[len - 1]);
  TEST_ASSERT_EQUAL_UINT32(len, strlen(body));

  for (size_t i = 0; i < ledfx::kTuningFieldCount; ++i) {
    const ledfx::TuningField &field = ledfx::kTuningFields[i];
    char needle[48];
    snprintf(needle, sizeof(needle), "\"%s\":%u", field.name,
             ledfx::tuningValue(defaults, field));
    TEST_ASSERT_TRUE_MESSAGE(strstr(body, needle) != nullptr, field.name);
  }

  // Worst case: every knob at its maximum, seven digits each.
  ledfx::Tuning widest;
  for (size_t i = 0; i < ledfx::kTuningFieldCount; ++i) {
    ledfx::setTuningValue(widest, ledfx::kTuningFields[i],
                          ledfx::kTuningFields[i].max);
  }
  const size_t wideLen = ledfx::formatTuning(body, sizeof(body), widest);
  TEST_ASSERT_TRUE(wideLen > 0);
  TEST_ASSERT_TRUE(wideLen < ledfx::kTuningBodyCapacity);

  // Too small a buffer is refused rather than half-written.
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatTuning(body, 8, defaults));
  TEST_ASSERT_EQUAL_STRING("", body);
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatTuning(nullptr, sizeof(body), defaults));
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatTuning(body, 0, defaults));
}

// The point of the endpoint: an edit reaches the running effect. Mode 2's slow
// half cycle is 1 s by default, so 100 ms in is a dim rung; shortened to 100 ms
// the same tick is the peak.
void testEngineFollowsEditedTuning() {
  ledfx::LedEngine plain;
  TEST_ASSERT_TRUE(plain.setMode(ledfx::kModePulse, 0, 0));
  plain.tick(0, false);
  plain.tick(100, false);
  TEST_ASSERT_TRUE(plain.frame()[0] > 0);
  TEST_ASSERT_TRUE(plain.frame()[0] < ledfx::kMaxPwm);

  ledfx::LedEngine tuned;
  const char *offender = nullptr;
  TEST_ASSERT_EQUAL_STRING(
      "kOk", resultName(applyOne(tuned.tuning(), "pulse_half_slow_ms", "100",
                                 &offender)));
  // Read back through the endpoint's own listing.
  static char body[ledfx::kTuningBodyCapacity];
  TEST_ASSERT_TRUE(ledfx::formatTuning(body, sizeof(body), tuned.tuning()) > 0);
  TEST_ASSERT_TRUE(strstr(body, "\"pulse_half_slow_ms\":100") != nullptr);

  TEST_ASSERT_TRUE(tuned.setMode(ledfx::kModePulse, 0, 0));
  tuned.tick(0, false);
  tuned.tick(100, false);  // half of the new 200 ms cycle
  TEST_ASSERT_EQUAL_UINT16(ledfx::kMaxPwm, tuned.frame()[0]);
}
