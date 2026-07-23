#include "LibraryScanner.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include "CrossPointSettings.h"

namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;

bool isBookFile(const std::string_view name) {
  return FsHelpers::hasEpubExtension(name) || FsHelpers::hasXtcExtension(name) || FsHelpers::hasTxtExtension(name) ||
         FsHelpers::hasMarkdownExtension(name);
}

std::string joinPath(const std::string& dir, const std::string& name) { return (dir == "/" ? dir : dir + "/") + name; }
}  // namespace

std::vector<LibraryScanner::Entry> LibraryScanner::scanAllBooks(const std::string& root, const size_t maxBooks) {
  std::vector<Entry> result;
  if (maxBooks == 0) {
    return result;
  }
  // Reserve conservatively up front; grows normally past this if the card holds more,
  // capped at maxBooks total below.
  result.reserve(std::min<size_t>(maxBooks, 256));

  const auto nameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!nameBuffer) {
    LOG_ERR("LIBSCAN", "OOM: %d bytes", static_cast<int>(NAME_BUFFER_SIZE));
    return result;
  }

  std::vector<std::string> pendingDirs;
  pendingDirs.push_back(root);
  bool truncated = false;

  while (!pendingDirs.empty() && !truncated) {
    const std::string dirPath = std::move(pendingDirs.back());
    pendingDirs.pop_back();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      continue;
    }
    dir.rewindDirectory();

    std::vector<std::string> subDirs;
    std::vector<std::string> bookFiles;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
      if ((!SETTINGS.showHiddenFiles && nameBuffer[0] == '.') ||
          strcmp(nameBuffer.get(), "System Volume Information") == 0) {
        continue;
      }
      if (file.isDirectory()) {
        subDirs.emplace_back(nameBuffer.get());
      } else if (isBookFile(nameBuffer.get())) {
        bookFiles.emplace_back(nameBuffer.get());
      }
    }
    dir.close();

    FsHelpers::sortFileList(bookFiles);
    FsHelpers::sortFileList(subDirs);

    for (const auto& name : bookFiles) {
      if (result.size() >= maxBooks) {
        truncated = true;
        break;
      }
      result.push_back(Entry{joinPath(dirPath, name)});
    }

    // Push in reverse so the worklist (a LIFO stack) still visits subfolders in
    // ascending name order.
    for (auto it = subDirs.rbegin(); it != subDirs.rend(); ++it) {
      pendingDirs.push_back(joinPath(dirPath, *it));
    }
  }

  if (truncated) {
    LOG_INF("LIBSCAN", "Library scan capped at %d books; the SD card holds more", static_cast<int>(maxBooks));
  }

  return result;
}
