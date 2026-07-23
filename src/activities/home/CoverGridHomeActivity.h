#pragma once

#include <string>
#include <vector>

#include "components/OptionPopup.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LibraryScanner.h"

// Alternative Home screen: a paginated grid of cover thumbnails for every book on
// the SD card, selected via SETTINGS.homeScreenStyle (default is the stock
// HomeActivity; this is strictly opt-in). See ActivityManager::goHome().
class CoverGridHomeActivity final : public Activity {
  enum class MenuAction { FileBrowser, Recents, OpdsBrowser, FileTransfer, Settings };

  struct GridCell {
    std::string title;
    std::string coverThumbPath;  // empty if this book has no usable cover
    bool hasCover = false;
  };

  ButtonNavigator buttonNavigator;
  OptionPopup backMenu;
  std::vector<MenuAction> backMenuActions;

  std::vector<LibraryScanner::Entry> books;
  int selectedIndex = 0;
  bool firstRenderDone = false;
  // Home can be entered while Back is still held (e.g. leaving Settings with
  // Back): ignore that stale release until a fresh press is seen here, same
  // guard HomeActivity uses.
  bool backPressSeen = false;

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
  int hitTestCell(int tx, int ty) const;
  void openBackMenu();

  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

 public:
  explicit CoverGridHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CoverGridHome", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
