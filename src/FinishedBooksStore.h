#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// Manually-set "finished" flags, keyed by book path. Deliberately its own store rather than living
// in book.bin, library_index.bin, or RecentBooksStore -- each of those has a normal, expected
// operation that would silently discard a flag stored there:
//   - book.bin is a rebuildable per-book cache, cleared by the Clear Cache context-menu action or
//     any fingerprint mismatch.
//   - library_index.bin's entries are wholesale-replaced (not merged) on any fingerprint mismatch
//     during a delta rebuild, and the index is only even consulted when SETTINGS.groupBySeries is
//     on -- with it off, Browse Books never reads it at all.
//   - RecentBooksStore is disqualified even more directly: SETTINGS.removeReadBooksFromRecents
//     already deletes a book's Recents entry the moment it's finished, so storing the finished
//     flag there would mean the very act of finishing a book erases whether it's finished.
// None of those are safe homes for a manually-set, user-facing flag that must survive cache
// clears, index rebuilds, and recents pruning untouched.
//
// Deliberately independent of SETTINGS.moveFinishedToReadFolder/removeReadBooksFromRecents, which
// key off atEndOfBook -- a live, reversible "is the reading position currently on the last page"
// signal, not a persisted flag (navigating backward off the last page un-triggers them). Manually
// marking a book finished is a distinct, deliberate, position-independent action; the two don't
// drive each other.
class FinishedBooksStore : public PersistableStore<FinishedBooksStore> {
 private:
  std::vector<std::string> finishedPaths;

  FinishedBooksStore() = default;
  ~FinishedBooksStore() = default;

  friend class PersistableStore<FinishedBooksStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/finished-books.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isFinished(const std::string& path) const;
  // Adds/removes path from the finished set. Persists on success, matching
  // RecentBooksStore::addBook/removeByPath's convention.
  void setFinished(const std::string& path, bool finished);

  // Repoint an entry's path after the backing file was moved on disk (e.g. into /read/ -- see
  // EpubReaderActivity's moveFinishedBookToReadFolder). No-op if no entry matches oldPath.
  // Persists on success.
  void updatePath(const std::string& oldPath, const std::string& newPath);
};

#define FINISHED_BOOKS FinishedBooksStore::getInstance()
