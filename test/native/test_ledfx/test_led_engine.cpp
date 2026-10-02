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

// Channels with an LED soldered on, i.e. the ones the modes drive.
void assertFitted(const ledfx::LedEngine &e, uint16_t value) {
  assertRange(e, ledfx::kFirstChannel, ledfx::kLastChannel, value);
}

// 0..kFirstChannel-1 and kLastChannel+1..kChannelCount-1: empty footprints.
void assertUnfitted(const ledfx::LedEngine &e, uint16_t value) {
  assertRange(e, 0, ledfx::kFirstChannel - 1, value);
  assertRange(e, ledfx::kLastChannel + 1, ledfx::kChannelCount - 1, value);
}

void assertAllOff(const ledfx::LedEngine &e) {
  assertRange(e, 0, ledfx::kChannelCount - 1, 0);
}

void assertSequentialModeUnchanged() {
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSequentialFade, engine.mode());
  TEST_ASSERT_FALSE(engine.finished());
}

constexpr uint16_t kOneStepLevel =
    static_cast<uint16_t>(ledfx::kMaxPwm / ledfx::kFadeSteps);

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

// --- parseModeArg cases, implemented in test_modes.cpp ---
void testParseAcceptZero(void);
void testParseAcceptOne(void);
void testParseAcceptLeadingZeros(void);
void testParseRejectEmpty(void);
void testParseRejectOutOfRange(void);
void testParseRejectLargeValues(void);
void testParseRejectNonDigits(void);
void testParseRejectWhitespace(void);

// --- shared cases ---

// /mode's ?m= values are the enum values, so their numbering is a contract.
static void testModeNumbering() {
  TEST_ASSERT_EQUAL_UINT16(0, ledfx::kModePulse);
  TEST_ASSERT_EQUAL_UINT16(1, ledfx::kModeSequentialFade);
  TEST_ASSERT_EQUAL_UINT16(2, ledfx::kModeCount);
}

static void testFreshEngineIsBlank() {
  ledfx::LedEngine fresh;
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModePulse, fresh.mode());
  TEST_ASSERT_TRUE(fresh.finished());
  assertAllOff(fresh);
  TEST_ASSERT_FALSE(fresh.tick(0, false));
}

static void testSetModeRejectsUnknownMode() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_FALSE(engine.setMode(ledfx::kModeCount, 0));
  assertSequentialModeUnchanged();
}

static void testSetModeRejectsLargestValue() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_FALSE(engine.setMode(65535, 0));
  assertSequentialModeUnchanged();
}

// --- kModePulse cases ---

static void testPulseStartsQuietAndRunsForever() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModePulse, engine.mode());
  TEST_ASSERT_FALSE(engine.finished());
  TEST_ASSERT_FALSE(engine.tick(0, false));
  assertFitted(engine, 0);

  // Far past any finite run: pulse keeps breathing instead of finishing.
  for (uint32_t t = 0; t <= 5 * ledfx::kPulseHalfSlowMs;
       t += ledfx::kPulseHalfSlowMs / 2) {
    engine.tick(t, false);
  }
  TEST_ASSERT_FALSE(engine.finished());
}

static void testPulseSlowPeakAndTrough() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, 0);
}

static void testPulseFastPeakAndTroughWhenRadarHigh() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfFastMs, true));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfFastMs, true));
  assertFitted(engine, 0);
}

static void testPulseRadarShortensCycle() {
  // The same 125 ms is a full eighth of the idle half cycle but half of the
  // moving one, so motion must be brighter (and thus further into the fade).
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  engine.tick(ledfx::kPulseHalfFastMs / 2, false);
  const uint16_t idle = fittedLevel(engine);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
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
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_FALSE(engine.tick(stepMs, false));  // still rung 0
  TEST_ASSERT_TRUE(engine.tick(stepMs + 1, false));
  assertFitted(engine, kOneStepLevel);
  // No repeat write while the quantised level is unchanged.
  TEST_ASSERT_FALSE(engine.tick(stepMs + 5, false));
  assertFitted(engine, kOneStepLevel);
}

static void testPulseSlowSweepIsMonotonicAndBounded() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
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
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);  // mid-cycle peak, not a reset

  // 125 ms into the 250 ms fast half cycle is halfway down the ladder; the
  // phase must carry over instead of restarting.
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs + ledfx::kPulseHalfFastMs / 2,
                              true));
  assertFitted(engine, pulseLevel(ledfx::kPulseHalfFastMs / 2, ledfx::kPulseHalfFastMs));
}

static void testPulseUnfittedChannelsStayOff() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  for (uint32_t t = 0; t <= 2 * ledfx::kPulseHalfSlowMs;
       t += ledfx::kPulseHalfSlowMs / ledfx::kPulseLevels) {
    engine.tick(t, false);
    assertUnfitted(engine, 0);
  }
}

static void testPulseRestartResetsFrame() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, ledfx::kPulseHalfSlowMs));
  assertAllOff(engine);
  TEST_ASSERT_FALSE(engine.tick(ledfx::kPulseHalfSlowMs, false));
}

// --- kModeSequentialFade cases ---

static void testSetModeStartsFade() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSequentialFade, engine.mode());
  TEST_ASSERT_FALSE(engine.finished());
}

static void testStepQuantisesToFadeStep() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_FALSE(engine.tick(0, false));
  TEST_ASSERT_FALSE(engine.tick(ledfx::kFadeStepMs / 2, false));
  assertFitted(engine, 0);
}

static void testFirstStepLevel() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kFadeStepMs, false));
  assertFitted(engine, kOneStepLevel);
}

static void testHalfCyclePeak() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kHalfCycleMs, false));
  assertFitted(engine, ledfx::kMaxPwm);
}

static void testLastStepOfFall() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kCycleMs - ledfx::kFadeStepMs, false));
  assertFitted(engine, kOneStepLevel);
}

static void testRiseIsMonotonic() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  uint16_t previous[ledfx::kChannelCount];
  for (uint16_t c = 0; c < ledfx::kChannelCount; ++c) {
    previous[c] = engine.frame()[c];
  }
  for (uint32_t t = 0; t <= ledfx::kHalfCycleMs; t += ledfx::kFadeStepMs) {
    engine.tick(t, false);
    for (uint8_t c = ledfx::kFirstChannel; c <= ledfx::kLastChannel; ++c) {
      TEST_ASSERT_TRUE(engine.frame()[c] >= previous[c]);
      previous[c] = engine.frame()[c];
    }
  }
  // The sweep must actually reach the peak, otherwise monotonicity is vacuous.
  assertFitted(engine, ledfx::kMaxPwm);
}

static void testFallIsMonotonic() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  engine.tick(ledfx::kHalfCycleMs, false);
  assertFitted(engine, ledfx::kMaxPwm);

  uint16_t previous[ledfx::kChannelCount];
  for (uint16_t c = 0; c < ledfx::kChannelCount; ++c) {
    previous[c] = engine.frame()[c];
  }
  for (uint32_t t = ledfx::kHalfCycleMs; t <= ledfx::kCycleMs; t += ledfx::kFadeStepMs) {
    engine.tick(t, false);
    for (uint8_t c = ledfx::kFirstChannel; c <= ledfx::kLastChannel; ++c) {
      TEST_ASSERT_TRUE(engine.frame()[c] <= previous[c]);
      previous[c] = engine.frame()[c];
    }
  }
  assertFitted(engine, 0);
}

static void testUnfittedChannelsStayOff() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  for (uint32_t t = 0; t <= ledfx::kLastChannel * ledfx::kCycleMs;
       t += ledfx::kFadeStepMs) {
    engine.tick(t, false);
    assertUnfitted(engine, 0);
  }
}

static void testChannelPeaksOnItsOwnCycleCount() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  // Channel 6 is on its 6th fade (cycles 0..5), so it is at the peak here.
  TEST_ASSERT_TRUE(engine.tick(5 * ledfx::kCycleMs + ledfx::kHalfCycleMs, false));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kMaxPwm, engine.frame()[6]);
}

static void testChannelGoesDarkAfterItsOwnCycleCount() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  // Cycle 6 belongs to channel 7 onwards; two more fades than channel 6 allows.
  TEST_ASSERT_TRUE(engine.tick(6 * ledfx::kCycleMs + ledfx::kHalfCycleMs, false));
  TEST_ASSERT_EQUAL_UINT16(0, engine.frame()[6]);
  assertRange(engine, 7, ledfx::kLastChannel, ledfx::kMaxPwm);
}

static void testRunEndsAfterLastChannelCycles() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  // The final frame is only a change - and so worth pushing - if the previous
  // frame was still lit, which is how the firmware ticks the engine.
  TEST_ASSERT_TRUE(engine.tick(ledfx::kLastChannel * ledfx::kCycleMs -
                               ledfx::kFadeStepMs, false));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kLastChannel * ledfx::kCycleMs, false));
  assertAllOff(engine);
  TEST_ASSERT_TRUE(engine.finished());
}

static void testTickAfterFinishedStaysQuiet() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  engine.tick(ledfx::kLastChannel * ledfx::kCycleMs - ledfx::kFadeStepMs, false);
  engine.tick(ledfx::kLastChannel * ledfx::kCycleMs, false);
  TEST_ASSERT_TRUE(engine.finished());
  TEST_ASSERT_FALSE(
      engine.tick(ledfx::kLastChannel * ledfx::kCycleMs + ledfx::kCycleMs, false));
  assertAllOff(engine);
  TEST_ASSERT_TRUE(engine.finished());
}

static void testMillisecondWraparound() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0xFFFFFF00u));
  // 0x100 - 0xFFFFFF00 wraps to kHalfCycleMs.
  TEST_ASSERT_TRUE(engine.tick(0x00000100u, false));
  assertFitted(engine, ledfx::kMaxPwm);
}

static void testSequentialFadeIgnoresRadar() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kHalfCycleMs, true));
  assertFitted(engine, ledfx::kMaxPwm);
}

static void testRestartResetsFrame() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kHalfCycleMs, false));
  assertFitted(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSequentialFade, ledfx::kHalfCycleMs));
  assertAllOff(engine);
  TEST_ASSERT_FALSE(engine.tick(ledfx::kHalfCycleMs, false));
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
  RUN_TEST(testSetModeStartsFade);
  RUN_TEST(testStepQuantisesToFadeStep);
  RUN_TEST(testFirstStepLevel);
  RUN_TEST(testHalfCyclePeak);
  RUN_TEST(testLastStepOfFall);
  RUN_TEST(testRiseIsMonotonic);
  RUN_TEST(testFallIsMonotonic);
  RUN_TEST(testUnfittedChannelsStayOff);
  RUN_TEST(testChannelPeaksOnItsOwnCycleCount);
  RUN_TEST(testChannelGoesDarkAfterItsOwnCycleCount);
  RUN_TEST(testRunEndsAfterLastChannelCycles);
  RUN_TEST(testTickAfterFinishedStaysQuiet);
  RUN_TEST(testMillisecondWraparound);
  RUN_TEST(testSequentialFadeIgnoresRadar);
  RUN_TEST(testRestartResetsFrame);
  RUN_TEST(testParseAcceptZero);
  RUN_TEST(testParseAcceptOne);
  RUN_TEST(testParseAcceptLeadingZeros);
  RUN_TEST(testParseRejectEmpty);
  RUN_TEST(testParseRejectOutOfRange);
  RUN_TEST(testParseRejectLargeValues);
  RUN_TEST(testParseRejectNonDigits);
  RUN_TEST(testParseRejectWhitespace);
  return UNITY_END();
}
