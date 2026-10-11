#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>

namespace ledfx {

// Clock handling for the status document. SNTP (main.cpp) sets the system
// clock; this turns it into the bytes the rig serves. Pure formatting, so the
// host suite can assert the stamps.

// "YYYY-MM-DDTHH:MM:SSZ" plus the NUL.
constexpr size_t kUtcStampCapacity = 21;

// The rig has no RTC, so until SNTP reaches a server the system clock is still
// the 1970 default and any stamp would be a lie. Anything before 2021 means "no
// real time yet". The SNTP sync-status flag cannot answer this: it is reset
// right after each update, so it only reports the last sync event, not whether
// the clock is set.
constexpr uint32_t kClockSetEpochS = 1609459200;  // 2021-01-01T00:00:00Z

constexpr bool clockIsSet(uint32_t epochSeconds) {
  return epochSeconds >= kClockSetEpochS;
}

// Breaks `epochSeconds` into UTC calendar fields. Always UTC, whatever local
// time zone configTime() installed, so a stamp is unambiguous wherever it is
// read. Shared by the stamp and the log-file name so both read the same clock.
inline bool utcParts(uint32_t epochSeconds, struct tm &out) {
  const time_t seconds = static_cast<time_t>(epochSeconds);
  return gmtime_r(&seconds, &out) != nullptr;
}

// Writes the UTC stamp for `epochSeconds` and returns its length, or 0 and an
// empty string when the buffer is too small.
inline size_t formatUtcStamp(char *out, size_t capacity,
                             uint32_t epochSeconds) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  struct tm parts;
  if (!utcParts(epochSeconds, parts)) {
    out[0] = '\0';
    return 0;
  }
  const int written =
      snprintf(out, capacity, "%04d-%02d-%02dT%02d:%02d:%02dZ",
               parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
               parts.tm_hour, parts.tm_min, parts.tm_sec);
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace ledfx
