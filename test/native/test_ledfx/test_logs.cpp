// logs.h tests: file names, activation lines, name validation and the /logs
// listing. No main() here: test_led_engine.cpp owns the only one in the
// `native/ledfx` suite and runs these cases too.
#include <logs.h>
#include <unity.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kBootEpochS = 1791738245;  // 2026-10-11T17:04:05Z

}  // namespace

// The file name is the boot stamp, squeezed so a text sort of the directory is
// a chronological sort.
void testLogNameFromBootEpoch() {
  char name[ledfx::kLogNameCapacity];
  TEST_ASSERT_EQUAL_UINT32(
      strlen("20261011T170405Z.log"),
      ledfx::formatLogName(name, sizeof(name), kBootEpochS));
  TEST_ASSERT_EQUAL_STRING("20261011T170405Z.log", name);
  ledfx::formatLogName(name, sizeof(name), 0);
  TEST_ASSERT_EQUAL_STRING("19700101T000000Z.log", name);
  ledfx::formatLogName(name, sizeof(name), 2147483647);
  TEST_ASSERT_EQUAL_STRING("20380119T031407Z.log", name);
  TEST_ASSERT_EQUAL_UINT32(strlen("20261011T170405Z.log") + 1,
                           ledfx::kLogNameCapacity);
}

void testLogNameRejectsSmallBuffers() {
  char tight[ledfx::kLogNameCapacity - 1] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogName(tight, sizeof(tight),
                                                   kBootEpochS));
  TEST_ASSERT_EQUAL_STRING("", tight);
  char name[ledfx::kLogNameCapacity];
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogName(nullptr, sizeof(name), 0));
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogName(name, 0, 0));
}

// An activation line is the stamp the log reader sees, plus its newline.
void testLogLineIsStampPlusNewline() {
  char line[ledfx::kLogLineCapacity];
  const size_t len = ledfx::formatLogLine(line, sizeof(line), kBootEpochS);
  TEST_ASSERT_EQUAL_STRING("2026-10-11T17:04:05Z\n", line);
  TEST_ASSERT_EQUAL_UINT32(strlen("2026-10-11T17:04:05Z") + 1, len);
  TEST_ASSERT_EQUAL_UINT32(ledfx::kUtcStampCapacity + 1,
                           ledfx::kLogLineCapacity);

  // One byte short of stamp + newline + NUL is refused rather than truncated.
  char tight[ledfx::kLogLineCapacity - 1] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogLine(tight, sizeof(tight),
                                                   kBootEpochS));
  TEST_ASSERT_EQUAL_STRING("", tight);
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogLine(nullptr, 32, 0));
}

// Everything the rig writes must validate - the listing and the file route both
// rely on it.
void testLogNameValidationAcceptsWrittenNames() {
  const uint32_t epochs[] = {0, 951782400, kBootEpochS, 2147483647};
  for (uint32_t epoch : epochs) {
    char name[ledfx::kLogNameCapacity];
    TEST_ASSERT_TRUE(ledfx::formatLogName(name, sizeof(name), epoch) > 0);
    TEST_ASSERT_TRUE_MESSAGE(ledfx::isLogName(name), name);
  }
  TEST_ASSERT_TRUE(ledfx::isLogName("20261011T170405Z.log"));
  TEST_ASSERT_TRUE(ledfx::isLogName("19700101T000000Z.log"));
}

// /logs/:filename only ever opens a file the rig wrote: a traversal, a foreign
// suffix or a wrong-length name is refused before it reaches LittleFS. The
// check is the shape of the name, not a calendar: a digits-only name with an
// impossible date can only ever refer to a file that does not exist, so it
// costs a 404 rather than a filesystem walk.
void testLogNameValidationRejectsEverythingElse() {
  const char *bad[] = {
      "",
      ".",
      "..",
      "../20261011T170405Z.log",
      "/etc/passwd",
      "20261011T170405Z.log/",
      "20261011T170405Z.logx",
      "20261011T170405Z.log.log",
      "20261011T170405.log",    // no Z
      "20261011X170405Z.log",   // no T
      "2026101T170405Z.log",    // short date
      "20261011T17045Z.log",    // short time
      "20261011T170405z.log",   // lowercase zone
      "20261011T170405Z.LOG",   // suffix case
      "20261011T170405Z.txt",
      "a0261011T170405Z.log",
      "20261011T170405Z.log ",
      " 20261011T170405Z.log",
      "20261011T170405Z.log/../x",
  };
  for (const char *name : bad) {
    TEST_ASSERT_FALSE_MESSAGE(ledfx::isLogName(name), name);
  }
  TEST_ASSERT_FALSE(ledfx::isLogName(nullptr));
}

void testLogListBody() {
  const ledfx::LogEntry entries[] = {{"20261011T170405Z.log", 120},
                                     {"20261012T090000Z.log", 7}};
  char body[ledfx::kLogListCapacity];
  const size_t len =
      ledfx::formatLogList(body, sizeof(body), entries, 2, false);
  TEST_ASSERT_EQUAL_STRING(
      "{\"logs\":[{\"name\":\"20261011T170405Z.log\",\"size\":120},"
      "{\"name\":\"20261012T090000Z.log\",\"size\":7}],\"truncated\":false}",
      body);
  TEST_ASSERT_EQUAL_UINT32(strlen(body), len);

  TEST_ASSERT_EQUAL_UINT32(
      strlen("{\"logs\":[],\"truncated\":false}"),
      ledfx::formatLogList(body, sizeof(body), nullptr, 0, false));
  TEST_ASSERT_EQUAL_STRING("{\"logs\":[],\"truncated\":false}", body);

  ledfx::formatLogList(body, sizeof(body), entries, 2, true);
  TEST_ASSERT_TRUE(strstr(body, "],\"truncated\":true}") != nullptr);
}

// The listing keeps the newest files and still counts everything it saw, so the
// response can admit it truncated.
void testLogListingKeepsNewest() {
  static ledfx::LogListing listing;
  ledfx::logListingReset(listing);
  TEST_ASSERT_EQUAL_UINT32(0, listing.count);
  TEST_ASSERT_EQUAL_UINT32(0, listing.total);

  const size_t added = ledfx::kMaxLogEntries + 8;
  for (size_t i = 0; i < added; ++i) {
    char name[ledfx::kLogNameCapacity];
    TEST_ASSERT_TRUE(ledfx::formatLogName(
                         name, sizeof(name),
                         1609459200u + static_cast<uint32_t>(i) * 3600u) > 0);
    ledfx::logListingAdd(listing, name, static_cast<uint32_t>(i));
  }
  TEST_ASSERT_EQUAL_UINT32(ledfx::kMaxLogEntries, listing.count);
  TEST_ASSERT_EQUAL_UINT32(added, listing.total);

  // Newest first, and every entry points at the name it was added with rather
  // than at a slot the shift moved.
  for (size_t i = 0; i < listing.count; ++i) {
    TEST_ASSERT_EQUAL_STRING(listing.names[i], listing.entries[i].name);
    if (i > 0) {
      TEST_ASSERT_TRUE(strcmp(listing.entries[i - 1].name,
                              listing.entries[i].name) > 0);
    }
  }
  char newest[ledfx::kLogNameCapacity];
  ledfx::formatLogName(newest, sizeof(newest),
                       1609459200u + static_cast<uint32_t>(added - 1) * 3600u);
  TEST_ASSERT_EQUAL_STRING(newest, listing.entries[0].name);
  TEST_ASSERT_EQUAL_UINT32(added - 1, listing.entries[0].size);
  // The oldest kept is the last of the newest kMaxLogEntries.
  const uint32_t oldestEpoch =
      1609459200u +
      static_cast<uint32_t>(added - ledfx::kMaxLogEntries) * 3600u;
  char oldest[ledfx::kLogNameCapacity];
  ledfx::formatLogName(oldest, sizeof(oldest), oldestEpoch);
  TEST_ASSERT_EQUAL_STRING(oldest, listing.entries[listing.count - 1].name);
}

// readdir order is whatever the filesystem feels like: the same files must give
// the same listing either way round.
void testLogListingIsOrderIndependent() {
  static ledfx::LogListing forward;
  static ledfx::LogListing backward;
  ledfx::logListingReset(forward);
  ledfx::logListingReset(backward);
  const size_t added = ledfx::kMaxLogEntries + 4;
  for (size_t i = 0; i < added; ++i) {
    char name[ledfx::kLogNameCapacity];
    TEST_ASSERT_TRUE(ledfx::formatLogName(
                         name, sizeof(name),
                         1609459200u + static_cast<uint32_t>(i) * 60u) > 0);
    ledfx::logListingAdd(forward, name, static_cast<uint32_t>(i));
  }
  for (size_t i = added; i > 0; --i) {
    char name[ledfx::kLogNameCapacity];
    TEST_ASSERT_TRUE(ledfx::formatLogName(
                         name, sizeof(name),
                         1609459200u + static_cast<uint32_t>(i - 1) * 60u) > 0);
    ledfx::logListingAdd(backward, name, static_cast<uint32_t>(i - 1));
  }
  TEST_ASSERT_EQUAL_UINT32(forward.count, backward.count);
  TEST_ASSERT_EQUAL_UINT32(forward.total, backward.total);
  for (size_t i = 0; i < forward.count; ++i) {
    TEST_ASSERT_EQUAL_STRING(forward.entries[i].name, backward.entries[i].name);
    TEST_ASSERT_EQUAL_UINT32(forward.entries[i].size,
                             backward.entries[i].size);
  }
}

void testLogListingBelowCapacity() {
  static ledfx::LogListing listing;
  ledfx::logListingReset(listing);
  const char *names[] = {"20261011T170405Z.log", "20261010T170405Z.log",
                         "20261012T170405Z.log"};
  for (size_t i = 0; i < 3; ++i) {
    ledfx::logListingAdd(listing, names[i], static_cast<uint32_t>(10 + i));
  }
  TEST_ASSERT_EQUAL_UINT32(3, listing.count);
  TEST_ASSERT_EQUAL_UINT32(3, listing.total);
  TEST_ASSERT_EQUAL_STRING("20261012T170405Z.log", listing.entries[0].name);
  TEST_ASSERT_EQUAL_STRING("20261011T170405Z.log", listing.entries[1].name);
  TEST_ASSERT_EQUAL_STRING("20261010T170405Z.log", listing.entries[2].name);
  TEST_ASSERT_EQUAL_UINT32(12, listing.entries[0].size);  // added last
  TEST_ASSERT_EQUAL_UINT32(10, listing.entries[1].size);  // added first
  TEST_ASSERT_EQUAL_UINT32(11, listing.entries[2].size);
}

// A full listing of maximum-size entries has to fit the buffer the handler
// serves from; anything less is refused rather than half-written.
void testLogListFitsWorstCase() {
  static ledfx::LogEntry entries[ledfx::kMaxLogEntries];
  static char names[ledfx::kMaxLogEntries][ledfx::kLogNameCapacity];
  for (size_t i = 0; i < ledfx::kMaxLogEntries; ++i) {
    TEST_ASSERT_TRUE(ledfx::formatLogName(names[i], ledfx::kLogNameCapacity,
                                          1609459200u + i * 3600u) > 0);
    entries[i].name = names[i];
    entries[i].size = 0xFFFFFFFFu;
  }
  static char body[ledfx::kLogListCapacity];
  const size_t len = ledfx::formatLogList(body, sizeof(body), entries,
                                          ledfx::kMaxLogEntries, true);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_TRUE(len < ledfx::kLogListCapacity);
  TEST_ASSERT_EQUAL_CHAR('{', body[0]);
  TEST_ASSERT_EQUAL_CHAR('}', body[len - 1]);
  TEST_ASSERT_EQUAL_UINT32(len, strlen(body));

  char tight[64] = "garbage";
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatLogList(tight, sizeof(tight), entries, 2, false));
  TEST_ASSERT_EQUAL_STRING("", tight);
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatLogList(nullptr, sizeof(body), entries, 2, false));
  TEST_ASSERT_EQUAL_UINT32(0, ledfx::formatLogList(body, 0, entries, 2, false));
  TEST_ASSERT_EQUAL_UINT32(
      0, ledfx::formatLogList(body, sizeof(body), nullptr, 1, false));
}
