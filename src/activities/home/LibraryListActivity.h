#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LibraryScanner.h"

// Third fileBrowserView option alongside the stock FileBrowserActivity (Files) and
// CoverGridBrowserActivity (Covers): a flat, paginated title+author list over the whole library,
// using the same row presentation RecentBooksActivity already draws via GUI.drawList. Reached from
// HomeActivity::onFileBrowserOpen() -- see ActivityManager::goToLibraryList().
//
// Like CoverGridBrowserActivity's Library source, this flattens the whole SD card (LibraryScanner)
// rather than preserving folder navigation -- a title+author row has nothing useful to show for a
// folder. Unlike the grid, entries are sorted by filename after the scan (cheap: no metadata needed,
// and a title list is browsed alphabetically) rather than left in scan order.
class LibraryListActivity final : public Activity {
 private:
  struct RowCache {
    std::string title;
    std::string author;
  };

  ButtonNavigator buttonNavigator;

  std::vector<LibraryScanner::Entry> books;
  int selectedIndex = 0;

  // Metadata for only the currently visible page, resolved lazily (see ensurePageLoaded) --
  // mirrors CoverGridBrowserActivity's pageCells/loadedPageStart pattern so a selection move within
  // an already-resolved page, or returning to a previously visited page, costs zero extra SD I/O.
  std::vector<RowCache> pageRows;
  int loadedPageStart = -1;

  // Library scan sorted by filename, most-recent-first initial selection.
  void loadBooks();
  // Resolves the current page's title/author synchronously (called at the top of render(), before
  // anything is drawn) via the shared BookMetadataResolver, with no cover box requested -- unlike
  // the grid, this never generates or reads a thumbnail.
  void ensurePageLoaded(int itemsPerPage);
  std::string rowTitle(int index, int itemsPerPage) const;
  std::string rowAuthor(int index, int itemsPerPage) const;
  // Header title + page-position subtitle for the given book, mirroring
  // CoverGridBrowserActivity::computeHeaderText.
  void computeHeaderText(int index, int itemsPerPage, std::string& outTitle, std::string& outSubtitle) const;

 public:
  explicit LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibraryList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
