#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LibraryGrouping.h"
#include "util/LongPressAction.h"

// Third fileBrowserView option alongside the stock FileBrowserActivity (Files) and
// CoverGridBrowserActivity (Covers): a flat, paginated title+author list over the whole library,
// using the same row presentation RecentBooksActivity already draws via GUI.drawList. Reached from
// HomeActivity::onFileBrowserOpen() -- see ActivityManager::goToLibraryList().
//
// Like CoverGridBrowserActivity's Library source, this flattens the whole SD card rather than
// preserving folder navigation -- a title+author row has nothing useful to show for a folder.
//
// Series grouping (SETTINGS.groupBySeries): when a group of entries collapses into a series (see
// LibraryGrouping), selecting it drills into a second-level page listing that series' books in
// index order -- title as headline, author as subtitle, exactly like any other row, rendered with
// the same GUI.drawList call. currentEntries() is the only thing that differs between the
// top-level page and a series page.
class LibraryListActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;
  LongPressAction longPressAction;

  std::vector<LibraryGrouping::Entry> topLevelEntries;
  int selectedIndex = 0;
  // >= 0 while viewing a series page: the index into topLevelEntries of the series being viewed.
  // -1 at the top level.
  int seriesTopIndex = -1;
  // selectedIndex at the top level, saved when drilling into a series and restored on Back out of
  // it.
  int savedTopLevelSelectedIndex = 0;

  // Which page ensurePageLoaded() last resolved -- lets a selection move within an already-
  // resolved page, or a return to a previously visited page, skip re-resolving. Always trivially
  // satisfied in grouped mode: every entry's text is already known from the index.
  int loadedPageStart = -1;

  std::vector<LibraryGrouping::Entry>& currentEntries();
  const std::vector<LibraryGrouping::Entry>& currentEntries() const;
  int currentCount() const { return static_cast<int>(currentEntries().size()); }

  // Library scan (or, when SETTINGS.groupBySeries is on and a valid index exists, the collapsed
  // index) sorted by filename.
  void loadBooks();
  // Resolves the current page's title/author synchronously (called at the top of render(), before
  // anything is drawn) via the shared LibraryGrouping::resolvePage, with no cover box requested --
  // unlike the grid, this never generates or reads a thumbnail. A no-op in grouped mode (every
  // entry's text is already known from the index).
  void ensurePageLoaded(int itemsPerPage);
  std::string rowTitle(int index) const;
  std::string rowAuthor(int index) const;
  // Header title + page-position subtitle. A series entry (only reachable at the top level) shows
  // its name with no subtitle; an individual book (standalone or drilled-in member) shows its
  // title and chapter-progress subtitle.
  void computeHeaderText(int index, std::string& outTitle, std::string& outSubtitle) const;

  void enterSeries(int topLevelIndex);
  void exitSeries();
  void activateSelected();
  // Two-level last-read search -- see CoverGridBrowserActivity::selectLastRead for the reasoning;
  // identical here since both views share LibraryGrouping's collapsed shape.
  void selectLastRead();
  // Opens the per-book context menu (long-press Confirm) -- see
  // CoverGridBrowserActivity::openContextMenu for the full reasoning (series-entry gating, why a
  // change snaps back to the top level). Never offers Remove from Recents: this view is always the
  // whole library, never Recent Books.
  void openContextMenu(const LibraryGrouping::Entry& entry);

 public:
  explicit LibraryListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibraryList", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
