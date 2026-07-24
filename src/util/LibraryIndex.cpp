#include "LibraryIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr uint8_t LIBRARY_INDEX_VERSION = 1;
// Sanity cap on the persisted entry count, checked before allocating/looping -- guards against a
// corrupt/truncated header producing a garbage count that would otherwise drive an unbounded
// allocation or read loop. Far above any real library (LibraryScanner's own scan cap tops out at a
// few thousand); this only ever fires on a genuinely corrupt file.
constexpr uint32_t MAX_SANE_ENTRY_COUNT = 100000;
}  // namespace

namespace LibraryIndex {

bool load(const std::string& path, Index& outIndex) {
  HalFile file;
  if (!Storage.openFileForRead("LIBIDX", path, file)) {
    return false;  // Missing file -- expected before the first build, not an error.
  }

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != LIBRARY_INDEX_VERSION) {
    LOG_DBG("LIBIDX", "Index version mismatch: expected %d, got %d", LIBRARY_INDEX_VERSION, version);
    return false;
  }

  Index index;
  serialization::readPod(file, index.lastBuildDurationMs);

  uint32_t entryCount = 0;
  serialization::readPod(file, entryCount);
  if (entryCount > MAX_SANE_ENTRY_COUNT) {
    LOG_ERR("LIBIDX", "Index entry count %u exceeds sanity cap; treating as corrupt", entryCount);
    return false;
  }

  index.entries.reserve(entryCount);
  for (uint32_t i = 0; i < entryCount; i++) {
    Entry entry;
    serialization::readString(file, entry.path);
    serialization::readPod(file, entry.size);
    serialization::readPod(file, entry.fatDateTime);
    serialization::readString(file, entry.title);
    serialization::readString(file, entry.author);
    serialization::readString(file, entry.series);
    serialization::readPod(file, entry.seriesIndex);
    index.entries.push_back(std::move(entry));
  }

  // A truncated file leaves later reads short (readString/readPod don't surface a failure code),
  // so a truncation partway through the entries would otherwise go unnoticed. available() == 0 is
  // the expected end state for a well-formed file (nothing left to read); anything else after
  // walking every declared entry means the file was longer than its own header claimed, which is
  // just as much "not the format we expect" as being shorter.
  if (file.available() != 0) {
    LOG_ERR("LIBIDX", "Index has trailing data after %u entries; treating as corrupt", entryCount);
    return false;
  }

  outIndex = std::move(index);
  return true;
}

bool save(const std::string& path, const Index& index) {
  const std::string tmpPath = path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("LIBIDX", tmpPath, file)) {
    LOG_ERR("LIBIDX", "Could not open %s for write", tmpPath.c_str());
    return false;
  }

  serialization::writePod(file, LIBRARY_INDEX_VERSION);
  serialization::writePod(file, index.lastBuildDurationMs);
  serialization::writePod(file, static_cast<uint32_t>(index.entries.size()));
  for (const auto& entry : index.entries) {
    serialization::writeString(file, entry.path);
    serialization::writePod(file, entry.size);
    serialization::writePod(file, entry.fatDateTime);
    serialization::writeString(file, entry.title);
    serialization::writeString(file, entry.author);
    serialization::writeString(file, entry.series);
    serialization::writePod(file, entry.seriesIndex);
  }
  file.close();

  if (Storage.exists(path.c_str())) {
    Storage.remove(path.c_str());
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("LIBIDX", "Could not rename %s to %s", tmpPath.c_str(), path.c_str());
    return false;
  }
  return true;
}

}  // namespace LibraryIndex
