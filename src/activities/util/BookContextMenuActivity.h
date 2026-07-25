#pragma once
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Extensible per-book action menu, triggered by a long-press on the selected entry in
// CoverGridBrowserActivity, LibraryListActivity, and RecentBooksActivity. A thin Activity (like
// ConfirmationActivity) wrapping a single OptionPopup for the action list, chaining to a second
// ConfirmationActivity for actions that need one. Reports back via BookActionResult{changed}:
// true if the caller's list entry needs refreshing (Delete, RemoveFromRecents), false otherwise
// (cache cleared, or the menu was dismissed without acting).
class BookContextMenuActivity final : public Activity {
 public:
  enum class Action { Delete, ClearCache, RemoveFromRecents, ToggleFinished };

  // Which actions apply to this book beyond the always-available Delete/ClearCache.
  struct Available {
    // Only offered where the book is actually a Recent Books entry -- meaningless (and
    // RECENT_BOOKS.removeByPath() would just no-op) for a Browse Books whole-library listing.
    bool removeFromRecents = false;
  };

  BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, std::string title,
                          Available available);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    Action action;
    StrId labelId;
  };
  static std::vector<MenuItem> buildMenuItems(const Available& available, bool isFinished);

  void selectAction(int index);
  void runClearCache();
  void confirmThenRun(Action action, const std::string& confirmHeading);
  void finishWithResult(bool changed);

  const std::string path;
  const std::string title;
  const std::vector<MenuItem> menuItems;
  OptionPopup optionPopup;
};
