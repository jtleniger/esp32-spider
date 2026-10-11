// formatHealth tests. No main() here: test_led_engine.cpp owns the only one in
// the `native/ledfx` suite and runs these cases too.
#include <health.h>
#include <modes.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

namespace {

ledfx::HealthStatus statusWith(uint16_t mode, uint32_t uptimeMs,
                               uint32_t epochSeconds) {
  ledfx::HealthStatus status = {};
  status.mode = mode;
  status.uptimeMs = uptimeMs;
  status.epochSeconds = epochSeconds;
  return status;
}

constexpr uint32_t kSyncedEpochS = 1791738245;  // 2026-10-11T17:04:05Z

}  // namespace

void testHealthBodyExact() {
  char body[ledfx::kHealthBodyCapacity];
  ledfx::HealthStatus status = statusWith(ledfx::kModeSmolder, 42000,
                                          kSyncedEpochS);
  status.logFiles = 3;
  status.freeBytes = 1400000;
  const size_t len = ledfx::formatHealth(body, sizeof(body), status);
  TEST_ASSERT_EQUAL_STRING(
      "{\"mode\":0,\"uptime_s\":42,\"utc\":\"2026-10-11T17:04:05Z\","
      "\"clock_set\":true,\"log_files\":3,\"free_bytes\":1400000}",
      body);
  TEST_ASSERT_EQUAL_UINT32(strlen(body), len);
}

// Sub-second remainders are dropped, not rounded: the reported uptime is the
// whole number of seconds the rig has been up.
void testHealthUptimeIsWholeSeconds() {
  char body[ledfx::kHealthBodyCapacity];
  ledfx::formatHealth(body, sizeof(body),
                      statusWith(ledfx::kModeToxic, 999, kSyncedEpochS));
  TEST_ASSERT_EQUAL_STRING(
      "{\"mode\":6,\"uptime_s\":0,\"utc\":\"2026-10-11T17:04:05Z\","
      "\"clock_set\":true,\"log_files\":0,\"free_bytes\":0}",
      body);
  ledfx::formatHealth(body, sizeof(body),
                      statusWith(ledfx::kModeToxic, 1999, kSyncedEpochS));
  TEST_ASSERT_TRUE(strstr(body, "\"uptime_s\":1,") != nullptr);
  ledfx::formatHealth(body, sizeof(body),
                      statusWith(ledfx::kModeToxic, 1000, kSyncedEpochS));
  TEST_ASSERT_TRUE(strstr(body, "\"uptime_s\":1,") != nullptr);
}

// Before SNTP gets out, time() still reads the 1970 default: the body must say
// so instead of presenting that stamp as the current time.
void testHealthBeforeClockSync() {
  char body[ledfx::kHealthBodyCapacity];
  ledfx::formatHealth(body, sizeof(body), statusWith(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_EQUAL_STRING(
      "{\"mode\":2,\"uptime_s\":0,\"utc\":\"1970-01-01T00:00:00Z\","
      "\"clock_set\":false,\"log_files\":0,\"free_bytes\":0}",
      body);
}

// The widest values the fields can carry must still fit: a truncating body
// would hand the client a malformed JSON object. The epoch bound is this
// target's signed 32-bit time_t, which is what time() can actually return.
void testHealthBodyFitsExtremeValues() {
  char body[ledfx::kHealthBodyCapacity];
  ledfx::HealthStatus status = statusWith(65535, 0xFFFFFFFFu, 2147483647);
  status.logFiles = 0xFFFFFFFFu;
  status.freeBytes = 0xFFFFFFFFu;
  const size_t len = ledfx::formatHealth(body, sizeof(body), status);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len < ledfx::kHealthBodyCapacity);
  TEST_ASSERT_EQUAL_CHAR('{', body[0]);
  TEST_ASSERT_EQUAL_CHAR('}', body[len - 1]);
  TEST_ASSERT_EQUAL_UINT32(len, strlen(body));
  TEST_ASSERT_TRUE(strstr(body, "\"clock_set\":true") != nullptr);
}

void testHealthBodyRejectsTinyBuffer() {
  char body[8] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatHealth(body, sizeof(body),
                             statusWith(ledfx::kModeSmolder, 0, 0)));
  TEST_ASSERT_EQUAL_STRING("", body);
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatHealth(nullptr, 16, statusWith(0, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatHealth(body, 0, statusWith(0, 0, 0)));
}
