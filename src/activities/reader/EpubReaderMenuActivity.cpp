#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/settings2.h"
#include "fontIds.h"

namespace {
// Both BookIcon and BookmarkIcon (CrossFade's own pre-existing icon assets) are 32x32; there's no
// 24x24 bookmark variant, so both tabs are drawn at the size that's actually shipped for both.
constexpr int tabIconSize = 32;
constexpr int selectedTabBoxWidth = 50;
constexpr int selectedTabBoxHeight = 40;
constexpr int selectedTabBoxRadius = 2;

// A focused tab needs its icon drawn in white on a filled black box; GfxRenderer::drawIcon()
// always plots in black, so this reimplements it with a foreground polarity flag. Bit convention
// and the (size-1-row, col) pixel transform are copied verbatim from GfxRenderer::drawIcon()
// (1bpp MSB-first, bit==0 = ink; the transform reproduces the Portrait orientation the icon
// assets were authored for) -- NOT from CrossInk's own icon blit, which uses a different asset
// orientation. Only the black-on-white path (state=true) is exercised by drawIcon() itself; this
// is the only place CrossFade draws one of these icons inverted.
void drawTabIcon(const GfxRenderer& renderer, const uint8_t bitmap[], const int x, const int y, const int size,
                 const bool foregroundBlack) {
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + (size - 1 - row), y + col, foregroundBlack);
      }
    }
  }
}
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks, const bool isFinished,
                                               const bool statsEnabled)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, isFinished, statsEnabled)),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {}

EpubReaderMenuActivity::TabMenuItems EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes, bool hasBookmarks,
                                                                            bool isFinished, bool statsEnabled) {
  TabMenuItems items;
  auto& mainItems = items[MAIN_TAB_INDEX];
  auto& bookmarkItems = items[BOOKMARKS_TAB_INDEX];
  auto& textSettingsItems = items[TEXT_SETTINGS_TAB_INDEX];
  mainItems.reserve(12);
  bookmarkItems.reserve(2);
  mainItems.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    mainItems.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  mainItems.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  mainItems.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  mainItems.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  mainItems.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  mainItems.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  mainItems.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  mainItems.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  mainItems.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  mainItems.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  mainItems.push_back(
      {MenuAction::TOGGLE_FINISHED, isFinished ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  // Hidden entirely when SETTINGS.shouldTrackReadingStats() is off -- nothing meaningful to show,
  // and showing the entry would just invite tapping into an always-empty screen.
  if (statsEnabled) {
    mainItems.push_back({MenuAction::READING_STATS, StrId::STR_READING_STATS});
  }

  if (hasBookmarks) {
    bookmarkItems.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  bookmarkItems.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});

  textSettingsItems.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  return items;
}

const std::vector<EpubReaderMenuActivity::MenuItem>& EpubReaderMenuActivity::activeMenuItems() const {
  return menuItems[activeTabIndex()];
}

void EpubReaderMenuActivity::focusTabRow() { selectedIndex = 0; }

void EpubReaderMenuActivity::cycleActiveTab() {
  const auto nextTabIndex = ButtonNavigator::nextIndex(static_cast<int>(activeTabIndex()), MENU_TAB_COUNT);
  activeTab = static_cast<MenuTab>(nextTabIndex);
  focusTabRow();
  requestUpdate();
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

Rect EpubReaderMenuActivity::tabSlotRect(const Rect barRect, const size_t index) const {
  const int slotX = barRect.x + static_cast<int>((index * barRect.width) / MENU_TAB_COUNT);
  const int nextSlotX = barRect.x + static_cast<int>(((index + 1) * barRect.width) / MENU_TAB_COUNT);
  return Rect{slotX, barRect.y, nextSlotX - slotX, barRect.height};
}

void EpubReaderMenuActivity::drawIconTabBar(const Rect rect) const {
  renderer.drawLine(rect.x, rect.y, rect.x + rect.width - 1, rect.y, true);
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);

  for (size_t i = 0; i < MENU_TAB_COUNT; i++) {
    const Rect slot = tabSlotRect(rect, i);
    const int centerX = slot.x + slot.width / 2;
    const bool selected = i == activeTabIndex();
    const bool tabFocused = selected && selectedIndex == 0;
    const int boxX = centerX - selectedTabBoxWidth / 2;
    const int boxY = slot.y + (slot.height - selectedTabBoxHeight) / 2;
    const int iconX = centerX - tabIconSize / 2;
    const int iconY = slot.y + (slot.height - tabIconSize) / 2;

    if (tabFocused) {
      renderer.fillRoundedRect(boxX, boxY, selectedTabBoxWidth, selectedTabBoxHeight, selectedTabBoxRadius,
                               Color::Black);
    } else if (selected) {
      renderer.drawRoundedRect(boxX, boxY, selectedTabBoxWidth, selectedTabBoxHeight, 1, selectedTabBoxRadius, true);
    }

    const uint8_t* icon =
        (i == MAIN_TAB_INDEX) ? BookIcon : (i == BOOKMARKS_TAB_INDEX) ? BookmarkIcon : Settings2Icon;
    if (tabFocused) {
      drawTabIcon(renderer, icon, iconX, iconY, tabIconSize, /*foregroundBlack=*/false);
    } else {
      renderer.drawIcon(icon, iconX, iconY, tabIconSize);
    }
  }
}

void EpubReaderMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The popup acts on button press; if that input closed it, the trailing
    // release must be swallowed below (Back would close the menu, Confirm
    // would re-activate the selected item).
    popupClosing = !optionPopup.isActive();
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;  // closing press still held
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;  // swallow the release that closed the popup
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selectedIndex > 0) {
      focusTabRow();
      requestUpdate();
      return;
    }
    closeCancelled();
    return;
  }

  auto activateSelected = [this] {
    const auto& items = activeMenuItems();
    const auto selectedAction = items[selectedIndex - 1].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       pendingOrientation, [this](int idx) {
                         pendingOrientation = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
      optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                       static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                         selectedPageTurnOption = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
    finish();
  };

  // No touch anywhere in this activity -- this is button-only hardware. Navigation mirrors
  // SettingsActivity's existing tab+list scheme exactly: index 0 = tab row (Confirm cycles the
  // active tab forward), 1..N = list item N-1; quick Next/Previous presses move through the list,
  // holding Next/Previous jumps tabs directly, same as SettingsActivity's category switch.
  bool hasChangedTab = false;

  buttonNavigator.onNextRelease([this] {
    const int itemCount = static_cast<int>(activeMenuItems().size());
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount + 1);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    const int itemCount = static_cast<int>(activeMenuItems().size());
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount + 1);
    requestUpdate();
  });

  // Only activeTab changes here -- selectedIndex is deliberately left alone so the check below
  // sees its pre-jump value, exactly like SettingsActivity's hasChangedCategory/
  // selectedSettingIndex pair.
  buttonNavigator.onNextContinuous([this, &hasChangedTab] {
    hasChangedTab = true;
    activeTab = static_cast<MenuTab>(ButtonNavigator::nextIndex(static_cast<int>(activeTabIndex()), MENU_TAB_COUNT));
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, &hasChangedTab] {
    hasChangedTab = true;
    activeTab =
        static_cast<MenuTab>(ButtonNavigator::previousIndex(static_cast<int>(activeTabIndex()), MENU_TAB_COUNT));
    requestUpdate();
  });

  if (hasChangedTab) {
    selectedIndex = (selectedIndex == 0) ? 0 : 1;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex == 0) {
      cycleActiveTab();
    } else {
      activateSelected();
    }
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());

  // Progress summary
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  GUI.drawSubHeader(
      renderer,
      Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width, metrics.tabBarHeight},
      progressLine.c_str());

  const Rect tabBarRect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight,
                        screen.width, metrics.tabBarHeight};
  drawIconTabBar(tabBarRect);

  const int contentTop = tabBarRect.y + tabBarRect.height + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const auto& items = activeMenuItems();

  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, items.size(), selectedIndex - 1,
      [&items](int index) { return I18N.get(items[index].labelId); }, nullptr, nullptr,
      [this, &items](int index) {
        const auto value = items[index].action;
        if (value == MenuAction::ROTATE_SCREEN) {
          // Render current orientation value on the right edge of the content area.
          return I18N.get(orientationLabels[pendingOrientation]);
        } else if (value == MenuAction::AUTO_PAGE_TURN) {
          // Render current page turn value on the right edge of the content area.
          return pageTurnLabels[selectedPageTurnOption];
        } else {
          return "";
        }
      },
      true);

  // Footer / Hints
  const auto confirmLabel = selectedIndex == 0 ? tr(STR_NEXT_FIELD) : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
