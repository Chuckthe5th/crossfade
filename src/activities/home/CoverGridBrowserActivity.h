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

  ButtonNavigator buttonNavigator;

  std::vector<LibraryScanner::Entry> books;
  int selectedIndex = 0;
  bool firstRenderDone = false;

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
  void ensurePageLoaded();
  void resolveCell(const std::string& path, GridCell& cell) const;
  void drawCell(int flatIndex, int x, int y, bool selected) const;
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
