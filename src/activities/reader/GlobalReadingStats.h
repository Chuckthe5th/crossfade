#pragma once
#include <cstdint>

// Cumulative reading statistics across all books, persisted to
// /.crosspoint/global_stats.bin. Trimmed port of CrossInk's GlobalReadingStats: rolling totals
// only, no per-session history, no date/streak buckets, no cross-device sync -- this fork has no
// reading heatmap or nearby-device sync to feed. Every save() overwrites the same fixed-size file
// in place, so this can never grow with reading history -- it's a running tally, not a log.
struct GlobalReadingStats {
  uint32_t totalSessions = 0;        // Total book-open sessions across all books
  uint32_t totalReadingSeconds = 0;  // Accumulated reading time across all books
  uint32_t totalPagesTurned = 0;     // Total forward page turns across all books

  // Loads stats from /.crosspoint/global_stats.bin. Returns default-constructed stats if the file
  // is missing or its version doesn't match.
  static GlobalReadingStats load();

  // Atomically replaces /.crosspoint/global_stats.bin (temp file + rename).
  void save() const;
};
