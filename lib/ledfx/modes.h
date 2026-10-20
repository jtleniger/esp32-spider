#pragma once

#include <cstdint>

#include "led_layout.h"

namespace ledfx {

// Integer enumeration accepted by POST /mode?m=<value>.
enum Mode : uint16_t {
  // Smolder: a sleep/wake machine. Awake runs a slow orange/red ember crossfade
  // with a flicker, a per-eye phase offset and occasional one-eye blinks; left
  // undisturbed it grows drowsy, then each eye's ember sinks and goes out (and
  // occasionally half-opens again) until the nap ends and the eyes open one at
  // a time.
  // Motion pulls the rig into an agitation picked at random: the original
  // eye-by-eye green spread (which holds while motion lasts and fades back into
  // the embers the moment it stops), or a sweeping beam. The sweep crossfades
  // back to the embers once the radar has been quiet briefly.
  kModeSmolder = 0,
  // Lights exactly the channel named by ?c=<channel> at full brightness and
  // holds it there; every other channel stays off. Ignores the radar. Used to
  // identify a physical LED during wiring checks.
  kModeSingleChannel = 1,
  // Every channel breathes in lockstep. Runs until another mode is selected;
  // the radar input shortens the cycle while it sees motion.
  kModePulse = 2,
  // Stalker: one dim eye at a time scans around the rig, each lit in a random
  // colour; motion ramps every eye to the stuttering bright red pop, then back
  // to scanning when motion stops.
  kModeStalker = 3,
  // 4 was kModeBlink and 5 was kModeHeartbeat: retired. Mode numbers are the
  // HTTP contract, so the holes stay - 6 keeps its number.
  // Toxic: dim green bubbling base with an isolated spark that is randomly
  // orange or red; motion makes the spark faster and the flicker frantic.
  kModeToxic = 6,
  // 7 was kModeHypnotic: retired.
};

// One past the largest ?m= value parseModeArg() will even consider, used as the
// parse-time overflow bound. Retired modes leave holes below it, so
// isValidMode() - not this constant - is the authority on which values are
// live.
constexpr uint16_t kModeCount = 8;

// True for the mode numbers that still select an effect. Rejects the retired
// holes (4, 5 and 7) as well as anything at or above kModeCount. A single
// return expression keeps this a valid C++11 constexpr (the ESP32 core builds
// with -std=gnu++11, where a constexpr body cannot be a switch).
constexpr bool isValidMode(uint16_t mode) {
  return mode == kModeSmolder || mode == kModeSingleChannel ||
         mode == kModePulse || mode == kModeStalker || mode == kModeToxic;
}

// Parses the ?m= value of POST /mode. Accepts a non-empty run of decimal ASCII
// digits only; "+1", "-1", "0x0", "1 ", "" and anything at or above kModeCount
// are rejected, as are the retired holes 4, 5 and 7 (see isValidMode). Bails out
// as soon as the accumulator reaches kModeCount, so no overflow is possible for
// any kModeCount.
inline bool parseModeArg(const char *text, uint16_t &out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  uint32_t value = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(*p - '0');
    if (value >= kModeCount) {
      return false;
    }
  }
  if (!isValidMode(static_cast<uint16_t>(value))) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

// Parses the ?c= value of POST /mode, the channel selected by kModeSingleChannel.
// Same lexical rules as parseModeArg, but bounded by kChannelCount so any valid
// channel index is accepted. Bails out as soon as the accumulator reaches
// kChannelCount, so no overflow is possible.
inline bool parseChannelArg(const char *text, uint16_t &out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  uint32_t value = 0;
  for (const char *p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value = value * 10 + static_cast<uint32_t>(*p - '0');
    if (value >= kChannelCount) {
      return false;
    }
  }
  out = static_cast<uint16_t>(value);
  return true;
}

}  // namespace ledfx
