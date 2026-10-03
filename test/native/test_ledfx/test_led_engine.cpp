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
// observable shape of kModeSingleChannel.
void assertOnlyChannelLit(const ledfx::LedEngine &e, uint16_t selected) {
  for (uint16_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
    TEST_ASSERT_EQUAL_UINT16(channel == selected ? ledfx::kMaxPwm : 0,
                             e.frame()[channel]);
  }
}

void assertAllOff(const ledfx::LedEngine &e) {
  assertRange(e, 0, ledfx::kChannelCount - 1, 0);
}

// kModePulse now drives the whole frame - every channel is fitted.
void assertAllChannels(const ledfx::LedEngine &e, uint16_t value) {
  assertRange(e, 0, ledfx::kChannelCount - 1, value);
}

uint16_t channelLevel(const ledfx::LedEngine &e, uint8_t channel) {
  return e.frame()[channel];
}

// The green LED of eye `eye` (`kEyes` order: braid 1's four eyes, then braid 2's).
uint16_t greenLevel(const ledfx::LedEngine &e, uint8_t eye) {
  return e.frame()[ledfx::kEyes[eye].green];
}

constexpr uint16_t kOneStepLevel =
    static_cast<uint16_t>(ledfx::kMaxPwm / ledfx::kPulseLevels);

// Level renderPulse() emits `up` ms into a half cycle of `halfMs`.
uint16_t pulseLevel(uint32_t up, uint32_t halfMs) {
  return static_cast<uint16_t>((up * ledfx::kPulseLevels / halfMs) *
                               ledfx::kMaxPwm / ledfx::kPulseLevels);
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
  TEST_ASSERT_EQUAL_UINT16(0, ledfx::kModeSmolder);
  TEST_ASSERT_EQUAL_UINT16(1, ledfx::kModeSingleChannel);
  TEST_ASSERT_EQUAL_UINT16(2, ledfx::kModePulse);
  TEST_ASSERT_EQUAL_UINT16(3, ledfx::kModeCount);
}

static void testFreshEngineIsBlank() {
  ledfx::LedEngine fresh;
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeSmolder, fresh.mode());
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

// --- wiring table cases ---

// CHANNELS.md lists 24 LEDs; kEyes must place every one of them in exactly one
// eye slot, 4 eyes per braid, 3 LEDs per eye.
static void testEyeWiringIsCompleteAndUnique() {
  bool seen[ledfx::kChannelCount] = {};
  uint8_t perBraid[ledfx::kBraidsPerRig] = {};
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    TEST_ASSERT_TRUE(w.braid < ledfx::kBraidsPerRig);
    const uint8_t channels[ledfx::kLedsPerEye] = {w.orange, w.red, w.green};
    for (uint8_t i = 0; i < ledfx::kLedsPerEye; ++i) {
      TEST_ASSERT_TRUE(channels[i] < ledfx::kChannelCount);
      TEST_ASSERT_FALSE(seen[channels[i]]);
      seen[channels[i]] = true;
    }
    ++perBraid[w.braid];
  }
  for (uint8_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
    TEST_ASSERT_TRUE(seen[channel]);
  }
  for (uint8_t braid = 0; braid < ledfx::kBraidsPerRig; ++braid) {
    TEST_ASSERT_EQUAL_UINT8(ledfx::kEyesPerBraid, perBraid[braid]);
  }
}

// --- kModePulse cases ---

static void testPulseStartsQuietAndRunsForever() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModePulse, engine.mode());
  TEST_ASSERT_FALSE(engine.tick(0, false));
  assertAllChannels(engine, 0);

  // Far past any finite run the mode is still cycling: an odd multiple of the
  // half cycle is a peak, the following even one a trough.
  const uint32_t peakTime = 999 * ledfx::kPulseHalfSlowMs;
  TEST_ASSERT_TRUE(engine.tick(peakTime, false));
  assertAllChannels(engine, ledfx::kMaxPwm);
  TEST_ASSERT_TRUE(engine.tick(peakTime + ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, 0);
}

static void testPulseSlowPeakAndTrough() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, 0);
}

static void testPulseFastPeakAndTroughWhenRadarHigh() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfFastMs, true));
  assertAllChannels(engine, ledfx::kMaxPwm);

  TEST_ASSERT_TRUE(engine.tick(2 * ledfx::kPulseHalfFastMs, true));
  assertAllChannels(engine, 0);
}

static void testPulseRadarShortensCycle() {
  // The same 125 ms is a full eighth of the idle half cycle but half of the
  // moving one, so motion must be brighter (and thus further into the fade).
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  engine.tick(ledfx::kPulseHalfFastMs / 2, false);
  const uint16_t idle = channelLevel(engine, 0);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  engine.tick(ledfx::kPulseHalfFastMs / 2, true);
  const uint16_t moving = channelLevel(engine, 0);

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
  assertAllChannels(engine, kOneStepLevel);
  // No repeat write while the quantised level is unchanged.
  TEST_ASSERT_FALSE(engine.tick(stepMs + 5, false));
  assertAllChannels(engine, kOneStepLevel);
}

static void testPulseSlowSweepIsMonotonicAndBounded() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  uint16_t previous = 0;
  for (uint32_t t = 0; t <= ledfx::kPulseHalfSlowMs; t += 4) {
    engine.tick(t, false);
    const uint16_t level = channelLevel(engine, 0);
    TEST_ASSERT_TRUE(level >= previous);
    TEST_ASSERT_TRUE(level <= ledfx::kMaxPwm);
    previous = level;
  }
  assertAllChannels(engine, ledfx::kMaxPwm);

  for (uint32_t t = ledfx::kPulseHalfSlowMs;
       t <= 2 * ledfx::kPulseHalfSlowMs; t += 4) {
    engine.tick(t, false);
    const uint16_t level = channelLevel(engine, 0);
    TEST_ASSERT_TRUE(level <= previous);
    previous = level;
  }
  assertAllChannels(engine, 0);
}

static void testPulseKeepsPhaseAcrossRadarSwitch() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, ledfx::kMaxPwm);  // mid-cycle peak, not a reset

  // 125 ms into the 250 ms fast half cycle is halfway down the ladder; the
  // phase must carry over instead of restarting.
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs + ledfx::kPulseHalfFastMs / 2,
                              true));
  assertAllChannels(
      engine, pulseLevel(ledfx::kPulseHalfFastMs / 2, ledfx::kPulseHalfFastMs));
}

// Every channel 0..kChannelCount-1 carries an LED now, so the pulse fills the
// whole frame; no channel is left off.
static void testPulseDrivesEveryChannel() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  for (uint32_t t = 0; t <= 2 * ledfx::kPulseHalfSlowMs;
       t += ledfx::kPulseHalfSlowMs / ledfx::kPulseLevels) {
    engine.tick(t, false);
    const uint16_t level = channelLevel(engine, 0);
    assertAllChannels(engine, level);
  }
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, ledfx::kMaxPwm);
}

static void testPulseRestartResetsFrame() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModePulse, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kPulseHalfSlowMs, false));
  assertAllChannels(engine, ledfx::kMaxPwm);

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

// The point of the mode: any channel index can be singled out.
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
  assertAllChannels(engine, 0);
}

// --- kModeSmolder cases ---

// With no motion the greens stay dark and the ember levels stay in range.
static void testSmolderIdleLeavesGreenOff() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  const uint32_t times[] = {0, 123, 1000, 3999, 8000};
  for (uint32_t t : times) {
    engine.tick(t, false);
    for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
      TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, eye));
      TEST_ASSERT_TRUE(ledfx::kEyes[eye].orange < ledfx::kChannelCount);
    }
    for (uint16_t channel = 0; channel < ledfx::kChannelCount; ++channel) {
      TEST_ASSERT_TRUE(channelLevel(engine, channel) <= ledfx::kMaxPwm);
    }
  }
}

// The whole point of the per-eye phase offsets: at any instant the eyes sit at
// different points of the crossfade.
static void testSmolderEyesAreNotInLockstep() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  engine.tick(500, false);
  const uint16_t first = channelLevel(engine, ledfx::kEyes[0].orange);
  bool sawDifferent = false;
  for (uint8_t eye = 1; eye < ledfx::kEyeCount; ++eye) {
    if (channelLevel(engine, ledfx::kEyes[eye].orange) != first) {
      sawDifferent = true;
    }
  }
  TEST_ASSERT_TRUE(sawDifferent);
}

// A radar rising edge starts the greens: eye 0 snaps on within the attack, the
// rest follow one at a time (kGreenSpreadMs apart).
static void testSmolderGreenSpreadsEyeByEye() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  engine.tick(0, false);
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, eye));
  }

  engine.tick(1000, true);  // rising edge: flash starts now
  TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, 0));  // attack has not elapsed

  engine.tick(1000 + ledfx::kGreenAttackMs, true);
  TEST_ASSERT_TRUE(greenLevel(engine, 0) >=
                   ledfx::kMaxPwm - ledfx::kGreenFlickerPwm);
  for (uint8_t eye = 1; eye < ledfx::kEyeCount; ++eye) {
    TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, eye));  // not started yet
  }

  engine.tick(1000 + ledfx::kGreenSpreadMs + ledfx::kGreenAttackMs, true);
  TEST_ASSERT_TRUE(greenLevel(engine, 0) >=
                   ledfx::kMaxPwm - ledfx::kGreenFlickerPwm);
  TEST_ASSERT_TRUE(greenLevel(engine, 1) >=
                   ledfx::kMaxPwm - ledfx::kGreenFlickerPwm);
  TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, 2));  // still waiting
}

// Each eye fades out after attack + hold + fade and hands back to the embers.
static void testSmolderGreenFadesAndEmbersResume() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  engine.tick(0, false);
  engine.tick(1000, true);

  const uint32_t eye0Total =
      ledfx::kGreenAttackMs + ledfx::kGreenHoldMs + ledfx::kGreenFadeMs;
  engine.tick(1000 + eye0Total, true);
  TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, 0));
  TEST_ASSERT_TRUE(channelLevel(engine, ledfx::kEyes[0].orange) > 0 ||
                   channelLevel(engine, ledfx::kEyes[0].red) > 0);

  // The last eye starts kGreenSpreadMs * (kEyeCount-1) later, so it is still
  // lit while eye 0 has already handed back.
  const uint32_t lastEyeStart =
      1000 + static_cast<uint32_t>(ledfx::kEyeCount - 1) * ledfx::kGreenSpreadMs;
  engine.tick(lastEyeStart + ledfx::kGreenAttackMs, true);
  TEST_ASSERT_TRUE(greenLevel(engine, ledfx::kEyeCount - 1) >=
                   ledfx::kMaxPwm - ledfx::kGreenFlickerPwm);

  // Once every eye is done the greens are all dark again.
  const uint32_t allDone = lastEyeStart + eye0Total;
  engine.tick(allDone, true);
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, eye));
  }
}

// Re-issuing the mode drops any in-flight flash and parks the frame.
static void testSmolderRestartResetsGreen() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  engine.tick(1000, true);
  engine.tick(1000 + ledfx::kGreenAttackMs, true);
  TEST_ASSERT_TRUE(greenLevel(engine, 0) > 0);

  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 1000));
  assertAllOff(engine);
  engine.tick(1000, false);  // fresh trigger state, still no motion
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    TEST_ASSERT_EQUAL_UINT16(0, greenLevel(engine, eye));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(testModeNumbering);
  RUN_TEST(testFreshEngineIsBlank);
  RUN_TEST(testSetModeRejectsUnknownMode);
  RUN_TEST(testSetModeRejectsLargestValue);
  RUN_TEST(testEyeWiringIsCompleteAndUnique);
  RUN_TEST(testPulseStartsQuietAndRunsForever);
  RUN_TEST(testPulseSlowPeakAndTrough);
  RUN_TEST(testPulseFastPeakAndTroughWhenRadarHigh);
  RUN_TEST(testPulseRadarShortensCycle);
  RUN_TEST(testPulseLevelQuantisesToPulseLevels);
  RUN_TEST(testPulseSlowSweepIsMonotonicAndBounded);
  RUN_TEST(testPulseKeepsPhaseAcrossRadarSwitch);
  RUN_TEST(testPulseDrivesEveryChannel);
  RUN_TEST(testPulseRestartResetsFrame);
  RUN_TEST(testSingleChannelStartsAtSelectedChannel);
  RUN_TEST(testSingleChannelHoldsWithoutFurtherWrites);
  RUN_TEST(testSingleChannelSelectsEveryValidChannel);
  RUN_TEST(testSingleChannelRejectsOutOfRangeChannel);
  RUN_TEST(testSingleChannelSwitchMovesTheLight);
  RUN_TEST(testSingleChannelIgnoresRadar);
  RUN_TEST(testSingleChannelToPulseRestart);
  RUN_TEST(testSmolderIdleLeavesGreenOff);
  RUN_TEST(testSmolderEyesAreNotInLockstep);
  RUN_TEST(testSmolderGreenSpreadsEyeByEye);
  RUN_TEST(testSmolderGreenFadesAndEmbersResume);
  RUN_TEST(testSmolderRestartResetsGreen);
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
