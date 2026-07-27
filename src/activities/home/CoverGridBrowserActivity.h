#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LibraryGrouping.h"
#include "util/LongPressAction.h"

// Alternative to FileBrowserActivity AND RecentBooksActivity: a paginated grid
// of cover thumbnails, either the whole SD card (SETTINGS.fileBrowserView) or
// the recent-books list (SETTINGS.recentBooksView); both default to the stock
// list activity, this is strictly opt-in. Reached from HomeActivity::
// onFileBrowserOpen()/onRecentsOpen() -- see ActivityManager::
// goToCoverGridBrowser()/goToCoverGridRecentBooks(). Everything below is
// source-agnostic except loadBooks() (where the list comes from), the initial
// selection in onEnter() (last-read search vs. index 0 -- redundant for
// RecentBooks, which is already MRU-ordered), and the empty-state message.
//
// Series grouping (SETTINGS.groupBySeries) only ever applies to Source::Library -- RecentBooks
// keeps its own MRU semantics unchanged and never groups (see loadBooks()). When entries collapse
// into a series (see LibraryGrouping), selecting one drills into a second-level page listing that
// series' books in index order, rendered with the exact same grid code -- currentEntries() is the
// only thing that differs between the top-level page and a series page.
class CoverGridBrowserActivity final : public Activity {
 public:
  // Mirrors FileBrowserActivity::Mode's role in the same directory: one
  // activity, a small enum picking where its data comes from, instead of a
  // second near-duplicate class.
  enum class Source { Library, RecentBooks };

 private:
  // Two independent axes: primaryNavigator moves by a full row/column and owns the long-press page
  // jump; secondaryNavigator moves within the current row/column only. Which physical buttons each
  // is bound to depends on SETTINGS.coverGridDirection (side Up/Down + primary=cols in Vertical,
  // front Left/Right + primary=rows in Horizontal -- see loop()). Kept as two instances so
  // ButtonNavigator's held-time bookkeeping for one axis's continuous-hold detection can't
  // interfere with the other's.
  ButtonNavigator primaryNavigator;
  ButtonNavigator secondaryNavigator;
  LongPressAction longPressAction;

  const Source source;
  std::vector<LibraryGrouping::Entry> topLevelEntries;
  int selectedIndex = 0;
  // >= 0 while viewing a series page: the index into topLevelEntries of the series being viewed.
  // -1 at the top level. currentEntries() and currentCount() are the only places that need to
  // check this -- everything else (navigation, drawing, pagination) operates on whichever list
  // they return, unaware of which level it's looking at.
  int seriesTopIndex = -1;
  // selectedIndex at the top level, saved when drilling into a series and restored on Back out of
  // it -- "Back from a series page returns to the parent list with the series entry re-selected."
  int savedTopLevelSelectedIndex = 0;

  int lastRenderedIndex = -1;
  bool hasComposedPage = false;

  // Grid geometry, recomputed every onEnter() from the live runtime display
  // size/orientation -- never cached across activity instances, never hardcoded,
  // so the same binary lays out correctly on both X3 and X4.
  int cols = 0;
  int rows = 0;
  int itemsPerPage = 0;
  int cellWidth = 0;
  int cellHeight = 0;
  // Exact pixel box thumbnails are generated at and drawn into -- coverWidth
  // matches drawCell's innerW exactly, so the cache is generated at the same
  // size it's streamed at and drawBitmap1Bit never has to rescale.
  int coverWidth = 0;
  int coverHeight = 0;
  int gridLeft = 0;
  int gridTop = 0;

  // Metadata/cover for only the currently visible page, resolved lazily (see
  // ensurePageLoaded). loadedPageStart tracks which page pageCells holds.
  int loadedPageStart = -1;

  std::vector<LibraryGrouping::Entry>& currentEntries();
  const std::vector<LibraryGrouping::Entry>& currentEntries() const;
  int currentCount() const { return static_cast<int>(currentEntries().size()); }

  void computeGridGeometry();
  // Source::Library scans (or, when SETTINGS.groupBySeries is on and a valid index exists,
  // reads+collapses) the whole SD card; Source::RecentBooks copies RECENT_BOOKS.getBooks() as-is
  // (already MRU-ordered, already capped, title/author already known, never grouped) -- the only
  // method that branches on `source`.
  void loadBooks();
  // Resolves the current page's metadata/covers synchronously (called at the top of render(),
  // before anything is drawn) via the shared LibraryGrouping::resolvePage. A no-op for any entry
  // already resolved -- always true for text in grouped mode, letting this run unconditionally
  // regardless of mode without the caller needing to know which one it's in.
  void ensurePageLoaded();
  void drawCell(int flatIndex, int x, int y, bool selected) const;
  // Stack indicator for a collapsed series entry (>= 2 members): three lines of decreasing height
  // drawn in the CoverGridGeometry::SERIES_STRIP_WIDTH strip reserved on every cell's right edge
  // (see computeGridGeometry) -- beside the cover, never over it, regardless of the cover's own
  // aspect ratio. Only called for series entries; the strip itself is left blank on every other
  // cell.
  void drawStackOverlay(int stripX, int stripY, int stripW, int stripH) const;
  // Top-left origin (in screen coordinates) of flatIndex's cell within the page
  // starting at pageStart. Shared by the full-grid compose and the
  // selection-only fast path so both draw from the same geometry.
  void cellOrigin(int flatIndex, int pageStart, int& outX, int& outY) const;
  // Header title + page-position subtitle for the given entry. A series entry (only reachable at
  // the top level -- drilling in replaces currentEntries() with its members) shows its name with
  // no subtitle; an individual book (standalone or a drilled-in series member) shows its title and
  // chapter-progress subtitle exactly as before grouping existed.
  void computeHeaderText(int flatIndex, std::string& outTitle, std::string& outSubtitle) const;
  // Axis-agnostic grid stepping over the current page's entries, expressed as `primaryCount`
  // secondary-axis positions per primary-axis group -- `cols` in Vertical mode (a "row" spans
  // `cols` entries, primary axis = rows), `rows` in Horizontal mode (a "column" spans `rows`
  // entries, primary axis = columns). Only the very last primary group can be short (a partial
  // last row/column), so stepPrimaryForward/Backward clamp into it rather than overshooting past
  // it when wrapping at the true first/last group; stepSecondary wraps within the current primary
  // group only, never crossing into another one.
  int stepPrimaryForward(int primaryCount) const;
  int stepPrimaryBackward(int primaryCount) const;
  int stepSecondary(int delta, int primaryCount) const;
  // SETTINGS.coverGridDirection == COVER_GRID_HORIZONTAL. Checked from render()/cellOrigin()/
  // hitTestCell()/loop() to pick column-major vs row-major fill and which physical buttons drive
  // the primary/secondary axes -- see the primaryNavigator/secondaryNavigator comment above.
  bool horizontalDirection() const;
  // 2D analogue of Activity::handleListTouch() for a grid instead of a 1D list:
  // maps a touch point to a flat book index via cellWidth/cellHeight/gridLeft/
  // gridTop. Untested on hardware -- no C3 target (X3/X4) has touch.
  int hitTestCell(int tx, int ty) const;

  void enterSeries(int topLevelIndex);
  void exitSeries();
  // Opens the reader on the selected entry, or drills into it if it's a series.
  void activateSelected();
  // Opens the per-book context menu (long-press Confirm) for a standalone entry -- never called
  // for a series entry, which represents multiple books, not one. Available.removeFromRecents is
  // set for Source::RecentBooks only. On a change (delete/remove), reloads the list and -- since
  // the list just changed underneath any series drill-down, which the deleted/removed book could
  // have been part of -- snaps back to the top level rather than trying to preserve it.
  void openContextMenu(const LibraryGrouping::Entry& entry);
  // Two-level last-read search: if the last-read book is a standalone top-level entry, selects
  // it. If it's inside a series, drills directly into that series with the book selected and
  // remembers the series' top-level position for Back -- landing one level removed from the
  // actual reading position would defeat the point of a last-read shortcut.
  void selectLastRead();

 public:
  explicit CoverGridBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    Source source = Source::Library)
      : Activity(source == Source::RecentBooks ? "CoverGridRecentBooks" : "CoverGridBrowser", renderer, mappedInput),
        source(source) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
