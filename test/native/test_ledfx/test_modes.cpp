// parseModeArg / parseChannelArg tests. No main() here: test_led_engine.cpp owns
// the only one in the `native/ledfx` suite and runs these cases too.
#include <led_layout.h>
#include <modes.h>
#include <unity.h>

#include <cstdint>

static void assertRejected(const char *text) {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_FALSE(ledfx::parseModeArg(text, out));
}

static void assertChannelRejected(const char *text) {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_FALSE(ledfx::parseChannelArg(text, out));
}

void testParseAcceptZero() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_TRUE(ledfx::parseModeArg("0", out));
  TEST_ASSERT_EQUAL_UINT16(0, out);
}

void testParseAcceptOne() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_TRUE(ledfx::parseModeArg("1", out));
  TEST_ASSERT_EQUAL_UINT16(1, out);
}

void testParseAcceptLeadingZeros() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_TRUE(ledfx::parseModeArg("00", out));
  TEST_ASSERT_EQUAL_UINT16(0, out);
  TEST_ASSERT_TRUE(ledfx::parseModeArg("01", out));
  TEST_ASSERT_EQUAL_UINT16(1, out);
}

void testParseRejectEmpty() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_FALSE(ledfx::parseModeArg("", out));
  TEST_ASSERT_FALSE(ledfx::parseModeArg(nullptr, out));
  // A rejected value must not be written back to the caller.
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, out);
}

void testParseRejectOutOfRange() {
  assertRejected("2");  // parses as a number, but kModeCount is 2
  assertRejected("3");
}

void testParseRejectLargeValues() {
  assertRejected("7");
  assertRejected("65536");
  assertRejected("99999999999999999999");
}

void testParseRejectNonDigits() {
  assertRejected("abc");
  assertRejected("0x0");
  assertRejected("-1");
  assertRejected("+0");
  assertRejected("1e0");
  assertRejected("0.0");
}

void testParseRejectWhitespace() {
  assertRejected("0 ");  // server.arg() is returned raw, no trimming
  assertRejected(" 0");
  assertRejected("1 ");
}

void testParseChannelAccept() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_TRUE(ledfx::parseChannelArg("0", out));
  TEST_ASSERT_EQUAL_UINT16(0, out);
  TEST_ASSERT_TRUE(ledfx::parseChannelArg("6", out));
  TEST_ASSERT_EQUAL_UINT16(6, out);
  TEST_ASSERT_TRUE(ledfx::parseChannelArg("23", out));  // kChannelCount - 1
  TEST_ASSERT_EQUAL_UINT16(23, out);
}

void testParseChannelAcceptLeadingZeros() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_TRUE(ledfx::parseChannelArg("07", out));
  TEST_ASSERT_EQUAL_UINT16(7, out);
  TEST_ASSERT_TRUE(ledfx::parseChannelArg("000", out));
  TEST_ASSERT_EQUAL_UINT16(0, out);
}

void testParseChannelRejectEmpty() {
  uint16_t out = 0xFFFF;
  TEST_ASSERT_FALSE(ledfx::parseChannelArg("", out));
  TEST_ASSERT_FALSE(ledfx::parseChannelArg(nullptr, out));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, out);
}

void testParseChannelRejectOutOfRange() {
  assertChannelRejected("24");  // parses as a number, but kChannelCount is 24
  assertChannelRejected("25");
  assertChannelRejected("255");
  assertChannelRejected("256");
  assertChannelRejected("65536");
  assertChannelRejected("99999999999999999999");
}

void testParseChannelRejectNonDigits() {
  assertChannelRejected("abc");
  assertChannelRejected("0x1");
  assertChannelRejected("-1");
  assertChannelRejected("+1");
  assertChannelRejected("1e0");
  assertChannelRejected("1.0");
}

void testParseChannelRejectWhitespace() {
  assertChannelRejected("1 ");
  assertChannelRejected(" 1");
  assertChannelRejected("1\n");
}
