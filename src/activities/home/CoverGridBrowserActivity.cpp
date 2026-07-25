#include "CoverGridBrowserActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "../reader/EpubReaderUtils.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/util/BookContextMenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Target cell size grid columns/rows are derived from at runtime -- never a
// hardcoded panel width/height, so the same binary lays out correctly on X3
// (792x528) and X4 (800x480).
constexpr int TARGET_CELL_WIDTH = 140;
constexpr int TARGET_CELL_HEIGHT = 190;
constexpr int CARD_PADDING = 6;
// Matches ButtonNavigator's own continuous-hold repeat interval -- a familiar
// "still working" cadence -- rather than refreshing the panel once per cell.
constexpr uint32_t PROGRESS_UPDATE_INTERVAL_MS = 500;

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

std::vector<LibraryGrouping::Entry>& CoverGridBrowserActivity::currentEntries() {
  if (seriesTopIndex >= 0 && seriesTopIndex < static_cast<int>(topLevelEntries.size())) {
    return topLevelEntries[seriesTopIndex].members;
  }
  return topLevelEntries;
}

const std::vector<LibraryGrouping::Entry>& CoverGridBrowserActivity::currentEntries() const {
  if (seriesTopIndex >= 0 && seriesTopIndex < static_cast<int>(topLevelEntries.size())) {
    return topLevelEntries[seriesTopIndex].members;
  }
  return topLevelEntries;
}

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

  // With titles off, the caption row's space folds back into the cover box itself -- taller
  // covers, not blank space -- which also means a different cache box size (see drawCell()'s
  // Epub::generateThumbBmp(width, height) call): old with-titles thumbnails are simply never
  // looked up at the new size, not explicitly invalidated.
  const int titleAreaHeight = SETTINGS.coversShowTitles ? renderer.getLineHeight(SMALL_FONT_ID) + CARD_PADDING : 0;
  coverHeight = std::max(20, cellHeight - CARD_PADDING * 2 - titleAreaHeight);
  coverWidth = cellWidth - CARD_PADDING * 2;
}

void CoverGridBrowserActivity::loadBooks() {
  topLevelEntries.clear();
  if (source == Source::RecentBooks) {
    // Mirrors RecentBooksActivity::onEnter(): prune entries whose backing files are gone before
    // displaying the list. Never grouped -- RecentBooksStore already has title/author cached, so
    // entries start fully resolved (no lazy text step needed), same as grouped mode gets from the
    // index, just via a different source.
    if (RECENT_BOOKS.pruneMissing()) {
      RECENT_BOOKS.saveToFile();
    }
    const auto& recentBooks = RECENT_BOOKS.getBooks();
    topLevelEntries.reserve(recentBooks.size());
    for (const auto& book : recentBooks) {
      LibraryGrouping::Entry e;
      e.path = book.path;
      e.title = book.title;
      e.author = book.author;
      topLevelEntries.push_back(std::move(e));
    }
    return;
  }
  topLevelEntries = LibraryGrouping::loadLibraryEntries(SETTINGS.groupBySeries);
}

void CoverGridBrowserActivity::ensurePageLoaded() {
  auto& entries = currentEntries();
  if (entries.empty() || itemsPerPage <= 0) {
    return;
  }
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  if (pageStart == loadedPageStart) {
    return;
  }

  // A loading popup -- and the extra e-ink refreshes it costs -- only appears when an entry
  // actually needs its thumbnail generated (LibraryGrouping::resolvePage() is a no-op for
  // anything already resolved, grouped or not). A page whose covers are all already cached
  // resolves silently here; the caller's single end-of-render displayBuffer() is the only refresh
  // that page turn pays. When generation IS needed, progress updates are throttled to at most one
  // every PROGRESS_UPDATE_INTERVAL_MS, not one per cell.
  const int pageEnd = std::min(static_cast<int>(entries.size()), pageStart + itemsPerPage);
  const int count = pageEnd - pageStart;

  Rect popupRect;
  bool showingLoading = false;
  uint32_t lastProgressUpdateMs = 0;
  for (int i = pageStart; i < pageEnd; i++) {
    const bool generated = LibraryGrouping::resolvePage(entries, i, i + 1, coverWidth, coverHeight);
    if (!generated) {
      continue;
    }
    const uint32_t nowMs = millis();
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 10 + (i - pageStart) * 90 / std::max(1, count));
      lastProgressUpdateMs = nowMs;
    } else if (nowMs - lastProgressUpdateMs >= PROGRESS_UPDATE_INTERVAL_MS) {
      GUI.fillPopupProgress(renderer, popupRect, 10 + (i - pageStart) * 90 / std::max(1, count));
      lastProgressUpdateMs = nowMs;
    }
  }

  loadedPageStart = pageStart;
}

void CoverGridBrowserActivity::enterSeries(const int topLevelIndex) {
  savedTopLevelSelectedIndex = topLevelIndex;
  seriesTopIndex = topLevelIndex;
  selectedIndex = 0;
  loadedPageStart = -1;
  lastRenderedIndex = -1;
  hasComposedPage = false;
  requestUpdate();
}

void CoverGridBrowserActivity::exitSeries() {
  seriesTopIndex = -1;
  selectedIndex = savedTopLevelSelectedIndex;
  loadedPageStart = -1;
  lastRenderedIndex = -1;
  hasComposedPage = false;
  requestUpdate();
}

void CoverGridBrowserActivity::activateSelected() {
  auto& entries = currentEntries();
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entries.size())) {
    return;
  }
  if (entries[selectedIndex].isSeries) {
    enterSeries(selectedIndex);
  } else {
    activityManager.goToReader(entries[selectedIndex].path);
  }
}

void CoverGridBrowserActivity::selectLastRead() {
  selectedIndex = 0;
  seriesTopIndex = -1;
  // RecentBooks is already MRU-ordered -- index 0 already is the last-read book, and Library mode
  // is the only source that flattens the whole card, so it's the only one worth actually
  // searching. See CoverGridBrowserActivity.h for why the drill-in-directly behavior matters.
  if (source != Source::Library || topLevelEntries.empty()) {
    return;
  }
  const auto& recents = RECENT_BOOKS.getBooks();
  if (recents.empty()) {
    return;
  }
  const std::string& lastReadPath = recents[0].path;

  for (int i = 0; i < static_cast<int>(topLevelEntries.size()); i++) {
    const auto& entry = topLevelEntries[i];
    if (!entry.isSeries) {
      if (entry.path == lastReadPath) {
        selectedIndex = i;
        return;
      }
      continue;
    }
    for (int m = 0; m < static_cast<int>(entry.members.size()); m++) {
      if (entry.members[m].path == lastReadPath) {
        // SETTINGS.browseBooksStartInSeries gates the drill-in itself: off, land on the series
        // entry at the top level (selectedIndex = i, seriesTopIndex left at -1 from above) instead
        // of entering it. A last-read book outside any series never reaches this branch at all --
        // see the !entry.isSeries case above -- so this setting can't affect it either way.
        if (SETTINGS.browseBooksStartInSeries) {
          seriesTopIndex = i;
          savedTopLevelSelectedIndex = i;
          selectedIndex = m;
        } else {
          selectedIndex = i;
        }
        return;
      }
    }
  }
}

void CoverGridBrowserActivity::openContextMenu(const LibraryGrouping::Entry& entry) {
  const std::string path = entry.path;
  const std::string title = entry.title;
  const bool isRecents = source == Source::RecentBooks;

  auto handler = [this](const ActivityResult& res) {
    const auto* result = std::get_if<BookActionResult>(&res.data);
    if (!result || !result->changed) {
      return;
    }
    seriesTopIndex = -1;  // the list just changed underneath any drill-down; snap back to top level
    loadBooks();
    const auto& entries = currentEntries();
    if (entries.empty()) {
      selectedIndex = 0;
    } else if (selectedIndex >= static_cast<int>(entries.size())) {
      selectedIndex = static_cast<int>(entries.size()) - 1;
    }
    loadedPageStart = -1;
    lastRenderedIndex = -1;
    hasComposedPage = false;
    requestUpdate(true);
  };

  startActivityForResult(
      std::make_unique<BookContextMenuActivity>(renderer, mappedInput, path, title,
                                                BookContextMenuActivity::Available{.removeFromRecents = isRecents}),
      std::move(handler));
}

void CoverGridBrowserActivity::onEnter() {
  Activity::onEnter();

  computeGridGeometry();
  loadBooks();
  selectLastRead();

  loadedPageStart = -1;
  lastRenderedIndex = -1;
  hasComposedPage = false;

  requestUpdate();
}

void CoverGridBrowserActivity::onExit() {
  Activity::onExit();
  topLevelEntries.clear();
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
  if (flatIndex >= currentCount()) {
    return -1;
  }
  return flatIndex;
}

int CoverGridBrowserActivity::stepRowDown() const {
  const int n = currentCount();
  if (n == 0 || cols <= 0) {
    return selectedIndex;
  }
  const int col = selectedIndex % cols;
  const int nextRowStart = (selectedIndex / cols + 1) * cols;
  const int candidate = nextRowStart + col;
  return candidate < n ? candidate : col;  // wrap to row 0, same column
}

int CoverGridBrowserActivity::stepRowUp() const {
  const int n = currentCount();
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
  const int n = currentCount();
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
  // FileBrowserActivity there's no "go up one level" state for folders -- but a series page is
  // one level of nesting, so Back exits that before ever reaching Home.
  if (mappedInput.wasReleased(Button::Back)) {
    if (seriesTopIndex >= 0) {
      exitSeries();
    } else {
      onGoHome();
    }
    return;
  }

  if (currentCount() == 0) {
    return;
  }

  // Long-press Confirm opens the per-book context menu; short-press (below, on release) opens the
  // reader or drills into a series as usual. update() must run every tick regardless of selection
  // validity -- it also owns swallowing the eventual release so it doesn't also fall through to
  // the short-press handler -- so the fire check is a single call, not a guard.
  if (longPressAction.update(mappedInput, Button::Confirm)) {
    const auto& entries = currentEntries();
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(entries.size()) && !entries[selectedIndex].isSeries) {
      openContextMenu(entries[selectedIndex]);
    }
    return;
  }

  const int total = currentCount();

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
  buttonNavigator.onContinuous({Button::Down}, [this, total] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, total, itemsPerPage);
    requestUpdate();
  });
  buttonNavigator.onContinuous({Button::Up}, [this, total] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, total, itemsPerPage);
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
    activateSelected();
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
      activateSelected();
    }
  }
}

void CoverGridBrowserActivity::drawCell(const int flatIndex, const int x, const int y, const bool selected) const {
  const auto& entries = currentEntries();
  const bool haveData = flatIndex >= 0 && flatIndex < static_cast<int>(entries.size());

  const int innerX = x + CARD_PADDING;
  const int innerY = y + CARD_PADDING;
  // Same value LibraryGrouping::resolvePage() generated the cached thumbnail at (coverWidth is
  // computed once in computeGridGeometry() as cellWidth - CARD_PADDING * 2), so a cache hit needs
  // no rescale in drawBitmap1Bit below.
  const int innerW = coverWidth;

  // Blank this cell's full bounds first. drawCell can run without a preceding
  // clearScreen() (the selection-move fast path only redraws the two affected
  // cells), and the selection fill/ring below are drawn additively with no
  // complementary erase for the deselected state -- without this, a cell that
  // was selected (solid fill, or a highlight ring) stays marked that way after
  // losing the highlight, since only clearScreen() used to blank it first.
  renderer.fillRect(x, y, cellWidth, cellHeight, false);

  bool drewCover = false;
  std::string title;
  if (haveData) {
    const auto& entry = entries[flatIndex];
    title = entry.title;
    if (entry.hasCover) {
      HalFile file;
      if (Storage.openFileForRead("CGB", entry.coverThumbPath, file)) {
        Bitmap bitmap(file);
        const bool headerOk = bitmap.parseHeaders() == BmpReaderError::Ok;
        if (headerOk) {
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
  // cover gets no frame around it. A series entry with no cover falls back to the exact same card,
  // with the stack overlay layered on top of it identically to the real-cover case below.
  if (!drewCover) {
    if (selected) {
      renderer.fillRect(innerX, innerY, innerW, coverHeight);
    } else {
      renderer.drawRect(innerX, innerY, innerW, coverHeight);
    }
  }

  if (haveData && entries[flatIndex].isSeries) {
    drawStackOverlay(innerX, innerY, innerW, coverHeight);
  }

  if (SETTINGS.coversShowTitles && !title.empty()) {
    const std::string truncated = renderer.truncatedText(SMALL_FONT_ID, title.c_str(), innerW);
    renderer.drawText(SMALL_FONT_ID, innerX, innerY + coverHeight + CARD_PADDING, truncated.c_str());
  }

  if (selected) {
    renderer.drawRect(innerX - 2, innerY - 2, innerW + 4, coverHeight + 4, 2, true);
  }
}

void CoverGridBrowserActivity::drawStackOverlay(const int coverX, const int coverY, const int coverW,
                                                const int coverH) const {
  // Three lines ordered inner-to-outer by height (inner = full height, outer = shortest, nearest
  // the cover's right edge), all vertically centered on the cover's own midpoint -- evoking a
  // stack of books peeking out from behind the front cover. An inset overlay drawn on top of
  // whatever's already in the box (real cover art or the no-cover fallback card) -- not a reserved
  // layout strip, so cell/cache geometry never changes. Each line gets its own 1px white halo
  // drawn first (rather than relying on an already-blank background, the way the selection ring
  // below does) since real cover art under it isn't guaranteed to be light.
  constexpr int strokeWidth = 2;
  constexpr int lineGap = 3;              // horizontal gap between adjacent lines
  constexpr int outerInsetFromRight = 4;  // the outermost line's distance from the cover's right edge
  constexpr int topBottomMargin = 4;      // the inner (full-height) line's clearance from top/bottom
  constexpr int haloPad = 1;
  const int rightEdge = coverX + coverW;
  const int midY = coverY + coverH / 2;

  const int outerX = rightEdge - outerInsetFromRight - strokeWidth;
  const int middleX = outerX - lineGap - strokeWidth;
  const int innerX = middleX - lineGap - strokeWidth;

  const int innerHeight = coverH - topBottomMargin * 2;
  const int middleHeight = coverH * 87 / 100;
  const int outerHeight = coverH * 75 / 100;

  const auto drawLine = [this](const int x, const int centerY, const int height) {
    const int y = centerY - height / 2;
    renderer.fillRect(x - haloPad, y - haloPad, strokeWidth + haloPad * 2, height + haloPad * 2, false);
    renderer.fillRect(x, y, strokeWidth, height, true);
  };
  drawLine(innerX, midY, innerHeight);
  drawLine(middleX, midY, middleHeight);
  drawLine(outerX, midY, outerHeight);
}

void CoverGridBrowserActivity::cellOrigin(const int flatIndex, const int pageStart, int& outX, int& outY) const {
  const int localIdx = flatIndex - pageStart;
  outX = gridLeft + (localIdx % cols) * cellWidth;
  outY = gridTop + (localIdx / cols) * cellHeight;
}

void CoverGridBrowserActivity::computeHeaderText(const int flatIndex, std::string& outTitle,
                                                 std::string& outSubtitle) const {
  outTitle.clear();
  outSubtitle.clear();
  const auto& entries = currentEntries();
  if (flatIndex < 0 || flatIndex >= static_cast<int>(entries.size())) {
    return;
  }
  const auto& entry = entries[flatIndex];
  outTitle = entry.title;

  if (entry.isSeries) {
    // No single book's reading progress applies to a collapsed multi-book entry.
    return;
  }

  if (FsHelpers::hasEpubExtension(entry.path)) {
    const Epub epub(entry.path, "/.crosspoint");
    int spineIndex = 0;
    int pageNumber = 0;
    int pageCount = 0;
    if (EpubReaderUtils::loadProgress(epub.getCachePath(), spineIndex, pageNumber, pageCount) && pageCount > 0) {
      outSubtitle = std::to_string(pageNumber + 1) + "/" + std::to_string(pageCount);
    }
  }
}

void CoverGridBrowserActivity::render(RenderLock&&) {
  const auto& entries = currentEntries();
  // Captured before ensurePageLoaded() runs (which updates loadedPageStart on a
  // page transition): true only when the page the new selection lands on is
  // the same one already composed in the framebuffer.
  const int pageStart = entries.empty() ? -1 : (selectedIndex / itemsPerPage) * itemsPerPage;
  const bool sameComposedPage = hasComposedPage && !entries.empty() && pageStart == loadedPageStart;

  // Resolve the current page's metadata/covers BEFORE drawing anything, so the
  // grid is composed once and shown once -- no placeholder pass while
  // ensurePageLoaded runs behind it. A no-op when the page is already resolved.
  ensurePageLoaded();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  // Fast path: selection moved within the page already on screen.
  // EINK_DISPLAY_SINGLE_BUFFER_MODE means the framebuffer already holds the
  // rest of the page, so only the cell that lost the highlight and the one
  // that gained it need redrawing (each touches SD only if it has a cover --
  // at most 2 files, not the whole page) plus the header, then one refresh.
  // No clearScreen, no full grid redraw.
  if (sameComposedPage && lastRenderedIndex != selectedIndex) {
    if (lastRenderedIndex >= pageStart && lastRenderedIndex < pageStart + itemsPerPage &&
        lastRenderedIndex < static_cast<int>(entries.size())) {
      int oldX, oldY;
      cellOrigin(lastRenderedIndex, pageStart, oldX, oldY);
      drawCell(lastRenderedIndex, oldX, oldY, false);
    }
    int newX, newY;
    cellOrigin(selectedIndex, pageStart, newX, newY);
    drawCell(selectedIndex, newX, newY, true);

    std::string headerTitle;
    std::string subtitle;
    computeHeaderText(selectedIndex, headerTitle, subtitle);
    // Not every theme's drawHeader clears its own rect first (LyraTheme does;
    // BaseTheme/RoundedRaffTheme draw straight over whatever's already there,
    // relying on the caller's clearScreen()) -- same gap as drawCell had, so
    // clear it ourselves rather than assume the theme will.
    renderer.fillRect(0, metrics.topPadding, pageWidth, metrics.headerHeight, false);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   headerTitle.empty() ? nullptr : headerTitle.c_str(), subtitle.empty() ? nullptr : subtitle.c_str());

    renderer.displayBuffer();
    lastRenderedIndex = selectedIndex;
    return;
  }

  renderer.clearScreen();

  std::string headerTitle;
  std::string subtitle;
  if (entries.empty()) {
    headerTitle = source == Source::RecentBooks ? tr(STR_NO_RECENT_BOOKS) : tr(STR_NO_BOOKS_FOUND);
  } else {
    computeHeaderText(selectedIndex, headerTitle, subtitle);
  }

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 headerTitle.empty() ? nullptr : headerTitle.c_str(), subtitle.empty() ? nullptr : subtitle.c_str());

  if (!entries.empty()) {
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) {
        const int flatIndex = pageStart + r * cols + c;
        if (flatIndex >= static_cast<int>(entries.size())) {
          continue;
        }
        drawCell(flatIndex, gridLeft + c * cellWidth, gridTop + r * cellHeight, flatIndex == selectedIndex);
      }
    }
  }

  // Back exits a series page (still on this same grid, so the hint reads "Back"); at the top
  // level it always returns Home (no directory nesting to go up), matching FileBrowserActivity's
  // root-level Back. Front Left/Right now move within the row (side Up/Down move by row), so the
  // hint text is "Left"/"Right", not the "Up"/"Down" stock lists use for the same physical
  // buttons.
  const auto labels = mappedInput.mapLabels(seriesTopIndex >= 0 ? tr(STR_BACK) : tr(STR_HOME), tr(STR_OPEN),
                                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (!entries.empty()) {
    const int totalPages = (static_cast<int>(entries.size()) + itemsPerPage - 1) / itemsPerPage;
    UITheme::drawPageIndicator(renderer, metrics.buttonHintsHeight, pageStart / itemsPerPage + 1, totalPages);
  }

  renderer.displayBuffer();

  hasComposedPage = true;
  lastRenderedIndex = selectedIndex;
}
