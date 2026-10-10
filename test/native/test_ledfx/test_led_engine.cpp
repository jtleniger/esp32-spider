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

// Any channel at all above zero (the frame is not dark).
bool anyLit(const ledfx::LedEngine &e) {
  for (uint16_t c = 0; c < ledfx::kChannelCount; ++c) {
    if (e.frame()[c] > 0) return true;
  }
  return false;
}

// Any green LED above zero (the agitation-only colour).
bool anyGreenLit(const ledfx::LedEngine &e) {
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if (greenLevel(e, eye) > 0) return true;
  }
  return false;
}

// Any orange or red LED above zero (the ember colours).
bool anyWarmLit(const ledfx::LedEngine &e) {
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if (channelLevel(e, ledfx::kEyes[eye].orange) > 0 ||
        channelLevel(e, ledfx::kEyes[eye].red) > 0) {
      return true;
    }
  }
  return false;
}

// Level renderPulse() emits `up` ms into a half cycle of `halfMs`.
uint16_t pulseLevel(uint32_t up, uint32_t halfMs) {
  return static_cast<uint16_t>((up * ledfx::kPulseLevels / halfMs) *
                               ledfx::kMaxPwm / ledfx::kPulseLevels);
}

// Eye whose brightest single LED is the brightest on the rig. Unambiguous for
// the chase tests, where the head dominates.
uint8_t brightestEye(const ledfx::LedEngine &e) {
  uint8_t best = 0;
  uint16_t bestLevel = 0;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    uint16_t level = e.frame()[w.orange];
    if (e.frame()[w.red] > level) level = e.frame()[w.red];
    if (e.frame()[w.green] > level) level = e.frame()[w.green];
    if (level > bestLevel) { bestLevel = level; best = eye; }
  }
  return best;
}

// Eye with the brightest spark LED (max of its orange and red); used by the
// toxic tests, where the green base would otherwise dominate a per-eye maximum.
uint8_t brightestSparkEye(const ledfx::LedEngine &e) {
  uint8_t best = 0;
  uint16_t bestLevel = 0;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    uint16_t level = e.frame()[w.orange];
    if (e.frame()[w.red] > level) level = e.frame()[w.red];
    if (level > bestLevel) { bestLevel = level; best = eye; }
  }
  return best;
}

// Which of an eye's three LEDs is lit (0 orange, 1 red, 2 green). Only
// meaningful while exactly one is lit, which every single-colour mode ensures.
uint8_t litSlot(const ledfx::LedEngine &e, uint8_t eye) {
  const ledfx::EyeWiring &w = ledfx::kEyes[eye];
  if (e.frame()[w.orange] > 0) return 0;
  if (e.frame()[w.red] > 0) return 1;
  return 2;
}

// True when the spark head (the brightest orange/red eye) rides red.
bool sparkHeadIsRed(const ledfx::LedEngine &e) {
  const ledfx::EyeWiring &w = ledfx::kEyes[brightestSparkEye(e)];
  return e.frame()[w.red] > e.frame()[w.orange];
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
void testParseAcceptNewModes(void);
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
  TEST_ASSERT_EQUAL_UINT16(3, ledfx::kModeStalker);
  TEST_ASSERT_EQUAL_UINT16(4, ledfx::kModeBlink);
  TEST_ASSERT_EQUAL_UINT16(5, ledfx::kModeHeartbeat);
  TEST_ASSERT_EQUAL_UINT16(6, ledfx::kModeToxic);
  TEST_ASSERT_EQUAL_UINT16(7, ledfx::kModeHypnotic);
  TEST_ASSERT_EQUAL_UINT16(8, ledfx::kModeCount);
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

// kModeSmolder must hold pure single-colour stretches: through eye 0's red hold
// the orange LED is fully dark and red is bright and flickering; through the
// orange hold it is the mirror image. Flicker is proportional to level, so the
// dark colour shows no flicker at all and the lit one swings by a large, visible
// fraction of the flicker amplitude.
static void testSmolderHoldsAreSingleColourAndFlicker() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeSmolder, 0, 0));
  const uint8_t orange = ledfx::kEyes[0].orange;
  const uint8_t red = ledfx::kEyes[0].red;

  // Eye 0's phase offset is zero, so t < kSmolderDwellMs parks it on the red
  // hold: red at full power, orange completely off.
  uint16_t redMin = ledfx::kMaxPwm;
  uint16_t redMax = 0;
  for (uint32_t t = 0; t < ledfx::kSmolderDwellMs;
       t += ledfx::kSmolderFlickerStepMs) {
    engine.tick(t, false);
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, orange));
    const uint16_t r = channelLevel(engine, red);
    if (r < redMin) redMin = r;
    if (r > redMax) redMax = r;
  }
  TEST_ASSERT_EQUAL_UINT16(ledfx::kMaxPwm, redMax);  // the hold reaches full
  TEST_ASSERT_TRUE(redMin >= ledfx::kMaxPwm - ledfx::kSmolderFlickerPwm);
  TEST_ASSERT_TRUE(redMax - redMin >= ledfx::kSmolderFlickerPwm / 2);

  // The orange hold is the mirror image: red is off, orange is bright.
  const uint32_t orangeHold = ledfx::kSmolderDwellMs + ledfx::kSmolderFadeMs;
  uint16_t orangeMin = ledfx::kMaxPwm;
  uint16_t orangeMax = 0;
  for (uint32_t t = orangeHold; t < orangeHold + ledfx::kSmolderDwellMs;
       t += ledfx::kSmolderFlickerStepMs) {
    engine.tick(t, false);
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, red));
    const uint16_t o = channelLevel(engine, orange);
    if (o < orangeMin) orangeMin = o;
    if (o > orangeMax) orangeMax = o;
  }
  TEST_ASSERT_EQUAL_UINT16(ledfx::kMaxPwm, orangeMax);
  TEST_ASSERT_TRUE(orangeMin >= ledfx::kMaxPwm - ledfx::kSmolderFlickerPwm);
  TEST_ASSERT_TRUE(orangeMax - orangeMin >= ledfx::kSmolderFlickerPwm / 2);
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

// Nothing nods off inside the first kSmolderSleepAfterMs: the engine stays awake
// and warm, with no green anywhere.
static void testSmolderStaysAwakeBeforeSleepWindow() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  e.tick(0, false);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAwake, e.smolderState());
  e.tick(ledfx::kSmolderSleepAfterMs - 1, false);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAwake, e.smolderState());
  TEST_ASSERT_TRUE(anyLit(e));
  TEST_ASSERT_FALSE(anyGreenLit(e));
}

// Undisturbed, the rig does fall asleep by the deadline, and once every eyelid
// gesture has run the frame is fully dark.
static void testSmolderFallsAsleepWhenUndisturbed() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  for (uint32_t t = 0; t <= ledfx::kSmolderSleepDeadlineMs; t += 250) {
    e.tick(t, false);
  }
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAsleep, e.smolderState());

  const uint32_t dark =
      ledfx::kSmolderSleepDeadlineMs + ledfx::kSleepFallMaxMs +
      ledfx::kSleepGestureMs;
  for (uint32_t t = ledfx::kSmolderSleepDeadlineMs + 250; t <= dark; t += 250) {
    e.tick(t, false);
  }
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAsleep, e.smolderState());
  TEST_ASSERT_FALSE(anyLit(e));
}

// A nap lasts a random kSmolderSleepMinMs..kSmolderSleepMaxMs and then the
// embers come back.
static void testSmolderNapsWithinBoundsThenWakes() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  uint32_t tOnset = 0;
  uint32_t tWake = 0;
  bool asleep = false;
  for (uint32_t t = 0;
       t <= ledfx::kSmolderSleepDeadlineMs + ledfx::kSmolderSleepMaxMs + 250;
       t += 250) {
    e.tick(t, false);
    if (!asleep && e.smolderState() == ledfx::kSmolderAsleep) {
      asleep = true;
      tOnset = t;
    } else if (asleep && e.smolderState() == ledfx::kSmolderAwake) {
      tWake = t;
      break;
    }
  }
  TEST_ASSERT_TRUE(asleep);
  TEST_ASSERT_TRUE(tWake > tOnset);
  const uint32_t nap = tWake - tOnset;
  TEST_ASSERT_TRUE(nap >= ledfx::kSmolderSleepMinMs - 250);
  TEST_ASSERT_TRUE(nap <= ledfx::kSmolderSleepMaxMs + 250);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAwake, e.smolderState());
  TEST_ASSERT_TRUE(anyLit(e));
  TEST_ASSERT_FALSE(anyGreenLit(e));
}

// While asleep the embers still surface: after the eyelid gestures have run the
// rig is dark, then every so often a group of eyes half-opens (warm, never
// green) and shuts again.
static void testSmolderHalfOpensEyesWhileAsleep() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  bool sawHalfOpen = false;
  bool sawDarkAgain = false;
  uint32_t onset = 0;
  uint8_t previous = ledfx::kSmolderAwake;
  for (uint32_t t = 0; t <= 1200000; t += 250) {
    e.tick(t, false);
    const uint8_t state = e.smolderState();
    if (state == ledfx::kSmolderAsleep && previous != ledfx::kSmolderAsleep) {
      onset = t;
    }
    previous = state;
    // Past the fall spread every wake-up inside a nap is a half-open event.
    if (state == ledfx::kSmolderAsleep &&
        t >= onset + ledfx::kSleepFallMaxMs + ledfx::kSleepGestureMs) {
      if (anyLit(e)) {
        TEST_ASSERT_TRUE(anyWarmLit(e));
        TEST_ASSERT_FALSE(anyGreenLit(e));
        sawHalfOpen = true;
      } else if (sawHalfOpen) {
        sawDarkAgain = true;
      }
    }
  }
  TEST_ASSERT_TRUE(sawHalfOpen);
  TEST_ASSERT_TRUE(sawDarkAgain);
}

// Motion during a nap wakes the rig straight into agitation.
static void testSmolderMotionWhileAsleepAgitates() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  uint32_t t = 0;
  for (; t <= ledfx::kSmolderSleepDeadlineMs; t += 250) {
    e.tick(t, false);
    if (e.smolderState() == ledfx::kSmolderAsleep) {
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAsleep, e.smolderState());

  e.tick(t + 250, true);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAgitated, e.smolderState());
  TEST_ASSERT_TRUE(anyGreenLit(e));
  TEST_ASSERT_FALSE(anyWarmLit(e));
}

// Agitation is green-only and moving: warm channels never light, and the greens
// keep changing which eyes are on.
static void testSmolderAgitationIsGreenOnlyAndAlive() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  e.tick(0, false);
  bool litEyes[ledfx::kEyeCount] = {};
  uint32_t last = 0;
  for (uint32_t t = 20; t <= 900; t += 20) {
    e.tick(t, true);
    last = t;
    TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAgitated, e.smolderState());
    TEST_ASSERT_FALSE(anyWarmLit(e));
    for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
      if (greenLevel(e, eye) > 0) {
        litEyes[eye] = true;
      }
    }
  }
  uint8_t distinct = 0;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if (litEyes[eye]) ++distinct;
  }
  TEST_ASSERT_TRUE(distinct >= 2);
  TEST_ASSERT_TRUE(anyGreenLit(e));

  e.tick(last + ledfx::kAgitationHoldMs + 100, false);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAwake, e.smolderState());
}

// The hold keeps the animation up until the radar has been quiet that long.
static void testSmolderAgitationEndsAfterMotionStops() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  e.tick(0, false);
  uint32_t last = 0;
  for (uint32_t t = 100; t <= 300; t += 100) {
    e.tick(t, true);
    last = t;
  }
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAgitated, e.smolderState());

  e.tick(last + ledfx::kAgitationHoldMs - 1, false);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAgitated, e.smolderState());

  e.tick(last + ledfx::kAgitationHoldMs, false);
  TEST_ASSERT_EQUAL_UINT8(ledfx::kSmolderAwake, e.smolderState());
  TEST_ASSERT_TRUE(anyWarmLit(e));
  TEST_ASSERT_FALSE(anyGreenLit(e));
}

// Repeated episodes do not always pick the same animation.
static void testSmolderAgitationKindsVary() {
  ledfx::LedEngine e;
  TEST_ASSERT_TRUE(e.setMode(ledfx::kModeSmolder, 0, 0));
  bool seen[ledfx::kAgitationKindCount] = {};
  uint32_t t = 100000;
  for (int episode = 0; episode < 40; ++episode) {
    e.tick(t, true);
    const uint8_t kind = e.agitationKind();
    TEST_ASSERT_TRUE(kind < ledfx::kAgitationKindCount);
    seen[kind] = true;
    while (e.smolderState() == ledfx::kSmolderAgitated) {
      t += 250;
      e.tick(t, false);
    }
    t += 1000;
  }
  uint8_t distinct = 0;
  for (uint8_t kind = 0; kind < ledfx::kAgitationKindCount; ++kind) {
    if (seen[kind]) ++distinct;
  }
  TEST_ASSERT_TRUE(distinct >= 2);
}

// --- kModeStalker cases ---

static void testStalkerScansDimThenPopsRed() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeStalker, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeStalker, engine.mode());
  TEST_ASSERT_TRUE(engine.tick(0, false));
  // Only eye 0 is lit at t=0, dim, in a single colour of its own.
  const ledfx::EyeWiring &e0 = ledfx::kEyes[0];
  const uint16_t o0 = channelLevel(engine, e0.orange);
  const uint16_t r0 = channelLevel(engine, e0.red);
  const uint16_t g0 = channelLevel(engine, e0.green);
  TEST_ASSERT_EQUAL_UINT8(1, (o0 > 0) + (r0 > 0) + (g0 > 0));
  TEST_ASSERT_TRUE(o0 <= ledfx::kStalkerDimPwm + ledfx::kStalkerFlickerPwm);
  TEST_ASSERT_TRUE(g0 <= ledfx::kStalkerDimPwm);  // green is trimmed
  for (uint8_t eye = 2; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.orange));
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.red));
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.green));
  }
  // Motion ramps every eye to the stuttering bright red pop and kills the scan.
  engine.tick(ledfx::kStalkerPopMs, true);
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    TEST_ASSERT_TRUE(channelLevel(engine, w.red) >=
                     ledfx::kMaxPwm - ledfx::kStalkerPopFlickerPwm);
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.orange));
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.green));
  }
}

static void testStalkerReleaseReturnsToScan() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeStalker, 0, 0));
  engine.tick(0, false);
  engine.tick(ledfx::kStalkerPopMs, true);  // full pop
  engine.tick(ledfx::kStalkerPopMs + ledfx::kStalkerReleaseMs, false);
  // Alert is back to 0: the far eye 3 is dark again, the scan head is lit.
  TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[3].orange));
  TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[3].red));
  TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[3].green));
}

// The palette reshuffles each lap, so the same eye does not scan in the same
// colour forever.
static void testStalkerColourRandomisesPerEye() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeStalker, 0, 0));
  uint8_t seen[ledfx::kEyeCount] = {};
  const uint32_t span =
      12u * ledfx::kEyeCount * static_cast<uint32_t>(ledfx::kStalkerStepMs);
  for (uint32_t t = 0; t <= span; t += ledfx::kStalkerStepMs / 4) {
    engine.tick(t, false);
    const uint8_t eye = brightestEye(engine);
    seen[eye] |= static_cast<uint8_t>(1u << litSlot(engine, eye));
  }
  bool varied = false;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if ((seen[eye] & (seen[eye] - 1u)) != 0) {
      varied = true;
    }
  }
  TEST_ASSERT_TRUE(varied);
}

// --- kModeBlink cases ---

static void testBlinkIsDeterministicAndBounded() {
  ledfx::LedEngine a;
  ledfx::LedEngine b;
  TEST_ASSERT_TRUE(a.setMode(ledfx::kModeBlink, 0, 0));
  TEST_ASSERT_TRUE(b.setMode(ledfx::kModeBlink, 0, 0));
  for (uint32_t t = 0; t <= 20000; t += 37) {
    a.tick(t, false);
    b.tick(t, false);
    for (uint16_t c = 0; c < ledfx::kChannelCount; ++c) {
      TEST_ASSERT_EQUAL_UINT16(a.frame()[c], b.frame()[c]);
      TEST_ASSERT_TRUE(a.frame()[c] <= ledfx::kMaxPwm);
    }
  }
}

static void testBlinkLightsWholeLedsInAGroup() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeBlink, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(0, false));  // first burst starts here
  uint8_t litEyes = 0;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    const uint16_t o = channelLevel(engine, w.orange);
    const uint16_t r = channelLevel(engine, w.red);
    const uint16_t g = channelLevel(engine, w.green);
    if (o > 0 || r > 0 || g > 0) {
      ++litEyes;
      // Exactly one colour per eye, near full brightness but sputtering.
      TEST_ASSERT_EQUAL_UINT8(1, (o > 0) + (r > 0) + (g > 0));
      const uint16_t level = o > 0 ? o : (r > 0 ? r : g);
      TEST_ASSERT_TRUE(level >= ledfx::kBlinkLevelPwm - ledfx::kBlinkFlickerPwm);
      TEST_ASSERT_TRUE(level <= ledfx::kBlinkLevelPwm);
    }
  }
  TEST_ASSERT_TRUE(litEyes >= 1);
  TEST_ASSERT_TRUE(litEyes <= ledfx::kEyeCount);
}

// Each blink picks a fresh colour for each blinking eye, so over many blinks an
// eye is seen in more than one colour.
static void testBlinkColourRandomisesPerBlink() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeBlink, 0, 0));
  uint8_t seen[ledfx::kEyeCount] = {};
  for (uint32_t t = 0; t <= 200000; t += 7) {
    engine.tick(t, false);
    for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
      const ledfx::EyeWiring &w = ledfx::kEyes[eye];
      const uint16_t o = channelLevel(engine, w.orange);
      const uint16_t r = channelLevel(engine, w.red);
      const uint16_t g = channelLevel(engine, w.green);
      if ((o > 0) + (r > 0) + (g > 0) == 1) {
        seen[eye] |= static_cast<uint8_t>(1u << litSlot(engine, eye));
      }
    }
  }
  bool varied = false;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if ((seen[eye] & (seen[eye] - 1u)) != 0) {
      varied = true;
    }
  }
  TEST_ASSERT_TRUE(varied);
}

// At kBlinkOnMs the first pulse has ended (single blink) or is in its dark gap
// (double blink), so the rig is always dark there.
static void testBlinkGoesDarkAfterTheOnWindow() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeBlink, 0, 0));
  TEST_ASSERT_TRUE(engine.tick(0, false));
  TEST_ASSERT_TRUE(engine.tick(ledfx::kBlinkOnMs, false));
  assertAllOff(engine);
}

// --- kModeHeartbeat cases ---

static void testHeartbeatLubDubGreenWhenCalm() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeHeartbeat, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeHeartbeat, engine.mode());
  TEST_ASSERT_FALSE(engine.tick(0, false));
  assertAllOff(engine);
  // Lub peak: bright (but trimmed and flickering) green, no red, no orange.
  const uint16_t greenPeak = static_cast<uint16_t>(
      static_cast<uint32_t>(ledfx::kMaxPwm) * ledfx::kHeartbeatGreenPermille /
      1000);
  engine.tick(ledfx::kHeartbeatThumpMs / 2, false);
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    const uint16_t green = channelLevel(engine, w.green);
    TEST_ASSERT_TRUE(green >= greenPeak - ledfx::kHeartbeatFlickerPwm);
    TEST_ASSERT_TRUE(green <= greenPeak + ledfx::kHeartbeatFlickerPwm);
    TEST_ASSERT_TRUE(green < ledfx::kMaxPwm);  // green is trimmed
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.red));
    TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, w.orange));
  }
  // Dub peak arrives kHeartbeatDubDelayMs later: the softer second thump
  // (kHeartbeatDubGainPermille of the lub), still pure green at rest.
  engine.tick(ledfx::kHeartbeatDubDelayMs + ledfx::kHeartbeatThumpMs / 2, false);
  const uint16_t dub = channelLevel(engine, ledfx::kEyes[0].green);
  TEST_ASSERT_TRUE(dub > 0);
  TEST_ASSERT_TRUE(dub < greenPeak);
  TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[0].red));
  TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[0].orange));
}

static void testHeartbeatEscalatesToRedAndSpeedsUp() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeHeartbeat, 0, 0));
  engine.tick(0, false);
  engine.tick(ledfx::kHeartbeatEscalateMs, true);  // full agitation
  bool sawRed = false;
  uint32_t t = ledfx::kHeartbeatEscalateMs;
  for (uint32_t i = 0; i <= ledfx::kHeartbeatFastPeriodMs; i += 5) {
    t += 5;
    engine.tick(t, true);
    const uint16_t red = channelLevel(engine, ledfx::kEyes[0].red);
    if (red == ledfx::kMaxPwm) {
      sawRed = true;
      TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[0].green));
    }
  }
  TEST_ASSERT_TRUE(sawRed);
}

static void testHeartbeatCoolsBackToGreen() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeHeartbeat, 0, 0));
  engine.tick(0, false);
  engine.tick(ledfx::kHeartbeatEscalateMs, true);  // fully agitated
  uint32_t t = ledfx::kHeartbeatEscalateMs + ledfx::kHeartbeatCoolMs;
  engine.tick(t, false);  // agitated to the floor
  bool sawGreenOnly = false;
  for (uint32_t i = 0; i <= ledfx::kHeartbeatCalmPeriodMs; i += 5) {
    t += 5;
    engine.tick(t, false);
    const uint16_t green = channelLevel(engine, ledfx::kEyes[0].green);
    if (green > 0) {
      sawGreenOnly = true;
      TEST_ASSERT_EQUAL_UINT16(0, channelLevel(engine, ledfx::kEyes[0].red));
    }
  }
  TEST_ASSERT_TRUE(sawGreenOnly);
}

// --- kModeToxic cases ---

static void testToxicKeepsGreenBase() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeToxic, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeToxic, engine.mode());
  TEST_ASSERT_TRUE(engine.tick(0, false));
  uint8_t sparkEyes = 0;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    const ledfx::EyeWiring &w = ledfx::kEyes[eye];
    TEST_ASSERT_TRUE(channelLevel(engine, w.green) > 0);  // base always glows
    const uint16_t o = channelLevel(engine, w.orange);
    const uint16_t r = channelLevel(engine, w.red);
    TEST_ASSERT_FALSE(o > 0 && r > 0);  // the spark is one colour, never both
    if (o > 0 || r > 0) {
      ++sparkEyes;
    }
  }
  TEST_ASSERT_EQUAL_UINT8(1, sparkEyes);  // the spark is isolated to one eye
}

static void testToxicSparkTravels() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeToxic, 0, 0));
  engine.tick(0, false);
  TEST_ASSERT_EQUAL_UINT8(0, brightestSparkEye(engine));  // head on eye 0
  engine.tick(ledfx::kToxicSparkIdleStepMs, false);
  TEST_ASSERT_EQUAL_UINT8(1, brightestSparkEye(engine));  // stepped to eye 1
}

static void testToxicMotionSpeedsSpark() {
  ledfx::LedEngine idle;
  ledfx::LedEngine frantic;
  TEST_ASSERT_TRUE(idle.setMode(ledfx::kModeToxic, 0, 0));
  TEST_ASSERT_TRUE(frantic.setMode(ledfx::kModeToxic, 0, 0));
  idle.tick(0, false);
  frantic.tick(0, false);
  idle.tick(ledfx::kToxicMotionRampMs, false);
  frantic.tick(ledfx::kToxicMotionRampMs, true);  // full frantic
  TEST_ASSERT_TRUE(brightestSparkEye(frantic) > brightestSparkEye(idle));
}

// The spark picks orange or red at random each revolution, so over a few laps
// both colours show up.
static void testToxicSparkColourRandomises() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeToxic, 0, 0));
  bool sawOrange = false;
  bool sawRed = false;
  const uint32_t span = 6u * ledfx::kEyeCount *
                        static_cast<uint32_t>(ledfx::kToxicSparkIdleStepMs);
  for (uint32_t t = 0; t <= span; t += 17) {
    engine.tick(t, false);
    const ledfx::EyeWiring &w = ledfx::kEyes[brightestSparkEye(engine)];
    if (channelLevel(engine, w.orange) == 0 &&
        channelLevel(engine, w.red) == 0) {
      continue;  // no spark sampled this frame
    }
    if (sparkHeadIsRed(engine)) {
      sawRed = true;
    } else {
      sawOrange = true;
    }
  }
  TEST_ASSERT_TRUE(sawOrange);
  TEST_ASSERT_TRUE(sawRed);
}

// --- kModeHypnotic cases ---

static void testHypnoticChaseRotatesOneColourPerEye() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeHypnotic, 0, 0));
  TEST_ASSERT_EQUAL_UINT16(ledfx::kModeHypnotic, engine.mode());
  TEST_ASSERT_TRUE(engine.tick(0, false));
  // Head on eye 0 at full, in a single colour.
  TEST_ASSERT_EQUAL_UINT8(0, brightestEye(engine));
  const ledfx::EyeWiring &w0 = ledfx::kEyes[0];
  const uint16_t o0 = channelLevel(engine, w0.orange);
  const uint16_t r0 = channelLevel(engine, w0.red);
  const uint16_t g0 = channelLevel(engine, w0.green);
  TEST_ASSERT_EQUAL_UINT8(1, (o0 > 0) + (r0 > 0) + (g0 > 0));
  const uint16_t head0 = o0 > 0 ? o0 : (r0 > 0 ? r0 : g0);
  TEST_ASSERT_TRUE(head0 >= ledfx::kHypnoHeadPwm - ledfx::kHypnoFlickerPwm);

  // Over a full revolution every eye takes its turn as the head, and no eye
  // ever shows more than one colour.
  bool seen[ledfx::kEyeCount] = {};
  uint32_t t = 0;
  for (uint32_t i = 0; i < 2500; ++i) {
    t += 20;
    engine.tick(t, false);
    for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
      const ledfx::EyeWiring &w = ledfx::kEyes[eye];
      const uint16_t o = channelLevel(engine, w.orange);
      const uint16_t r = channelLevel(engine, w.red);
      const uint16_t g = channelLevel(engine, w.green);
      TEST_ASSERT_TRUE((o > 0) + (r > 0) + (g > 0) <= 1);
    }
    seen[brightestEye(engine)] = true;
  }
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    TEST_ASSERT_TRUE(seen[eye]);
  }
}

// The palette is reshuffled each revolution, so an eye seen as the head in
// different laps is not always the same colour.
static void testHypnoticColoursRandomiseOverTime() {
  TEST_ASSERT_TRUE(engine.setMode(ledfx::kModeHypnotic, 0, 0));
  uint8_t seen[ledfx::kEyeCount] = {};
  for (uint32_t t = 0; t <= 60000; t += 11) {
    engine.tick(t, false);
    for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
      const ledfx::EyeWiring &w = ledfx::kEyes[eye];
      const uint16_t o = channelLevel(engine, w.orange);
      const uint16_t r = channelLevel(engine, w.red);
      const uint16_t g = channelLevel(engine, w.green);
      if ((o > 0) + (r > 0) + (g > 0) == 1) {
        seen[eye] |= static_cast<uint8_t>(1u << litSlot(engine, eye));
      }
    }
  }
  bool varied = false;
  for (uint8_t eye = 0; eye < ledfx::kEyeCount; ++eye) {
    if ((seen[eye] & (seen[eye] - 1u)) != 0) {
      varied = true;
    }
  }
  TEST_ASSERT_TRUE(varied);
}

static void testHypnoticRadarSpeedsTheChase() {
  ledfx::LedEngine slow;
  ledfx::LedEngine fast;
  TEST_ASSERT_TRUE(slow.setMode(ledfx::kModeHypnotic, 0, 0));
  TEST_ASSERT_TRUE(fast.setMode(ledfx::kModeHypnotic, 0, 0));
  slow.tick(0, false);
  fast.tick(0, false);
  uint8_t lastSlow = brightestEye(slow);
  uint8_t lastFast = brightestEye(fast);
  uint32_t slowSteps = 0;
  uint32_t fastSteps = 0;
  uint32_t t = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    t += 25;
    slow.tick(t, false);
    fast.tick(t, true);
    const uint8_t s = brightestEye(slow);
    const uint8_t f = brightestEye(fast);
    if (s != lastSlow) {
      ++slowSteps;
      lastSlow = s;
    }
    if (f != lastFast) {
      ++fastSteps;
      lastFast = f;
    }
  }
  TEST_ASSERT_TRUE(fastSteps > slowSteps);
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
  RUN_TEST(testSmolderHoldsAreSingleColourAndFlicker);
  RUN_TEST(testSmolderEyesAreNotInLockstep);
  RUN_TEST(testSmolderStaysAwakeBeforeSleepWindow);
  RUN_TEST(testSmolderFallsAsleepWhenUndisturbed);
  RUN_TEST(testSmolderNapsWithinBoundsThenWakes);
  RUN_TEST(testSmolderHalfOpensEyesWhileAsleep);
  RUN_TEST(testSmolderMotionWhileAsleepAgitates);
  RUN_TEST(testSmolderAgitationIsGreenOnlyAndAlive);
  RUN_TEST(testSmolderAgitationEndsAfterMotionStops);
  RUN_TEST(testSmolderAgitationKindsVary);
  RUN_TEST(testStalkerScansDimThenPopsRed);
  RUN_TEST(testStalkerReleaseReturnsToScan);
  RUN_TEST(testStalkerColourRandomisesPerEye);
  RUN_TEST(testBlinkIsDeterministicAndBounded);
  RUN_TEST(testBlinkLightsWholeLedsInAGroup);
  RUN_TEST(testBlinkColourRandomisesPerBlink);
  RUN_TEST(testBlinkGoesDarkAfterTheOnWindow);
  RUN_TEST(testHeartbeatLubDubGreenWhenCalm);
  RUN_TEST(testHeartbeatEscalatesToRedAndSpeedsUp);
  RUN_TEST(testHeartbeatCoolsBackToGreen);
  RUN_TEST(testToxicKeepsGreenBase);
  RUN_TEST(testToxicSparkTravels);
  RUN_TEST(testToxicMotionSpeedsSpark);
  RUN_TEST(testToxicSparkColourRandomises);
  RUN_TEST(testHypnoticChaseRotatesOneColourPerEye);
  RUN_TEST(testHypnoticColoursRandomiseOverTime);
  RUN_TEST(testHypnoticRadarSpeedsTheChase);
  RUN_TEST(testParseAcceptZero);
  RUN_TEST(testParseAcceptOne);
  RUN_TEST(testParseAcceptLeadingZeros);
  RUN_TEST(testParseRejectEmpty);
  RUN_TEST(testParseRejectOutOfRange);
  RUN_TEST(testParseRejectLargeValues);
  RUN_TEST(testParseAcceptNewModes);
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
