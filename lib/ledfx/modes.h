#pragma once

#include <cstdint>

#include "led_layout.h"

namespace ledfx {

// Integer enumeration accepted by POST /mode?m=<value>.
enum Mode : uint16_t {
  // Smolder: every eye runs a slow orange/red crossfade with an ember flicker
  // and its own phase offset. Motion makes the greens flash across the eyes.
  kModeSmolder = 0,
  // Lights exactly the channel named by ?c=<channel> at full brightness and
  // holds it there; every other channel stays off. Ignores the radar. Used to
  // identify a physical LED during wiring checks.
  kModeSingleChannel = 1,
  // Every channel breathes in lockstep. Runs until another mode is selected;
  // the radar input shortens the cycle while it sees motion.
  kModePulse = 2,
  // Stalker: one dim eye at a time scans around the rig; motion ramps every eye
  // up to the bright red pop, then back to scanning when motion stops.
  kModeStalker = 3,
  // Blink: a random group of eyes flashes for a short burst, occasionally a
  // double blink, then darkness until the next hashed interval. Ignores radar.
  kModeBlink = 4,
  // Heartbeat: a lub-dub pulse; beats are green when calm and red as motion
  // persists, and the beat period shortens with agitation.
  kModeHeartbeat = 5,
  // Toxic: green bubbling base with a travelling orange spark; motion makes the
  // spark faster and the flicker frantic.
  kModeToxic = 6,
  // Hypnotic: a three-colour chase rotating around the eyes; the radar speeds it
  // up.
  kModeHypnotic = 7,
};

constexpr uint16_t kModeCount = 8;

// Parses the ?m= value of POST /mode. Accepts a non-empty run of decimal ASCII
// digits only; "+1", "-1", "0x0", "1 ", "" and anything at or above kModeCount
// are rejected. Bails out as soon as the accumulator reaches kModeCount, so no
// overflow is possible for any kModeCount.
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
