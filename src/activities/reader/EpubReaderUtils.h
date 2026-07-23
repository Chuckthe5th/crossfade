#pragma once

#include <Epub.h>
#include <Logging.h>

#include "ProgressFile.h"

namespace EpubReaderUtils {

// Persists reader progress for an EPUB to its cache directory. Returns true on success.
inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  uint8_t data[6];
  data[0] = spineIndex & 0xFF;
  data[1] = (spineIndex >> 8) & 0xFF;
  data[2] = pageNumber & 0xFF;
  data[3] = (pageNumber >> 8) & 0xFF;
  data[4] = pageCount & 0xFF;
  data[5] = (pageCount >> 8) & 0xFF;
  if (!ProgressFile::writeAtomic(epub.getCachePath(), data, sizeof(data))) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d page=%d", spineIndex, pageNumber);
  return true;
}

// Reads back progress.bin written by saveProgress(). Returns false if no progress
// is cached (spineIndex/pageNumber/pageCount are left unchanged). pageCount is
// only set when the 6-byte (v2) format is present; callers should treat a
// pageCount of 0 as "unknown" for older 4-byte saves.
inline bool loadProgress(const std::string& cachePath, int& spineIndex, int& pageNumber, int& pageCount) {
  HalFile f;
  if (!Storage.openFileForRead("ERS", cachePath + "/progress.bin", f)) {
    return false;
  }
  uint8_t data[6];
  const int dataSize = f.read(data, 6);
  if (dataSize != 4 && dataSize != 6) {
    return false;
  }
  spineIndex = data[0] + (data[1] << 8);
  pageNumber = data[2] + (data[3] << 8);
  if (pageNumber == UINT16_MAX) {
    // UINT16_MAX is an in-memory navigation sentinel for "open previous chapter on
    // its last page". It should never be treated as persisted resume state.
    pageNumber = 0;
  }
  if (dataSize == 6) {
    pageCount = data[4] + (data[5] << 8);
  }
  return true;
}

}  // namespace EpubReaderUtils
