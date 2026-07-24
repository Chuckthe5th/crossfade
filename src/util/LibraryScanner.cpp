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

std::string basename(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Carries a book file's fingerprint alongside its name through the same sort sortFileList would
// apply to a plain name list -- sortFileList itself only accepts vector<string>, and for a
// directory's book files specifically (never containing a '/'-suffixed directory entry) its
// "directories first" branch never fires, so a direct naturalLess sort is equivalent.
struct BookFileInfo {
  std::string name;
  uint32_t size = 0;
  uint32_t fatDateTime = 0;
};
}  // namespace

std::vector<LibraryScanner::Entry> LibraryScanner::scanAllBooks(const std::string& root, const size_t maxBooks,
                                                                const bool collectFingerprint) {
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
    std::vector<BookFileInfo> bookFiles;
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
      if ((!SETTINGS.showHiddenFiles && nameBuffer[0] == '.') ||
          strcmp(nameBuffer.get(), "System Volume Information") == 0) {
        continue;
      }
      if (file.isDirectory()) {
        subDirs.emplace_back(nameBuffer.get());
      } else if (isBookFile(nameBuffer.get())) {
        BookFileInfo info;
        info.name = nameBuffer.get();
        if (collectFingerprint) {
          info.size = static_cast<uint32_t>(file.fileSize());
          uint16_t fatDate = 0;
          uint16_t fatTime = 0;
          if (file.getModifyTime(fatDate, fatTime)) {
            info.fatDateTime = (static_cast<uint32_t>(fatDate) << 16) | fatTime;
          }
        }
        bookFiles.push_back(std::move(info));
      }
    }
    dir.close();

    std::sort(bookFiles.begin(), bookFiles.end(),
              [](const BookFileInfo& a, const BookFileInfo& b) { return FsHelpers::naturalLess(a.name, b.name); });
    FsHelpers::sortFileList(subDirs);

    for (const auto& info : bookFiles) {
      if (result.size() >= maxBooks) {
        truncated = true;
        break;
      }
      result.push_back(Entry{joinPath(dirPath, info.name), info.size, info.fatDateTime});
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

void LibraryScanner::sortByFilename(std::vector<Entry>& entries) {
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return FsHelpers::naturalLess(basename(a.path), basename(b.path)); });
}
