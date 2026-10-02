// Engine tests plus the single runner for the `native/ledfx` suite. PlatformIO's
// Unity integration generates only unity_config (weak setUp/tearDown) and no
// main(), and all files under one test directory link into one binary, so
// exactly one file owns main() - here - and it also runs the cases declared in
// test_modes.cpp.
#include <led_engine.h>
#include <led_layout.h>
#include <unity.h>

#include <cstdint>

namespace {

ledfx::LedEngine engine;

void assertRange(const ledfx::LedEngine &e, uint8_t first, uint8_t last,
                 uint16_t value) {
  for (uint16_t channel = first; channel <= last; ++channel) {
    TEST_ASSERT_EQUAL_UINT16(value, e.frame()[channel]);
  }
}

// Exactly `selected` sits at full PWM; every other channel is off. This is the
// observable shape of kModeSingleChannel, including the empty footprints.
void assertOnlyChannelLit(const ledfx::LedEngine &e, uint16_t selected) {
  for (uint16_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
    TEST_ASSERT_EQUAL_UINT16(channel == selected ? ledfx::kMaxPwm : 0,
                             e.frame()[channel]);
  }
}

void assertAllOff(const ledfx::LedEngine &e) {
  assertRange(e, 0, ledfx::kChannelCount - 1, 0);
}

// Channels with an LED soldered on, i.e. the ones kModePulse drives.
void assertFitted(const ledfx::LedEngine &e, uint16_t value) {
  assertRange(e, ledfx::kFirstChannel, ledfx::kLastChannel, value);
}

// 0..kFirstChannel-1 and kLastChannel+1..kChannelCount-1: empty footprints.
void assertUnfitted(const ledfx::LedEngine &e, uint16_t value) {
  assertRange(e, 0, ledfx::kFirstChannel - 1, value);
  assertRange(e, ledfx::kLastChannel + 1, ledfx::kChannelCount - 1, value);
}

constexpr uint16_t kOneStepLevel =
    static_cast<uint16_t>(ledfx::kMaxPwm / ledfx::kPulseLevels);

// Level renderPulse() emits `up` ms into a half cycle of `halfMs`.
uint16_t pulseLevel(uint32_t up, uint32_t halfMs) {
  return static_cast<uint16_t>((up * ledfx::kPulseLevels / halfMs) *
                               ledfx::kMaxPwm / ledfx::kPulseLevels);
}

// The level shared by every fitted channel.
uint16_t fittedLevel(const ledfx::LedEngine &e) {
  return e.frame()[ledfx::kFirstChannel];
}

}  // namespace

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

// --- parseModeArg / parseChannelArg cases, implemented in test_modes.cpp ---
void testParseAcceptZero(void);
void testParseAcceptOne(void);
void testParseAcceptLeadingZeros(void);
void testParseRejectEmpty(void);
void testParseRejectOutOfRange(void);
void testParseRejectLargeValues(void);
void testParseRejectNonDigits(void);
void testParseRejectWhitespace(void);
void testParseChannelAccept(void);
void testParseChannelAcceptLeadingZeros(void);
void testParseChannelRejectEmpty(void);
void testParseChannelRejectOutOfRange(void);
void testParseChannelRejectNonDigits(void);
void testParseChannelRejectWhitespace(void);

// --- shared cases ---

// /mode's ?m= values are the enum values, so their numbering is a contract.
static void testModeNumbering() {
  TEST_ASSERT_EQUAL_UINT16(0, ledfx::kModePulse);
  TEST_ASSERT_EQUAL_UINT16(1, ledfx::kModeSingleChannel);
  TEST_ASSERT_EQUAL_UINT16(2, ledfx::kModeCount);
}

static void testFreshEngineIsBlank() {
  ledfx::LedEngine fresh;
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModePulse, fresh.mode());
  assertAllOff(fresh);
  // No mode has been started, so even a late tick must not emit anything.
  TEST_ASSERT_FALSE(fresh.tick(0, false));
  TEST_ASSERT_FALSE(fresh.tick(12345, false));
  assertAllOff(fresh);
}

static void testSetModeRejectsUnknownMode() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 7, 0));
  TEST_ASSERT_FALSE(engine.setMode(ledfx::kModeCount, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSingleChannel, engine.mode());
  // The rejected call must not have disturbed the running selection.
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 7);
}

static void testSetModeRejectsLargestValue() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 7, 0));
  TEST_ASSERT_FALSE(engine.setMode(65535, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSingleChannel, engine.mode());
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 7);
}

// --- kModePulse cases ---

static void testPulseStartsQuietAndRunsForever() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModePulse, engine.mode());
  TEST_ASSERT_FALSE(engine.tick(0, false));
  assertFitted(engine, 0);

  // Far past any finite run the mode is still cycling: an odd multiple of the
  // half cycle is a peak, the following even one a trough.
  const uint32_t peakTime = 999 * ledfx::kPulseHalfSlowMs;
  TEST_ASSERT_TRUE(engine.tick(peakTime, false));
  assertFitted(engine, ledfx::kMaxPwm);
  TEST_ASSERT_TRUE(engine.tick(peakTime + ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, 0);
}

static void testPulseSlowPeakAndTrough() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, 0);
}

static void testPulseFastPeakAndTroughWhenRadarHigh() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfFastMs, true));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfFastMs, true));
  assertFitted(engine, 0);
}

static void testPulseRadarShortensCycle() {
  // The same 125 ms is a full eighth of the idle half cycle but half of the
  // moving one, so motion must be brighter (and thus further into the fade).
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  engine.tick(ledfx::kPulseHalfFastMs / 2, false);
  const uint16_t idle = fittedLevel(engine);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  engine.tick(ledfx::kPulseHalfFastMs / 2, true);
  const uint16_t moving = fittedLevel(engine);

  TEST_ASSERT_EQUAL_UINT16(
      pulseLevel(ledfx::kPulseHalfFastMs / 2, ledfx::kPulseHalfSlowMs), idle);
  TEST_ASSERT_EQUAL_UINT16(
      pulseLevel(ledfx::kPulseHalfFastMs / 2, ledfx::kPulseHalfFastMs), moving);
  TEST_ASSERT_TRUE(moving > idle);
}

static void testPulseLevelQuantisesToPulseLevels() {
  const uint32_t stepMs = ledfx::kPulseHalfSlowMs / ledfx::kPulseLevels;
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_FALSE(engine.tick(stepMs, false));  // still rung 0
  TEST_ASSERT_TRUE(engine.tick(stepMs + 1, false));
  assertFitted(engine, kOneStepLevel);
  // No repeat write while the quantised level is unchanged.
  TEST_ASSERT_FALSE(engine.tick(stepMs + 5, false));
  assertFitted(engine, kOneStepLevel);
}

static void testPulseSlowSweepIsMonotonicAndBounded() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  uint16_t previous = 0;
  for (uint32_t t = 0; t <= ledfx::kPulseHalfSlowMs; t += 4) {
    engine.tick(t, false);
    const uint16_t level = fittedLevel(engine);
    TEST_ASSERT_TRUE(level >= previous);
    TEST_ASSERT_TRUE(level <= ledfx::kMaxPwm);
    previous = level;
  }
  assertFitted(engine, ledfx::kMaxPwm);

  for (uint32_t t = ledfx::kPulseHalfSlowMs;
       t <= 2 * ledfx::kPulseHalfSlowMs; t += 4) {
    engine.tick(t, false);
    const uint16_t level = fittedLevel(engine);
    TEST_ASSERT_TRUE(level <= previous);
    previous = level;
  }
  assertFitted(engine, 0);
}

static void testPulseKeepsPhaseAcrossRadarSwitch() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);  // mid-cycle peak, not a reset

  // 125 ms into the 250 ms fast half cycle is halfway down the ladder; the
  // phase must carry over instead of restarting.
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs + ledfx::kPulseHalfFastMs / 2,
                              true));
  assertFitted(engine, pulseLevel(ledfx::kPulseHalfFastMs / 2, ledfx::kPulseHalfFastMs));
}

static void testPulseUnfittedChannelsStayOff() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  for (uint32_t t = 0; t <= 2 * ledfx::kPulseHalfSlowMs;
       t += ledfx::kPulseHalfSlowMs / ledfx::kPulseLevels) {
    engine.tick(t, false);
    assertUnfitted(engine, 0);
  }
}

static void testPulseRestartResetsFrame() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, ledfx::kPulseHalfSlowMs));
  assertAllOff(engine);
  TEST_ASSERT_FALSE(engine.tick(ledfx::kPulseHalfSlowMs, false));
}

// --- kModeSingleChannel cases ---

static void testSingleChannelStartsAtSelectedChannel() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 10, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSingleChannel, engine.mode());
  assertAllOff(engine);  // cleared until the first tick
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 10);
}

static void testSingleChannelHoldsWithoutFurtherWrites() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 12, 0));
  TEST_ASSERT_TRUE(engine.tick(0, false));
  TEST_ASSERT_FALSE(engine.tick(1, false));
  TEST_ASSERT_FALSE(engine.tick(100000, false));
  assertOnlyChannelLit(engine, 12);
}

// The point of the mode: any channel index, fitted or empty, can be singled
// out. Covers both ends of the frame and the empty footprints.
static void testSingleChannelSelectsEveryValidChannel() {
  for (uint16_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
    TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, channel, 0));
    TEST_ASSERT_TRUE(engine.tick(0, false));
    assertOnlyChannelLit(engine, channel);
  }
}

static void testSingleChannelRejectsOutOfRangeChannel() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 5, 0));
  TEST_ASSERT_FALSE(engine.setMode(ledfx::kModeSingleChannel, ledfx::kChannelCount, 0));
  TEST_ASSERT_FALSE(engine.setMode(ledfx::kModeSingleChannel, 65535, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSingleChannel, engine.mode());
  // The rejected calls must not have disturbed the running selection.
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 5);
}

static void testSingleChannelSwitchMovesTheLight() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 7, 0));
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 7);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 9, 0));
  assertAllOff(engine);
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 9);
}

static void testSingleChannelIgnoresRadar() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 6, 0));
  TEST_ASSERT_TRUE(engine.tick(0, true));
  assertOnlyChannelLit(engine, 6);
  TEST_ASSERT_FALSE(engine.tick(1000, true));
  assertOnlyChannelLit(engine, 6);
}

static void testSingleChannelToPulseRestart() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSingleChannel, 8, 0));
  TEST_ASSERT_TRUE(engine.tick(0, false));
  assertOnlyChannelLit(engine, 8);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  assertAllOff(engine);
  TEST_ASSERT_FALSE(engine.tick(0, false));
  assertFitted(engine, 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(testModeNumbering);
  RUN_TEST(testFreshEngineIsBlank);
  RUN_TEST(testSetModeRejectsUnknownMode);
  RUN_TEST(testSetModeRejectsLargestValue);
  RUN_TEST(testPulseStartsQuietAndRunsForever);
  RUN_TEST(testPulseSlowPeakAndTrough);
  RUN_TEST(testPulseFastPeakAndTroughWhenRadarHigh);
  RUN_TEST(testPulseRadarShortensCycle);
  RUN_TEST(testPulseLevelQuantisesToPulseLevels);
  RUN_TEST(testPulseSlowSweepIsMonotonicAndBounded);
  RUN_TEST(testPulseKeepsPhaseAcrossRadarSwitch);
  RUN_TEST(testPulseUnfittedChannelsStayOff);
  RUN_TEST(testPulseRestartResetsFrame);
  RUN_TEST(testSingleChannelStartsAtSelectedChannel);
  RUN_TEST(testSingleChannelHoldsWithoutFurtherWrites);
  RUN_TEST(testSingleChannelSelectsEveryValidChannel);
  RUN_TEST(testSingleChannelRejectsOutOfRangeChannel);
  RUN_TEST(testSingleChannelSwitchMovesTheLight);
  RUN_TEST(testSingleChannelIgnoresRadar);
  RUN_TEST(testSingleChannelToPulseRestart);
  RUN_TEST(testParseAcceptZero);
  RUN_TEST(testParseAcceptOne);
  RUN_TEST(testParseAcceptLeadingZeros);
  RUN_TEST(testParseRejectEmpty);
  RUN_TEST(testParseRejectOutOfRange);
  RUN_TEST(testParseRejectLargeValues);
  RUN_TEST(testParseRejectNonDigits);
  RUN_TEST(testParseRejectWhitespace);
  RUN_TEST(testParseChannelAccept);
  RUN_TEST(testParseChannelAcceptLeadingZeros);
  RUN_TEST(testParseChannelRejectEmpty);
  RUN_TEST(testParseChannelRejectOutOfRange);
  RUN_TEST(testParseChannelRejectNonDigits);
  RUN_TEST(testParseChannelRejectWhitespace);
  return UNITY_END();
}
