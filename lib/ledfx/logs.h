#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "clock.h"

namespace ledfx {

// Motion log formatting: one LittleFS file per boot, one line per radar
// activation. The storage and the HTTP routes live in main.cpp; the bytes and
// the name validation live here (no Arduino, no allocation), so the host suite
// can assert what the endpoints serve and that a request cannot reach a file
// outside the log directory.

// "YYYYMMDDTHHMMSSZ.log" plus the NUL. Fixed width on purpose: sorting these
// names as text sorts them by boot time, which is how the bounded /logs listing
// keeps the newest files without a timestamp per entry.
constexpr size_t kLogNameCapacity = 21;

// One activation line: the UTC stamp plus its newline.
constexpr size_t kLogLineCapacity = kUtcStampCapacity + 1;

// The listing is bounded by the caller's buffers: it keeps the newest
// kMaxLogEntries files and flags the response `truncated` when the directory
// held more (the true count is in /health).
constexpr size_t kMaxLogEntries = 32;
constexpr size_t kLogListCapacity = 2048;

// Name of this boot's log, from the boot epoch (epoch - uptime, see main.cpp).
inline size_t formatLogName(char *out, size_t capacity,
                            uint32_t bootEpochSeconds) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  struct tm parts;
  if (!utcParts(bootEpochSeconds, parts)) {
    out[0] = '\0';
    return 0;
  }
  const int written =
      snprintf(out, capacity, "%04d%02d%02dT%02d%02d%02dZ.log",
               parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
               parts.tm_hour, parts.tm_min, parts.tm_sec);
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

// One activation: the UTC stamp and a newline. Rejection empties the buffer,
// like the other formatters, so a caller that ignores the result cannot serve a
// stale line.
inline size_t formatLogLine(char *out, size_t capacity, uint32_t epochSeconds) {
  if (out == nullptr || capacity == 0) {
    return 0;
  }
  if (capacity < kLogLineCapacity) {
    out[0] = '\0';
    return 0;
  }
  const size_t stampLen = formatUtcStamp(out, capacity, epochSeconds);
  if (stampLen == 0) {
    return 0;
  }
  out[stampLen] = '\n';
  out[stampLen + 1] = '\0';
  return stampLen + 1;
}

inline bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

// True only for names this rig writes: eight digits, 'T', six digits, 'Z',
// ".log". Everything else - a traversal, a made-up name, a stray directory
// entry - is refused, so /logs/:filename cannot be talked into reading or
// deleting something else. Rejects the length first, so a huge name costs
// nothing.
inline bool isLogName(const char *name) {
  if (name == nullptr || strlen(name) != kLogNameCapacity - 1) {
    return false;
  }
  for (size_t i = 0; i < 8; ++i) {
    if (!isAsciiDigit(name[i])) {
      return false;
    }
  }
  if (name[8] != 'T') {
    return false;
  }
  for (size_t i = 9; i < 15; ++i) {
    if (!isAsciiDigit(name[i])) {
      return false;
    }
  }
  return name[15] == 'Z' && strcmp(name + 16, ".log") == 0;
}

struct LogEntry {
  const char *name;
  uint32_t size;
};

// Collector for GET /logs: keeps the newest kMaxLogEntries files while counting
// every file it saw, so the response can admit it truncated. Names are
// fixed-width boot stamps, so "newest" is the largest name as text and no
// timestamp has to be kept per entry.
struct LogListing {
  LogEntry entries[kMaxLogEntries];
  char names[kMaxLogEntries][kLogNameCapacity];
  size_t count;  // entries kept
  size_t total;  // files seen
};

inline void logListingReset(LogListing &listing) {
  listing.count = 0;
  listing.total = 0;
}

// Adds one file. Callers may hand entries in any order: the listing stays
// sorted newest first, and once it is full only a newer file displaces the
// oldest.
inline void logListingAdd(LogListing &listing, const char *name,
                          uint32_t size) {
  ++listing.total;
  size_t pos = 0;
  while (pos < listing.count && strcmp(listing.names[pos], name) > 0) {
    ++pos;
  }
  if (pos == kMaxLogEntries) {
    return;  // older than everything kept (only possible once full)
  }
  const size_t last =
      listing.count < kMaxLogEntries ? listing.count : kMaxLogEntries - 1;
  for (size_t i = last; i > pos; --i) {
    memcpy(listing.names[i], listing.names[i - 1], kLogNameCapacity);
    listing.entries[i].size = listing.entries[i - 1].size;
  }
  memcpy(listing.names[pos], name, kLogNameCapacity);
  listing.entries[pos].size = size;
  // The shift moved the names the entries point at, so re-point them.
  for (size_t i = pos; i <= last; ++i) {
    listing.entries[i].name = listing.names[i];
  }
  if (listing.count < kMaxLogEntries) {
    ++listing.count;
  }
}

// GET /logs body: {"logs":[{"name":"...","size":123},...],"truncated":false}.
// Returns the length, or 0 and an empty string when the buffer cannot hold the
// whole body.
inline size_t formatLogList(char *out, size_t capacity, const LogEntry *entries,
                            size_t count, bool truncated) {
  if (out == nullptr || capacity == 0 || (count > 0 && entries == nullptr)) {
    return 0;
  }
  int written = snprintf(out, capacity, "{\"logs\":[");
  if (written < 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  size_t used = static_cast<size_t>(written);
  for (size_t i = 0; i < count; ++i) {
    written =
        snprintf(out + used, capacity - used, "%s{\"name\":\"%s\",\"size\":%u}",
                 i == 0 ? "" : ",", entries[i].name,
                 static_cast<unsigned>(entries[i].size));
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) {
      out[0] = '\0';
      return 0;
    }
    used += static_cast<size_t>(written);
  }
  written = snprintf(out + used, capacity - used, "],\"truncated\":%s}",
                     truncated ? "true" : "false");
  if (written < 0 || static_cast<size_t>(written) >= capacity - used) {
    out[0] = '\0';
    return 0;
  }
  return used + static_cast<size_t>(written);
}

}  // namespace ledfx
