#pragma once
#include <Epub.h>
#include <I18n.h>

#include <array>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

struct Rect;

class EpubReaderMenuActivity final : public Activity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY,
    TOGGLE_FINISHED
  };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  bool isFinished);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  // Tab-bar layout, hit-testing, and the "Confirm cycles tabs / Down moves into the list" focus
  // model are adapted from CrossInk (https://github.com/uxjulia/CrossInk), also a CrossPoint fork
  // -- see NOTICE. The tab set, icons, and menu items themselves are CrossFade's own.
  enum class MenuTab : uint8_t { Main = 0, Bookmarks = 1 };
  static constexpr size_t MAIN_TAB_INDEX = 0;
  static constexpr size_t BOOKMARKS_TAB_INDEX = 1;
  static constexpr size_t MENU_TAB_COUNT = 2;
  using TabMenuItems = std::array<std::vector<MenuItem>, MENU_TAB_COUNT>;

  static TabMenuItems buildMenuItems(bool hasFootnotes, bool hasBookmarks, bool isFinished);
  [[nodiscard]] const std::vector<MenuItem>& activeMenuItems() const;
  [[nodiscard]] size_t activeTabIndex() const { return static_cast<size_t>(activeTab); }
  void cycleActiveTab();
  // -1 = focus on the tab row (Confirm cycles tabs); >= 0 = focus in the active tab's list.
  void focusTabRow();
  void closeCancelled();
  void drawIconTabBar(Rect rect) const;
  // Tab slot rect for tab `index`, matching drawIconTabBar's own slot math -- shared so touch
  // hit-testing never drifts out of sync with what's actually drawn.
  Rect tabSlotRect(Rect barRect, size_t index) const;

  // Fixed menu layout
  const TabMenuItems menuItems;

  int selectedIndex = -1;
  MenuTab activeTab = MenuTab::Main;

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  // True while the button press that closed the popup is still held; its release
  // must not fall through to the menu's own Back/Confirm handlers.
  bool popupClosing = false;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
