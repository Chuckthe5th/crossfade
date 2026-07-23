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

}  // namespace LibraryScanner
