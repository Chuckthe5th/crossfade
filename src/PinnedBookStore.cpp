#include "PinnedBookStore.h"

#include <Logging.h>

void PinnedBookStore::toJson(JsonDocument& doc) const {
  doc["path"] = pinnedPath;
  doc["title"] = pinnedTitle;
}

bool PinnedBookStore::fromJson(JsonVariantConst doc) {
  pinnedPath = doc["path"] | "";
  pinnedTitle = doc["title"] | "";
  LOG_DBG("PBS", "Pinned book loaded from file (%s)", pinnedPath.empty() ? "none" : pinnedPath.c_str());
  return true;
}

void PinnedBookStore::setPinned(const std::string& path, const std::string& title) {
  if (pinnedPath == path && pinnedTitle == title) return;  // Already pinned to this path/title -- nothing changed.
  pinnedPath = path;
  pinnedTitle = title;
  saveToFile();
}

void PinnedBookStore::clearPinned() {
  if (pinnedPath.empty()) return;  // Already unpinned -- nothing changed.
  pinnedPath.clear();
  pinnedTitle.clear();
  saveToFile();
}

void PinnedBookStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  if (pinnedPath != oldPath) return;
  pinnedPath = newPath;
  saveToFile();
}
