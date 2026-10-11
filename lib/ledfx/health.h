#pragma once

#include <cstdint>
#include <cstdio>

#include "clock.h"

namespace ledfx {

// GET /health body formatting. Pure string building with no Arduino and no
// allocation, so the host suite asserts the exact bytes the board serves; the
// sources of these values (millis(), time()) stay in main.cpp.

// Everything the endpoint reports, as one struct: the body keeps gaining fields
// (storage counters next), and a struct means adding one does not rewrite every
// call site.
struct HealthStatus {
  uint16_t mode;         // the mode number POST /mode takes
  uint32_t uptimeMs;     // millis()
  uint32_t epochSeconds; // 0 until SNTP has set the clock
};

// The body is a fixed-shape JSON object: {"mode":0,"uptime_s":42,
// "utc":"2026-10-11T17:04:05Z","clock_set":true}. `mode` is at most 5 digits,
// `uptime_s` at most 7 and the stamp is a fixed 20 bytes, so the widest body is
// 79 bytes; the constant leaves room for the fields later tasks add (log
// counts, free space) without a mid-serve truncation.
constexpr size_t kHealthBodyCapacity = 128;

// Writes the NUL-terminated body into `out` and returns its length excluding
// the NUL. Returns 0 and writes an empty string when `capacity` cannot hold the
// whole body (or `out` is null / `capacity` is 0), so a caller that ignores the
// result never serves a half-written object.
//
// Whole seconds are reported, so uptime_s wraps with millis() at ~49.7 days.
// `clock_set` is derived from the epoch rather than passed in, so it can never
// disagree with the stamp printed next to it.
inline size_t formatHealth(char *out, size_t capacity,
                           const HealthStatus &status) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  char stamp[kUtcStampCapacity];
  if (formatUtcStamp(stamp, sizeof(stamp), status.epochSeconds) == 0) {
    out[0] = '\0';
    return 0;
  }
  const int written = snprintf(
      out, capacity,
      "{\"mode\":%u,\"uptime_s\":%u,\"utc\":\"%s\",\"clock_set\":%s}",
      static_cast<unsigned>(status.mode),
      static_cast<unsigned>(status.uptimeMs / 1000u), stamp,
      clockIsSet(status.epochSeconds) ? "true" : "false");
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace ledfx
