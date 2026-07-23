#include "CoverGridBrowserActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Xtc.h>

#include <algorithm>

#include "../reader/EpubReaderUtils.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

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

// Mirrors GfxRenderer::drawBitmap1Bit's own fit-within-box scale calculation
// (shrink-only, bound by whichever dimension needs it more) so the caller can
// compute the resulting drawn size and center it -- drawBitmap1Bit itself always
// top-left anchors at the (x, y) it's given.
float fitScale(int srcW, int srcH, int maxW, int maxH) {
  float scale = 1.0f;
  if (maxW > 0 && srcW > maxW) {
    scale = static_cast<float>(maxW) / static_cast<float>(srcW);
  }
  if (maxH > 0 && srcH > maxH) {
    scale = std::min(scale, static_cast<float>(maxH) / static_cast<float>(srcH));
  }
  return scale;
}
}  // namespace

void CoverGridBrowserActivity::computeGridGeometry() {
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

void CoverGridBrowserActivity::loadBooks() { books = LibraryScanner::scanAllBooks("/", MAX_GRID_BOOKS); }

void CoverGridBrowserActivity::resolveCell(const std::string& path, GridCell& cell) const {
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

void CoverGridBrowserActivity::ensurePageLoaded() {
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

void CoverGridBrowserActivity::onEnter() {
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

  requestUpdate();
}

void CoverGridBrowserActivity::onExit() {
  Activity::onExit();
  books.clear();
  pageCells.clear();
}

int CoverGridBrowserActivity::hitTestCell(const int tx, const int ty) const {
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

int CoverGridBrowserActivity::stepRowDown() const {
  const int n = static_cast<int>(books.size());
  if (n == 0 || cols <= 0) {
    return selectedIndex;
  }
  const int col = selectedIndex % cols;
  const int nextRowStart = (selectedIndex / cols + 1) * cols;
  const int candidate = nextRowStart + col;
  return candidate < n ? candidate : col;  // wrap to row 0, same column
}

int CoverGridBrowserActivity::stepRowUp() const {
  const int n = static_cast<int>(books.size());
  if (n == 0 || cols <= 0) {
    return selectedIndex;
  }
  const int col = selectedIndex % cols;
  const int row = selectedIndex / cols;
  if (row > 0) {
    return (row - 1) * cols + col;
  }
  // Wrap to the bottom row; if it's short of this column (partial last row),
  // the row above it is guaranteed full since only the last row can be short.
  const int lastRow = (n - 1) / cols;
  const int candidate = lastRow * cols + col;
  return candidate < n ? candidate : candidate - cols;
}

int CoverGridBrowserActivity::stepColumn(const int delta) const {
  const int n = static_cast<int>(books.size());
  if (n == 0 || cols <= 0) {
    return selectedIndex;
  }
  const int rowStart = (selectedIndex / cols) * cols;
  const int rowCount = std::min(cols, n - rowStart);
  const int localCol = selectedIndex - rowStart;
  const int newLocalCol = (localCol + delta + rowCount) % rowCount;
  return rowStart + newLocalCol;
}

void CoverGridBrowserActivity::loop() {
  using Button = MappedInputManager::Button;

  // The grid has no directory nesting (it flattens the whole card), so unlike
  // FileBrowserActivity there's no "go up one level" state -- Back always
  // returns straight to Home, matching FileBrowserActivity's root-level Back.
  if (mappedInput.wasReleased(Button::Back)) {
    onGoHome();
    return;
  }

  if (books.empty()) {
    return;
  }

  const int totalBooks = static_cast<int>(books.size());

  // Rows: side Up/Down. Single step on release (matching FileBrowserActivity/
  // RecentBooksActivity's release-edge convention for paginated lists);
  // continuous hold reuses their exact nextPageIndex/previousPageIndex page-jump
  // formula unchanged, which lands on jumping a full itemsPerPage -- i.e. the
  // next/previous page of rows.
  buttonNavigator.onRelease({Button::Down}, [this] {
    selectedIndex = stepRowDown();
    requestUpdate();
  });
  buttonNavigator.onRelease({Button::Up}, [this] {
    selectedIndex = stepRowUp();
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

  // Columns: front Left/Right move within the current row only. No stock list
  // activity has a second axis to mirror a long-press convention from, so this
  // is single-step only.
  columnNavigator.onRelease({Button::Right}, [this] {
    selectedIndex = stepColumn(1);
    requestUpdate();
  });
  columnNavigator.onRelease({Button::Left}, [this] {
    selectedIndex = stepColumn(-1);
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

void CoverGridBrowserActivity::drawCell(const int flatIndex, const int x, const int y, const bool selected) const {
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
      if (Storage.openFileForRead("CGB", cell.coverThumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          // Center the cover in its box: drawBitmap1Bit only ever shrinks (never
          // stretches) to fit maxWidth/maxHeight, binding on whichever dimension
          // needs it more, so a cover whose aspect ratio doesn't match the cell's
          // is left/top anchored unless we offset the origin ourselves.
          const float scale = fitScale(bitmap.getWidth(), bitmap.getHeight(), innerW, coverHeight);
          const int renderedW = static_cast<int>(bitmap.getWidth() * scale);
          const int renderedH = static_cast<int>(bitmap.getHeight() * scale);
          const int coverX = innerX + (innerW - renderedW) / 2;
          const int coverY = innerY + (coverHeight - renderedH) / 2;
          renderer.drawBitmap1Bit(bitmap, coverX, coverY, innerW, coverHeight);
          drewCover = true;
        }
      }
    }
  }

  // The outlined/filled card is the no-cover fallback only -- a cell with a real
  // cover gets no frame around it.
  if (!drewCover) {
    if (selected) {
      renderer.fillRect(innerX, innerY, innerW, coverHeight);
    } else {
      renderer.drawRect(innerX, innerY, innerW, coverHeight);
    }
  }

  if (!title.empty()) {
    const std::string truncated = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), innerW);
    renderer.drawText(SMALL_FONT_ID, innerX, innerY + coverHeight + CARD_PADDING, truncated.c_str());
  }

  if (selected) {
    renderer.drawRect(innerX - 2, innerY - 2, innerW + 4, coverHeight + 4, 2, true);
  }
}

void CoverGridBrowserActivity::render(RenderLock&&) {
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

  // Back always returns Home from here (no directory nesting to go up), so the
  // hint reads "Home" -- same semantics as FileBrowserActivity's root-level Back.
  // Front Left/Right now move within the row (side Up/Down move by row), so the
  // hint text is "Left"/"Right", not the "Up"/"Down" stock lists use for the
  // same physical buttons.
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else {
    ensurePageLoaded();
  }
}
