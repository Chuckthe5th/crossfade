#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace LibraryScanner {

struct Entry {
  std::string path;
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
std::vector<Entry> scanAllBooks(const std::string& root, size_t maxBooks);

// Re-sorts a scanAllBooks() result by filename (FsHelpers::naturalLess, numeric-aware and
// case-insensitive) instead of the scan's directory tree-walk order. Metadata-free -- filenames are
// already known, so this costs nothing beyond the comparisons themselves -- unlike sorting by title,
// which would require resolving every entry's metadata first. Used by both flattened-library views
// (CoverGridBrowserActivity's Covers grid and LibraryListActivity's Titles list) so toggling between
// them presents the same library in the same order.
void sortByFilename(std::vector<Entry>& entries);

}  // namespace LibraryScanner
