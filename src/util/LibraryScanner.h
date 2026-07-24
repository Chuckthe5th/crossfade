#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace LibraryScanner {

// Shared cap for anything that walks the whole library (CoverGridBrowserActivity's Covers grid,
// LibraryListActivity's Titles list, LibraryIndexBuilder's delta rebuild) -- keeping it one symbol
// means they can never disagree about how much of a very large card they each cover.
constexpr size_t MAX_LIBRARY_BOOKS = 2000;

struct Entry {
  std::string path;
  // Fingerprint, populated only when scanAllBooks() is called with collectFingerprint=true (left
  // at 0 otherwise -- callers that don't ask for it pay nothing extra during the walk). size is
  // the file's byte size; fatDateTime packs HalFile::getModifyTime()'s (date, time) as
  // (date << 16) | time. Note this firmware never registers an SdFat date/time callback (no
  // RTC/NTP wiring), so files it wrote itself (File Transfer uploads, OPDS downloads) always
  // report fatDateTime == 0 -- only files copied on via a PC carry a real timestamp. size still
  // discriminates most real edits even when fatDateTime can't.
  uint32_t size = 0;
  uint32_t fatDateTime = 0;
};

// Recursively walks `root` for every book file (epub/xtc/txt/md — bmp is a viewer,
// not a book, so it is excluded, matching NextBookFinder), applying the same
// hidden-file/System-Volume-Information rules as FileBrowserActivity::loadFiles.
// Iterative (explicit directory worklist, not C++ recursion) so arbitrarily deep
// folder nesting cannot grow the task stack. Each directory's children are sorted
// with FsHelpers::sortFileList before its subfolders are queued, so results are
// stable and predictable, though not a single global sort across the whole tree.
//
// Stops after maxBooks entries and logs via LOG_INF if the card holds more, so a
// capped scan is never silently mistaken for full coverage.
//
// collectFingerprint: when true, also captures each book file's size/modify-time while it's
// already open during the walk (no extra SD open) -- used by the library index's delta rebuild
// to detect changed files without reopening every path in a second pass. Callers that only need
// paths (the Covers grid, the Titles list) leave this false.
std::vector<Entry> scanAllBooks(const std::string& root, size_t maxBooks, bool collectFingerprint = false);

// Re-sorts a scanAllBooks() result by filename (FsHelpers::naturalLess, numeric-aware and
// case-insensitive) instead of the scan's directory tree-walk order. Metadata-free -- filenames are
// already known, so this costs nothing beyond the comparisons themselves -- unlike sorting by title,
// which would require resolving every entry's metadata first. Used by both flattened-library views
// (CoverGridBrowserActivity's Covers grid and LibraryListActivity's Titles list) so toggling between
// them presents the same library in the same order.
void sortByFilename(std::vector<Entry>& entries);

}  // namespace LibraryScanner
