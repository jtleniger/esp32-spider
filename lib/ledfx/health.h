#pragma once

#include <cstdint>
#include <cstdio>

namespace ledfx {

// GET /health body formatting. Pure string building with no Arduino and no
// allocation, so the host suite asserts the exact bytes the board serves; the
// uptime source (millis()) stays in main.cpp.

// The body is a fixed-shape JSON object with one field per line of interest:
// {"mode":0,"uptime_s":42}. `mode` is at most 5 digits and `uptime_s` at most 7
// (a uint32 millisecond count / 1000), so the current body is 33 bytes at most;
// the constant leaves room for the fields later tasks add to this endpoint
// (log counts, free space) without a mid-serve truncation.
constexpr size_t kHealthBodyCapacity = 96;

// Writes the NUL-terminated body into `out` and returns its length excluding
// the NUL. Returns 0 and writes an empty string when `capacity` cannot hold the
// whole body (or `out` is null / `capacity` is 0), so a caller that ignores the
// result never serves a half-written object.
//
// `uptimeMs` is the caller's millisecond clock: whole seconds are reported, so
// uptime_s wraps with millis() at ~49.7 days.
inline size_t formatHealth(char *out, size_t capacity, uint16_t mode,
                           uint32_t uptimeMs) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  const int written = snprintf(out, capacity, "{\"mode\":%u,\"uptime_s\":%u}",
                               static_cast<unsigned>(mode),
                               static_cast<unsigned>(uptimeMs / 1000u));
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace ledfx
