// clock.h tests. No main() here: test_led_engine.cpp owns the only one in the
// `native/ledfx` suite and runs these cases too.
#include <clock.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

namespace {

void assertStamp(uint32_t epochSeconds, const char *expected) {
  char stamp[ledfx::kUtcStampCapacity];
  const size_t len = ledfx::formatUtcStamp(stamp, sizeof(stamp), epochSeconds);
  TEST_ASSERT_EQUAL_STRING(expected, stamp);
  TEST_ASSERT_EQUAL_UINT32(strlen(expected), len);
}

}  // namespace

// The stamp is always UTC, whatever local time zone configTime() installed, so
// the same epoch has to render the same bytes everywhere.
void testUtcStampKnownEpochs() {
  assertStamp(0, "1970-01-01T00:00:00Z");
  assertStamp(1000000000, "2001-09-09T01:46:40Z");
  assertStamp(1609459200, "2021-01-01T00:00:00Z");
  assertStamp(1791738245, "2026-10-11T17:04:05Z");
  // The largest value this target's 32-bit time_t can hold.
  assertStamp(2147483647, "2038-01-19T03:14:07Z");
}

// A leap day is where hand-rolled calendar maths usually breaks.
void testUtcStampLeapDay() {
  assertStamp(1709210096, "2024-02-29T12:34:56Z");
  assertStamp(951782400, "2000-02-29T00:00:00Z");
}

void testUtcStampRejectsSmallBuffers() {
  char stamp[ledfx::kUtcStampCapacity];
  TEST_ASSERT_EQUAL_UINT32(
      strlen("2026-10-11T17:04:05Z"),
      ledfx::formatUtcStamp(stamp, ledfx::kUtcStampCapacity, 1791738245));
  TEST_ASSERT_EQUAL_UINT32(strlen("2026-10-11T17:04:05Z") + 1,
                           ledfx::kUtcStampCapacity);

  char tight[ledfx::kUtcStampCapacity - 1] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatUtcStamp(tight, sizeof(tight),
                                                    1791738245));
  TEST_ASSERT_EQUAL_STRING("", tight);
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatUtcStamp(nullptr, 21, 0));
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatUtcStamp(stamp, 0, 0));
}

// The threshold is what /health reports as clock_set: the 1970 default must
// read as "not set", and the first second of 2021 as "set".
void testClockIsSetThreshold() {
  TEST_ASSERT_FALSE(ledfx::clockIsSet(0));
  TEST_ASSERT_FALSE(ledfx::clockIsSet(ledfx::kClockSetEpochS - 1));
  TEST_ASSERT_FALSE(ledfx::clockIsSet(1609459199));  // 2020-12-31T23:59:59Z
  TEST_ASSERT_TRUE(ledfx::clockIsSet(ledfx::kClockSetEpochS));
  TEST_ASSERT_TRUE(ledfx::clockIsSet(2147483647));
}
