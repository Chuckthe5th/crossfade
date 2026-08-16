#pragma once
#include <cstdint>
#include <string>

// Per-book reading statistics, persisted to cachePath/stats.bin. Trimmed port of CrossInk's
// BookReadingStats: keeps only what feeds a time-read/time-left label (this fork has no reading
// heatmap or cross-device sync, so the date/streak/time-bucket fields those need are left out).
// Entirely opt-in -- see CrossPointSettings::shouldTrackReadingStats() -- and entirely
// millis()-based, no RTC/wall-clock dependency.
struct BookReadingStats {
  uint16_t sessionCount = 0;              // Total times this book was opened for a qualifying session
  uint32_t totalReadingSeconds = 0;       // Accumulated reading time in seconds
  uint32_t totalPagesTurned = 0;          // Total forward page turns after the dwell threshold
  uint16_t avgSecondsPerForwardPage = 0;  // Running average pace for time-left estimates
  uint16_t paceSampleCount = 0;           // Number of forward-page pace samples included in the average
  uint32_t estimatedTimeLeftSeconds = 0;  // Last computed time-left estimate; 0 means unavailable

  // Loads stats from cachePath/stats.bin. Returns default-constructed stats if no file exists or
  // it doesn't match the current version -- callers get a fresh start, not an error.
  static BookReadingStats load(const std::string& cachePath);

  // Atomically replaces cachePath/stats.bin (temp file + rename -- see ProgressFile::writeAtomic
  // for why: a torn write must never leave a half-written, unrecoverable file).
  void save(const std::string& cachePath) const;

  // Deletes cachePath/stats.bin. Missing file is treated as success.
  static bool remove(const std::string& cachePath);

  // Updates the running forward-reading pace with one page's dwell time. Weighted average, capped
  // at MAX_PACE_SAMPLE_COUNT samples so one very old session can't dominate current pace forever.
  void recordForwardPageRead(uint32_t seconds);
};
