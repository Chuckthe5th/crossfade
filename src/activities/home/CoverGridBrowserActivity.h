#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LibraryScanner.h"

// Alternative to FileBrowserActivity: a paginated grid of cover thumbnails for
// every book on the SD card, selected via SETTINGS.fileBrowserView (default is
// the stock FileBrowserActivity; this is strictly opt-in). Reached only from
// HomeActivity::onFileBrowserOpen() -- see ActivityManager::goToCoverGridBrowser().
class CoverGridBrowserActivity final : public Activity {
  struct GridCell {
    std::string title;
    std::string coverThumbPath;  // empty if this book has no usable cover
    bool hasCover = false;
  };

  // Two independent axes: buttonNavigator (side Up/Down) moves by row and owns
  // the long-press page jump; columnNavigator (front Left/Right) moves within
  // the current row. Kept separate so ButtonNavigator's held-time bookkeeping
  // for one axis's continuous-hold detection can't interfere with the other's.
  ButtonNavigator buttonNavigator;
  ButtonNavigator columnNavigator;

  std::vector<LibraryScanner::Entry> books;
  int selectedIndex = 0;
  // Flat index highlighted in the framebuffer as of the last completed render,
  // and whether a full page has ever been composed. EINK_DISPLAY_SINGLE_BUFFER_MODE
  // means the framebuffer persists between renders, so a same-page selection
  // move only needs to erase the old highlight and draw the new one -- see the
  // fast path at the top of render().
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
  int coverHeight = 0;
  int gridLeft = 0;
  int gridTop = 0;

  // Metadata/cover for only the currently visible page, resolved lazily (see
  // ensurePageLoaded). loadedPageStart tracks which page pageCells holds.
  std::vector<GridCell> pageCells;
  int loadedPageStart = -1;

  void computeGridGeometry();
  void loadBooks();
  // Resolves the current page's metadata/covers synchronously (called at the
  // top of render(), before anything is drawn). Returns via resolveCell whether
  // each cell needed its thumbnail generated, so a loading popup only appears
  // -- and only refreshes the panel -- when generation is actually happening;
  // an already-cached page resolves silently and costs zero extra refreshes.
  void ensurePageLoaded();
  // Returns true if this cell's thumbnail had to be generated (cache miss).
  bool resolveCell(const std::string& path, GridCell& cell) const;
  void drawCell(int flatIndex, int x, int y, bool selected) const;
  // Top-left origin (in screen coordinates) of flatIndex's cell within the page
  // starting at pageStart. Shared by the full-grid compose and the
  // selection-only fast path so both draw from the same geometry.
  void cellOrigin(int flatIndex, int pageStart, int& outX, int& outY) const;
  // Header title + page-position subtitle for the given book. Shared by the
  // full-grid compose and the selection-only fast path.
  void computeHeaderText(int flatIndex, int pageStart, std::string& outTitle, std::string& outSubtitle) const;
  // Row/column stepping over the flattened book list, treated as a grid of
  // `cols` columns where only the last row may be short. Both row steps wrap
  // (bottom row wraps to row 0 same column and vice versa); column steps wrap
  // within the current row only, never crossing into another row.
  int stepRowDown() const;
  int stepRowUp() const;
  int stepColumn(int delta) const;
  // 2D analogue of Activity::handleListTouch() for a grid instead of a 1D list:
  // maps a touch point to a flat book index via cellWidth/cellHeight/gridLeft/
  // gridTop. Untested on hardware -- no C3 target (X3/X4) has touch.
  int hitTestCell(int tx, int ty) const;

 public:
  explicit CoverGridBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CoverGridBrowser", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
