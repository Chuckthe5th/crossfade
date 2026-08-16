#include "GlobalReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr const char* GLOBAL_STATS_PATH = "/.crosspoint/global_stats.bin";
// Binary layout v1 (13 bytes):
//   [0]      version (= 1)
//   [1-4]    totalSessions        uint32_t LE
//   [5-8]    totalReadingSeconds  uint32_t LE
//   [9-12]   totalPagesTurned     uint32_t LE
constexpr uint8_t STATS_FILE_VERSION = 1;
constexpr int STATS_FILE_SIZE = 13;

uint32_t readLe32(const uint8_t* data, const int offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void writeLe32(uint8_t* data, const int offset, const uint32_t value) {
  data[offset] = value & 0xFF;
  data[offset + 1] = (value >> 8) & 0xFF;
  data[offset + 2] = (value >> 16) & 0xFF;
  data[offset + 3] = (value >> 24) & 0xFF;
}

}  // namespace

GlobalReadingStats GlobalReadingStats::load() {
  GlobalReadingStats stats;
  HalFile f;
  if (!Storage.openFileForRead("GSTATS", GLOBAL_STATS_PATH, f)) {
    return stats;
  }
  uint8_t data[STATS_FILE_SIZE] = {};
  const int n = f.read(data, STATS_FILE_SIZE);

  if (n != STATS_FILE_SIZE || data[0] != STATS_FILE_VERSION) {
    LOG_DBG("GSTATS", "Global stats missing or version mismatch, starting fresh");
    return stats;
  }

  stats.totalSessions = readLe32(data, 1);
  stats.totalReadingSeconds = readLe32(data, 5);
  stats.totalPagesTurned = readLe32(data, 9);
  return stats;
}

void GlobalReadingStats::save() const {
  uint8_t data[STATS_FILE_SIZE];
  memset(data, 0, sizeof(data));
  data[0] = STATS_FILE_VERSION;
  writeLe32(data, 1, totalSessions);
  writeLe32(data, 5, totalReadingSeconds);
  writeLe32(data, 9, totalPagesTurned);

  // Always overwrites the same fixed-size file -- a rolling tally, never an append log. Atomic
  // temp-file-then-rename, same convention as ProgressFile::writeAtomic.
  const std::string finalPath = GLOBAL_STATS_PATH;
  const std::string tmpPath = finalPath + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("GSTATS", tmpPath, f)) {
      LOG_ERR("GSTATS", "Could not open temp global stats file for write: %s", tmpPath.c_str());
      return;
    }
    const size_t written = f.write(data, STATS_FILE_SIZE);
    if (written != static_cast<size_t>(STATS_FILE_SIZE)) {
      LOG_ERR("GSTATS", "Short write saving global stats to %s", tmpPath.c_str());
      return;
    }
    f.flush();
  }
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("GSTATS", "Failed to rename temp global stats into place: %s", finalPath.c_str());
  }
}
