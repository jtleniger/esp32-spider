#pragma once

#include <cstdint>

namespace ledfx {

// Integer enumeration accepted by POST /mode?m=<value>.
enum Mode : uint16_t {
  // Every fitted channel breathes in lockstep. Runs until another mode is
  // selected; the radar input shortens the cycle while it sees motion.
  kModePulse = 0,
  // Every fitted channel fades in and out channelNumber times, then the run ends.
  kModeSequentialFade = 1,
};

constexpr uint16_t kModeCount = 2;

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

}  // namespace ledfx
