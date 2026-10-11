// formatHealth tests. No main() here: test_led_engine.cpp owns the only one in
// the `native/ledfx` suite and runs these cases too.
#include <health.h>
#include <modes.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

void testHealthBodyExact() {
  char body[ledfx::kHealthBodyCapacity];
  const size_t len = ledfx::formatHealth(body, sizeof(body), ledfx::kModeSmolder,
                                         42000);
  TEST_ASSERT_EQUAL_STRING("{\"mode\":0,\"uptime_s\":42}", body);
  TEST_ASSERT_EQUAL_UINT32(strlen("{\"mode\":0,\"uptime_s\":42}"), len);
}

// Sub-second remainders are dropped, not rounded: the reported uptime is the
// whole number of seconds the rig has been up.
void testHealthUptimeIsWholeSeconds() {
  char body[ledfx::kHealthBodyCapacity];
  ledfx::formatHealth(body, sizeof(body), ledfx::kModeToxic, 999);
  TEST_ASSERT_EQUAL_STRING("{\"mode\":6,\"uptime_s\":0}", body);
  ledfx::formatHealth(body, sizeof(body), ledfx::kModeToxic, 1999);
  TEST_ASSERT_EQUAL_STRING("{\"mode\":6,\"uptime_s\":1}", body);
  ledfx::formatHealth(body, sizeof(body), ledfx::kModeToxic, 1000);
  TEST_ASSERT_EQUAL_STRING("{\"mode\":6,\"uptime_s\":1}", body);
}

// The widest values either field can carry must still fit: a truncating body
// would hand the client a malformed JSON object.
void testHealthBodyFitsExtremeValues() {
  char body[ledfx::kHealthBodyCapacity];
  const size_t len = ledfx::formatHealth(body, sizeof(body), 65535, 0xFFFFFFFFu);
  TEST_ASSERT_EQUAL_STRING("{\"mode\":65535,\"uptime_s\":4294967}", body);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len < ledfx::kHealthBodyCapacity);
}

void testHealthBodyRejectsTinyBuffer() {
  char body[8] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatHealth(body, sizeof(body),
                                                 ledfx::kModeSmolder, 0));
  TEST_ASSERT_EQUAL_STRING("", body);
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatHealth(nullptr, 16, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatHealth(body, 0, 0, 0));
}
