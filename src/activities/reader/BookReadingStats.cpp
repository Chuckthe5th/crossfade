#include "BookReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {
// Binary layout v1 (19 bytes):
//   [0]      version (= 1)
//   [1-2]    sessionCount              uint16_t LE
//   [3-6]    totalReadingSeconds       uint32_t LE
//   [7-10]   totalPagesTurned          uint32_t LE
//   [11-12]  avgSecondsPerForwardPage  uint16_t LE
//   [13-14]  paceSampleCount           uint16_t LE
//   [15-18]  estimatedTimeLeftSeconds  uint32_t LE
constexpr uint8_t STATS_FILE_VERSION = 1;
constexpr int STATS_FILE_SIZE = 19;
constexpr uint16_t MAX_PACE_SAMPLE_COUNT = 1000;

uint16_t readLe16(const uint8_t* data, const int offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t readLe32(const uint8_t* data, const int offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void writeLe16(uint8_t* data, const int offset, const uint16_t value) {
  data[offset] = value & 0xFF;
  data[offset + 1] = (value >> 8) & 0xFF;
}

void writeLe32(uint8_t* data, const int offset, const uint32_t value) {
  data[offset] = value & 0xFF;
  data[offset + 1] = (value >> 8) & 0xFF;
  data[offset + 2] = (value >> 16) & 0xFF;
  data[offset + 3] = (value >> 24) & 0xFF;
}

std::string statsPath(const std::string& cachePath) { return cachePath + "/stats.bin"; }

}  // namespace

BookReadingStats BookReadingStats::load(const std::string& cachePath) {
  BookReadingStats stats;
  HalFile f;
  if (!Storage.openFileForRead("BSTATS", statsPath(cachePath), f)) {
    return stats;
  }
  uint8_t data[STATS_FILE_SIZE] = {};
  const int n = f.read(data, STATS_FILE_SIZE);

  if (n != STATS_FILE_SIZE || data[0] != STATS_FILE_VERSION) {
    LOG_DBG("BSTATS", "Stats missing or version mismatch, starting fresh");
    return stats;
  }

  stats.sessionCount = readLe16(data, 1);
  stats.totalReadingSeconds = readLe32(data, 3);
  stats.totalPagesTurned = readLe32(data, 7);
  stats.avgSecondsPerForwardPage = readLe16(data, 11);
  stats.paceSampleCount = readLe16(data, 13);
  stats.estimatedTimeLeftSeconds = readLe32(data, 15);
  return stats;
}

void BookReadingStats::save(const std::string& cachePath) const {
  uint8_t data[STATS_FILE_SIZE];
  memset(data, 0, sizeof(data));
  data[0] = STATS_FILE_VERSION;
  writeLe16(data, 1, sessionCount);
  writeLe32(data, 3, totalReadingSeconds);
  writeLe32(data, 7, totalPagesTurned);
  writeLe16(data, 11, avgSecondsPerForwardPage);
  writeLe16(data, 13, paceSampleCount);
  writeLe32(data, 15, estimatedTimeLeftSeconds);

  // Atomic temp-file-then-rename, same convention as ProgressFile::writeAtomic -- a torn write
  // (power loss mid-SPI) must never leave stats.bin half-written and unrecoverable.
  const std::string finalPath = statsPath(cachePath);
  const std::string tmpPath = finalPath + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("BSTATS", tmpPath, f)) {
      LOG_ERR("BSTATS", "Could not open temp stats file for write: %s", tmpPath.c_str());
      return;
    }
    const size_t written = f.write(data, STATS_FILE_SIZE);
    if (written != static_cast<size_t>(STATS_FILE_SIZE)) {
      LOG_ERR("BSTATS", "Short write saving stats to %s", tmpPath.c_str());
      return;
    }
    f.flush();
  }
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("BSTATS", "Failed to rename temp stats into place: %s", finalPath.c_str());
  }
}

bool BookReadingStats::remove(const std::string& cachePath) {
  const std::string path = statsPath(cachePath);
  if (!Storage.exists(path.c_str())) {
    return true;
  }
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("BSTATS", "Could not delete %s", path.c_str());
    return false;
  }
  return true;
}

void BookReadingStats::recordForwardPageRead(uint32_t seconds) {
  if (seconds == 0) {
    return;
  }
  if (seconds > UINT16_MAX) {
    seconds = UINT16_MAX;
  }

  const uint16_t sample = static_cast<uint16_t>(seconds);
  if (paceSampleCount == 0 || avgSecondsPerForwardPage == 0) {
    avgSecondsPerForwardPage = sample;
    paceSampleCount = 1;
    return;
  }

  const uint16_t weight = paceSampleCount < MAX_PACE_SAMPLE_COUNT ? paceSampleCount : MAX_PACE_SAMPLE_COUNT;
  const uint32_t nextAverage =
      (static_cast<uint32_t>(avgSecondsPerForwardPage) * weight + sample) / (static_cast<uint32_t>(weight) + 1U);
  avgSecondsPerForwardPage = static_cast<uint16_t>(nextAverage);
  if (paceSampleCount < MAX_PACE_SAMPLE_COUNT) {
    paceSampleCount++;
  }
}
