#include "CoverGridHomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "../reader/EpubReaderUtils.h"

namespace {
// Target cell size grid columns/rows are derived from at runtime -- never a
// hardcoded panel width/height, so the same binary lays out correctly on X3
// (792x528) and X4 (800x480).
constexpr int TARGET_CELL_WIDTH = 140;
constexpr int TARGET_CELL_HEIGHT = 190;
constexpr int CARD_PADDING = 6;
// Bounds the recursive SD-card walk's RAM: each entry is one full path
// std::string. At this cap, worst case is roughly 2000 * ~80 bytes =~ 160 KB,
// held only while this activity is on screen and freed in onExit(). Typical
// libraries are far smaller; LibraryScanner logs if the card holds more.
constexpr size_t MAX_GRID_BOOKS = 2000;

std::string filenameWithoutExtension(const std::string& path) {
  size_t start = path.find_last_of('/');
  start = (start == std::string::npos) ? 0 : start + 1;
  size_t end = path.find_last_of('.');
  if (end == std::string::npos || end < start) {
    end = path.size();
  }
  return path.substr(start, end - start);
}
}  // namespace

void CoverGridHomeActivity::computeGridGeometry() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  int viewTop, viewRight, viewBottom, viewLeft;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  gridTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  gridLeft = viewLeft;
  const int gridWidth = pageWidth - viewLeft - viewRight;
  const int gridBottom = pageHeight - viewBottom - metrics.buttonHintsHeight;
  const int gridHeight = std::max(TARGET_CELL_HEIGHT, gridBottom - gridTop);

  cols = std::max(2, gridWidth / TARGET_CELL_WIDTH);
  cellWidth = gridWidth / cols;
  rows = std::max(1, gridHeight / TARGET_CELL_HEIGHT);
  cellHeight = gridHeight / rows;
  itemsPerPage = cols * rows;

  const int titleAreaHeight = renderer.getLineHeight(SMALL_FONT_ID) + CARD_PADDING;
  coverHeight = std::max(20, cellHeight - CARD_PADDING * 2 - titleAreaHeight);
}

void CoverGridHomeActivity::loadBooks() { books = LibraryScanner::scanAllBooks("/", MAX_GRID_BOOKS); }

void CoverGridHomeActivity::resolveCell(const std::string& path, GridCell& cell) const {
  cell.title.clear();
  cell.coverThumbPath.clear();
  cell.hasCover = false;

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    bool haveMetadata = epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true);
    if (!haveMetadata) {
      // Never opened before: no book.bin yet. A full build (spine + TOC) would be
      // too expensive to pay per book just to populate a grid tile, so fall back
      // to a lightweight OPF-only parse (title/author/cover href, no spine/TOC).
      epub.setupCacheDir();
      haveMetadata = epub.loadMetadataOnly();
    }
    if (haveMetadata) {
      cell.title = epub.getTitle();
      const std::string thumbPath = epub.getThumbBmpPath(coverHeight);
      if (!Storage.exists(thumbPath.c_str())) {
        epub.generateThumbBmp(coverHeight);
      }
      if (Storage.exists(thumbPath.c_str())) {
        cell.coverThumbPath = thumbPath;
        cell.hasCover = true;
      }
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      cell.title = xtc.getTitle();
      const std::string thumbPath = xtc.getThumbBmpPath(coverHeight);
      if (!Storage.exists(thumbPath.c_str())) {
        xtc.setupCacheDir();
        xtc.generateThumbBmp(coverHeight);
      }
      if (Storage.exists(thumbPath.c_str())) {
        cell.coverThumbPath = thumbPath;
        cell.hasCover = true;
      }
    }
  }
  // TXT/MD have no cover concept -- title-only fallback card below.

  if (cell.title.empty()) {
    cell.title = filenameWithoutExtension(path);
  }
}

void CoverGridHomeActivity::ensurePageLoaded() {
  if (books.empty() || itemsPerPage <= 0) {
    return;
  }
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  if (pageStart == loadedPageStart) {
    return;
  }

  const int pageEnd = std::min(static_cast<int>(books.size()), pageStart + itemsPerPage);
  pageCells.assign(pageEnd - pageStart, GridCell{});

  Rect popupRect;
  bool showingLoading = false;
  const int count = pageEnd - pageStart;
  for (int i = pageStart; i < pageEnd; i++) {
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + (i - pageStart) * 90 / std::max(1, count));
    resolveCell(books[i].path, pageCells[i - pageStart]);
  }

  loadedPageStart = pageStart;
  requestUpdate();
}

void CoverGridHomeActivity::onEnter() {
  Activity::onEnter();

  computeGridGeometry();
  loadBooks();

  selectedIndex = 0;
  if (!books.empty()) {
    const auto& recents = RECENT_BOOKS.getBooks();
    if (!recents.empty()) {
      const auto it = std::find_if(books.begin(), books.end(),
                                   [&recents](const LibraryScanner::Entry& e) { return e.path == recents[0].path; });
      if (it != books.end()) {
        selectedIndex = static_cast<int>(std::distance(books.begin(), it));
      }
    }
  }

  loadedPageStart = -1;
  pageCells.clear();
  firstRenderDone = false;
  backPressSeen = false;

  requestUpdate();
}

void CoverGridHomeActivity::onExit() {
  Activity::onExit();
  books.clear();
  pageCells.clear();
}

void CoverGridHomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }
void CoverGridHomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }
void CoverGridHomeActivity::onSettingsOpen() { activityManager.goToSettings(); }
void CoverGridHomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }
void CoverGridHomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void CoverGridHomeActivity::openBackMenu() {
  backMenuActions.clear();
  std::vector<std::string> options;
  options.reserve(5);

  options.emplace_back(tr(STR_BROWSE_FILES));
  backMenuActions.push_back(MenuAction::FileBrowser);
  options.emplace_back(tr(STR_MENU_RECENT_BOOKS));
  backMenuActions.push_back(MenuAction::Recents);
  if (OPDS_STORE.hasServers()) {
    options.emplace_back(tr(STR_OPDS_BROWSER));
    backMenuActions.push_back(MenuAction::OpdsBrowser);
  }
  options.emplace_back(tr(STR_FILE_TRANSFER));
  backMenuActions.push_back(MenuAction::FileTransfer);
  options.emplace_back(tr(STR_SETTINGS_TITLE));
  backMenuActions.push_back(MenuAction::Settings);

  backMenu.show(StrId::STR_MENU, options, 0, [this](int index) {
    if (index < 0 || index >= static_cast<int>(backMenuActions.size())) {
      return;
    }
    switch (backMenuActions[index]) {
      case MenuAction::FileBrowser:
        onFileBrowserOpen();
        break;
      case MenuAction::Recents:
        onRecentsOpen();
        break;
      case MenuAction::OpdsBrowser:
        onOpdsBrowserOpen();
        break;
      case MenuAction::FileTransfer:
        onFileTransferOpen();
        break;
      case MenuAction::Settings:
        onSettingsOpen();
        break;
    }
  });
  requestUpdate();
}

int CoverGridHomeActivity::hitTestCell(const int tx, const int ty) const {
  if (ty < gridTop || tx < gridLeft || cellWidth <= 0 || cellHeight <= 0) {
    return -1;
  }
  const int col = (tx - gridLeft) / cellWidth;
  const int row = (ty - gridTop) / cellHeight;
  if (col < 0 || col >= cols || row < 0 || row >= rows) {
    return -1;
  }
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  const int flatIndex = pageStart + row * cols + col;
  if (flatIndex >= static_cast<int>(books.size())) {
    return -1;
  }
  return flatIndex;
}

void CoverGridHomeActivity::loop() {
  if (backMenu.isActive()) {
    backMenu.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  using Button = MappedInputManager::Button;

  if (mappedInput.wasPressed(Button::Back)) {
    backPressSeen = true;
  }
  if (mappedInput.wasReleased(Button::Back) && backPressSeen) {
    openBackMenu();
    return;
  }

  if (books.empty()) {
    return;
  }

  const int totalBooks = static_cast<int>(books.size());

  buttonNavigator.onPress({Button::Down}, [this, totalBooks] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalBooks);
    requestUpdate();
  });
  buttonNavigator.onPress({Button::Up}, [this, totalBooks] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalBooks);
    requestUpdate();
  });
  buttonNavigator.onContinuous({Button::Down}, [this, totalBooks] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, totalBooks, itemsPerPage);
    requestUpdate();
  });
  buttonNavigator.onContinuous({Button::Up}, [this, totalBooks] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, totalBooks, itemsPerPage);
    requestUpdate();
  });

  if (mappedInput.wasReleased(Button::Confirm)) {
    activityManager.goToReader(books[selectedIndex].path);
    return;
  }

  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    const int hit = hitTestCell(tx, ty);
    if (hit >= 0 && hit != selectedIndex) {
      selectedIndex = hit;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = hitTestCell(tx, ty);
    if (hit >= 0) {
      selectedIndex = hit;
      activityManager.goToReader(books[selectedIndex].path);
    }
  }
}

void CoverGridHomeActivity::drawCell(const int flatIndex, const int x, const int y, const bool selected) const {
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  const int cellIdx = flatIndex - pageStart;
  const bool haveData = loadedPageStart == pageStart && cellIdx >= 0 && cellIdx < static_cast<int>(pageCells.size());

  const int innerX = x + CARD_PADDING;
  const int innerY = y + CARD_PADDING;
  const int innerW = cellWidth - CARD_PADDING * 2;

  bool drewCover = false;
  std::string title;
  if (haveData) {
    const GridCell& cell = pageCells[cellIdx];
    title = cell.title;
    if (cell.hasCover) {
      HalFile file;
      if (Storage.openFileForRead("CGH", cell.coverThumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap1Bit(bitmap, innerX, innerY, innerW, coverHeight);
          drewCover = true;
        }
      }
    }
  }

  if (drewCover) {
    renderer.drawRect(innerX, innerY, innerW, coverHeight);
  } else if (selected) {
    renderer.fillRect(innerX, innerY, innerW, coverHeight);
  } else {
    renderer.drawRect(innerX, innerY, innerW, coverHeight);
  }

  if (!title.empty()) {
    const std::string truncated = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), innerW);
    renderer.drawText(SMALL_FONT_ID, innerX, innerY + coverHeight + CARD_PADDING, truncated.c_str());
  }

  if (selected) {
    renderer.drawRect(innerX - 2, innerY - 2, innerW + 4, coverHeight + 4, 2, true);
  }
}

void CoverGridHomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();

  std::string headerTitle;
  char subtitleBuf[32] = "";
  const char* subtitle = nullptr;

  if (books.empty()) {
    headerTitle = tr(STR_NO_BOOKS_FOUND);
  } else {
    const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
    const int cellIdx = selectedIndex - pageStart;
    if (loadedPageStart == pageStart && cellIdx < static_cast<int>(pageCells.size())) {
      headerTitle = pageCells[cellIdx].title;
    }

    const std::string& selectedPath = books[selectedIndex].path;
    if (FsHelpers::hasEpubExtension(selectedPath)) {
      const Epub epub(selectedPath, "/.crosspoint");
      int spineIndex = 0;
      int pageNumber = 0;
      int pageCount = 0;
      if (EpubReaderUtils::loadProgress(epub.getCachePath(), spineIndex, pageNumber, pageCount) && pageCount > 0) {
        snprintf(subtitleBuf, sizeof(subtitleBuf), "%d/%d", pageNumber + 1, pageCount);
        subtitle = subtitleBuf;
      }
    }
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                headerTitle.empty() ? nullptr : headerTitle.c_str(), subtitle);

  if (!books.empty()) {
    const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        const int flatIndex = pageStart + r * cols + c;
        if (flatIndex >= static_cast<int>(books.size())) {
          continue;
        }
        drawCell(flatIndex, gridLeft + c * cellWidth, gridTop + r * cellHeight, flatIndex == selectedIndex);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  backMenu.render(renderer);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else {
    ensurePageLoaded();
  }
}
