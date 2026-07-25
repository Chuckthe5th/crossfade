#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

// A single manually-pinned book (path + display title), independent of RecentBooksStore --
// the pinned book is meant to stay put regardless of reading activity, so it can't live in a
// store that reorders or prunes on every read (see FinishedBooksStore's header comment for why
// RecentBooksStore specifically is disqualified for this kind of persistent, user-set flag).
class PinnedBookStore : public PersistableStore<PinnedBookStore> {
 private:
  std::string pinnedPath;
  std::string pinnedTitle;

  PinnedBookStore() = default;
  ~PinnedBookStore() = default;

  friend class PersistableStore<PinnedBookStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/pinned-book.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool hasPinned() const { return !pinnedPath.empty(); }
  bool isPinned(const std::string& path) const { return hasPinned() && pinnedPath == path; }
  const std::string& getPinnedPath() const { return pinnedPath; }
  const std::string& getPinnedTitle() const { return pinnedTitle; }

  // Replaces any existing pin (only one book can be pinned at a time). Persists on success.
  void setPinned(const std::string& path, const std::string& title);
  // No-op if nothing is pinned. Persists on success.
  void clearPinned();

  // Repoint the pinned path after the backing file was moved on disk (e.g. into /read/ -- see
  // EpubReaderActivity's moveFinishedBookToReadFolder). No-op if the pin doesn't match oldPath.
  // Persists on success.
  void updatePath(const std::string& oldPath, const std::string& newPath);
};

#define PINNED_BOOK PinnedBookStore::getInstance()
