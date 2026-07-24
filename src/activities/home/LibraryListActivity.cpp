#include "LibraryListActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "../reader/EpubReaderUtils.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

std::vector<LibraryGrouping::Entry>& LibraryListActivity::currentEntries() {
  if (seriesTopIndex >= 0 && seriesTopIndex < static_cast<int>(topLevelEntries.size())) {
    return topLevelEntries[seriesTopIndex].members;
  }
  return topLevelEntries;
}

const std::vector<LibraryGrouping::Entry>& LibraryListActivity::currentEntries() const {
  if (seriesTopIndex >= 0 && seriesTopIndex < static_cast<int>(topLevelEntries.size())) {
    return topLevelEntries[seriesTopIndex].members;
  }
  return topLevelEntries;
}

void LibraryListActivity::loadBooks() { topLevelEntries = LibraryGrouping::loadLibraryEntries(SETTINGS.groupBySeries); }

void LibraryListActivity::ensurePageLoaded(const int itemsPerPage) {
  auto& entries = currentEntries();
  if (entries.empty() || itemsPerPage <= 0) {
    return;
  }
  const int pageStart = (selectedIndex / itemsPerPage) * itemsPerPage;
  if (pageStart == loadedPageStart) {
    return;
  }

  // No cover box, no loading popup: unlike the grid, this never touches SD for anything but the
  // (cheap, metadata-only) text resolve, and LibraryGrouping::resolvePage() is a no-op per entry
  // that's already resolved -- always true in grouped mode, where the whole page arrives
  // pre-resolved from the index.
  const int pageEnd = std::min(static_cast<int>(entries.size()), pageStart + itemsPerPage);
  LibraryGrouping::resolvePage(entries, pageStart, pageEnd);
  loadedPageStart = pageStart;
}

std::string LibraryListActivity::rowTitle(const int index) const {
  const auto& entries = currentEntries();
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return "";
  }
  const auto& entry = entries[index];
  // Text is the only row element every theme actually renders (see the icon lambda below --
  // RoundedRaffTheme discards rowIcon entirely and BaseTheme never draws it, so a series row
  // signalled only by icon is invisible on half the themes). A title suffix works identically
  // everywhere, so it's the row's sole "this is a series" signal.
  return entry.isSeries ? entry.title + tr(STR_SERIES_SUFFIX) : entry.title;
}

std::string LibraryListActivity::rowAuthor(const int index) const {
  const auto& entries = currentEntries();
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return "";
  }
  return entries[index].author;
}

void LibraryListActivity::computeHeaderText(const int index, std::string& outTitle, std::string& outSubtitle) const {
  outTitle.clear();
  outSubtitle.clear();
  const auto& entries = currentEntries();
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }
  const auto& entry = entries[index];
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

void LibraryListActivity::enterSeries(const int topLevelIndex) {
  savedTopLevelSelectedIndex = topLevelIndex;
  seriesTopIndex = topLevelIndex;
  selectedIndex = 0;
  loadedPageStart = -1;
  requestUpdate();
}

void LibraryListActivity::exitSeries() {
  seriesTopIndex = -1;
  selectedIndex = savedTopLevelSelectedIndex;
  loadedPageStart = -1;
  requestUpdate();
}

void LibraryListActivity::activateSelected() {
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

void LibraryListActivity::selectLastRead() {
  selectedIndex = 0;
  seriesTopIndex = -1;
  if (topLevelEntries.empty()) {
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
        seriesTopIndex = i;
        savedTopLevelSelectedIndex = i;
        selectedIndex = m;
        return;
      }
    }
  }
}

void LibraryListActivity::onEnter() {
  Activity::onEnter();

  loadBooks();
  // Lands on the last-read book, same as CoverGridBrowserActivity -- both are whole-library
  // metadata views with no folder concept of their own to return to. Drills directly into a
  // series if the last-read book is one of its members (see selectLastRead()).
  selectLastRead();

  loadedPageStart = -1;
  requestUpdate();
}

void LibraryListActivity::onExit() {
  Activity::onExit();
  topLevelEntries.clear();
}

void LibraryListActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  int touchSel = selectedIndex;
  const auto listTouch = handleListTouch(touchSel, currentCount(), contentTop, contentHeight, true);
  if (listTouch != ListTouchResult::None) {
    selectedIndex = touchSel;
    if (listTouch == ListTouchResult::Activated) {
      activateSelected();
    } else {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (seriesTopIndex >= 0) {
      exitSeries();
    } else {
      onGoHome();
    }
    return;
  }

  const int listSize = currentCount();
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

  const auto& entries = currentEntries();

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string headerTitle;
  std::string subtitle;
  if (entries.empty()) {
    headerTitle = tr(STR_NO_BOOKS_FOUND);
  } else {
    computeHeaderText(selectedIndex, headerTitle, subtitle);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 headerTitle.empty() ? nullptr : headerTitle.c_str(), subtitle.empty() ? nullptr : subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (!entries.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(entries.size()), selectedIndex,
        [this](int index) { return rowTitle(index); }, [this](int index) { return rowAuthor(index); },
        [this](int index) {
          const auto& entry = currentEntries()[index];
          // Reuses the codebase's existing "this row navigates into a container, not a leaf item"
          // signal -- the Folder icon FileBrowserActivity shows for directories -- rather than
          // inventing a second visual language (chevron, count, prefix glyph) for the same idea.
          return entry.isSeries ? UIIcon::Folder : UITheme::getFileIcon(entry.path);
        });
  }

  const auto labels = mappedInput.mapLabels(seriesTopIndex >= 0 ? tr(STR_BACK) : tr(STR_HOME), tr(STR_OPEN),
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
