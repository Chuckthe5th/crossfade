#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/LongPressAction.h"

class RecentBooksActivity final : public Activity {
 private:
  ButtonNavigator buttonNavigator;
  LongPressAction longPressAction;

  size_t selectorIndex = 0;

  // Recent tab state
  std::vector<RecentBook> recentBooks;

  // Data loading
  void loadRecentBooks();

  // Opens the per-book context menu (Remove from Recents, Clear Cache, Delete) for the given book.
  void openContextMenu(const std::string& path, const std::string& title);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
