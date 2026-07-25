#include "BookContextMenuActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include "FinishedBooksStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/BookCacheUtils.h"

BookContextMenuActivity::BookContextMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 std::string path, std::string title, const Available available)
    : Activity("BookContextMenu", renderer, mappedInput),
      path(path),  // not moved yet -- reused below, before the member init list moves title in
      title(std::move(title)),
      menuItems(buildMenuItems(available, FINISHED_BOOKS.isFinished(path))) {}

std::vector<BookContextMenuActivity::MenuItem> BookContextMenuActivity::buildMenuItems(const Available& available,
                                                                                       const bool isFinished) {
  std::vector<MenuItem> items;
  if (available.removeFromRecents) {
    items.push_back({Action::RemoveFromRecents, StrId::STR_REMOVE_FROM_RECENTS});
  }
  items.push_back({Action::ToggleFinished, isFinished ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  items.push_back({Action::ClearCache, StrId::STR_DELETE_CACHE});
  items.push_back({Action::Delete, StrId::STR_DELETE});
  return items;
}

void BookContextMenuActivity::onEnter() {
  Activity::onEnter();

  std::vector<const char*> labels;
  labels.reserve(menuItems.size());
  for (const auto& item : menuItems) {
    labels.push_back(I18N.get(item.labelId));
  }
  optionPopup.show(title.c_str(), labels.data(), static_cast<int>(labels.size()), 0,
                   [this](const int index) { selectAction(index); });
  requestUpdate();
}

void BookContextMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): nothing changed.
  finishWithResult(false);
}

void BookContextMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

void BookContextMenuActivity::selectAction(const int index) {
  if (index < 0 || index >= static_cast<int>(menuItems.size())) {
    finishWithResult(false);
    return;
  }

  switch (menuItems[index].action) {
    case Action::ClearCache:
      runClearCache();
      return;
    case Action::Delete:
      confirmThenRun(Action::Delete, std::string(tr(STR_DELETE)) + "?");
      return;
    case Action::RemoveFromRecents:
      confirmThenRun(Action::RemoveFromRecents, tr(STR_REMOVE_FROM_RECENTS));
      return;
    case Action::ToggleFinished:
      // No confirmation, matching ClearCache: reversible, low-risk, and doesn't change anything
      // the caller's list displays (no finished-state badge), so no reload is needed either.
      FINISHED_BOOKS.setFinished(path, !FINISHED_BOOKS.isFinished(path));
      finishWithResult(false);
      return;
  }
}

void BookContextMenuActivity::runClearCache() {
  // No confirmation, matching EpubReaderMenuActivity::MenuAction::DELETE_CACHE's existing
  // no-confirm precedent: low-risk (removes rebuildable cache files, not user content), and the
  // in-memory list entry's title/author/cover came from the library index / RecentBooksStore, not
  // book.bin, so nothing displayed needs to change.
  clearBookCache(path);
  finishWithResult(false);
}

void BookContextMenuActivity::confirmThenRun(const Action action, const std::string& confirmHeading) {
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, confirmHeading, title),
                         [this, action](const ActivityResult& res) {
                           if (res.isCancelled) {
                             finishWithResult(false);
                             return;
                           }
                           if (action == Action::Delete) {
                             clearBookCache(path);
                             Storage.remove(path.c_str());
                           } else if (action == Action::RemoveFromRecents) {
                             RECENT_BOOKS.removeByPath(path);
                           }
                           finishWithResult(true);
                         });
}

void BookContextMenuActivity::finishWithResult(const bool changed) {
  setResult(BookActionResult{changed});
  finish();
}
