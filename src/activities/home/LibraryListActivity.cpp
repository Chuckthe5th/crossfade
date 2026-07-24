#include "LibraryListActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "../reader/EpubReaderUtils.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookMetadataResolver.h"

namespace {
// Same cap and reasoning as CoverGridBrowserActivity::MAX_GRID_BOOKS: bounds the recursive SD-card
// walk's transient RAM (~80 bytes/path worst case), held only while this activity is on screen.
constexpr size_t MAX_LIST_BOOKS = 2000;
}  // namespace

void LibraryListActivity::loadBooks() {
  books = LibraryScanner::scanAllBooks("/", MAX_LIST_BOOKS);
  // Same filename sort CoverGridBrowserActivity's Covers grid applies to its Library source -- both
  // are views over the same scanAllBooks() result, so they present the same order.
  LibraryScanner::sortByFilename(books);
}

void LibraryListActivity::ensurePageLoaded(const int itemsPerPage) {
  if (books.empty() || itemsPerPage <= 0) {
    return;
  }
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  if (pageStart == loadedPageStart) {
    return;
  }

  const int pageEnd = std::min(static_cast<int>(books.size()), pageStart + itemsPerPage);
  pageRows.assign(pageEnd - pageStart, RowCache{});
  for (int i = pageStart; i < pageEnd; i++) {
    const BookMetadataResolver::Result result = BookMetadataResolver::resolve(books[i].path);
    pageRows[i - pageStart] = RowCache{result.title, result.author};
  }
  loadedPageStart = pageStart;
}

std::string LibraryListActivity::rowTitle(const int index, const int itemsPerPage) const {
  const int pageStart = (index / itemsPerPage) * itemsPerPage;
  const int rowIdx = index - pageStart;
  if (loadedPageStart == pageStart && rowIdx >= 0 && rowIdx < static_cast<int>(pageRows.size())) {
    return pageRows[rowIdx].title;
  }
  return "";
}

std::string LibraryListActivity::rowAuthor(const int index, const int itemsPerPage) const {
  const int pageStart = (index / itemsPerPage) * itemsPerPage;
  const int rowIdx = index - pageStart;
  if (loadedPageStart == pageStart && rowIdx >= 0 && rowIdx < static_cast<int>(pageRows.size())) {
    return pageRows[rowIdx].author;
  }
  return "";
}

void LibraryListActivity::computeHeaderText(const int index, const int itemsPerPage, std::string& outTitle,
                                            std::string& outSubtitle) const {
  outTitle = rowTitle(index, itemsPerPage);
  outSubtitle.clear();

  const std::string& path = books[index].path;
  if (FsHelpers::hasEpubExtension(path)) {
    const Epub epub(path, "/.crosspoint");
    int spineIndex = 0;
    int pageNumber = 0;
    int pageCount = 0;
    if (EpubReaderUtils::loadProgress(epub.getCachePath(), spineIndex, pageNumber, pageCount) && pageCount > 0) {
      outSubtitle = std::to_string(pageNumber + 1) + "/" + std::to_string(pageCount);
    }
  }
}

void LibraryListActivity::onEnter() {
  Activity::onEnter();

  loadBooks();

  // Lands on the last-read book, same as CoverGridBrowserActivity's Library source -- both are
  // whole-library metadata views with no folder concept of their own to return to.
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
  pageRows.clear();
  requestUpdate();
}

void LibraryListActivity::onExit() {
  Activity::onExit();
  books.clear();
  pageRows.clear();
}

void LibraryListActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!books.empty() && selectedIndex < static_cast<int>(books.size())) {
      activityManager.goToReader(books[selectedIndex].path);
    }
    return;
  }

  int touchSel = selectedIndex;
  const auto listTouch = handleListTouch(touchSel, static_cast<int>(books.size()), contentTop, contentHeight, true);
  if (listTouch != ListTouchResult::None) {
    selectedIndex = touchSel;
    if (listTouch == ListTouchResult::Activated) {
      activityManager.goToReader(books[selectedIndex].path);
    } else {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int listSize = static_cast<int>(books.size());
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, listSize, pageItems);
    requestUpdate();
  });
}

void LibraryListActivity::render(RenderLock&&) {
  const int itemsPerPage = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  // Resolve the current page's title/author BEFORE drawing anything, same as
  // CoverGridBrowserActivity::render() -- the page is composed once and shown once, one
  // displayBuffer() call, no placeholder pass while metadata resolves behind it.
  ensurePageLoaded(itemsPerPage);

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string headerTitle;
  std::string subtitle;
  if (books.empty()) {
    headerTitle = tr(STR_NO_BOOKS_FOUND);
  } else {
    computeHeaderText(selectedIndex, itemsPerPage, headerTitle, subtitle);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 headerTitle.empty() ? nullptr : headerTitle.c_str(), subtitle.empty() ? nullptr : subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (!books.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(books.size()), selectedIndex,
        [this, itemsPerPage](int index) { return rowTitle(index, itemsPerPage); },
        [this, itemsPerPage](int index) { return rowAuthor(index, itemsPerPage); },
        [this](int index) { return UITheme::getFileIcon(books[index].path); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
