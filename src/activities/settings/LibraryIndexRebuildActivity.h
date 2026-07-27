#pragma once

#include <cstdint>
#include <functional>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/LibraryIndex.h"
#include "util/LibraryIndexBuilder.h"

// Escape hatch for the library index (see LibraryIndexBuilder / LibraryIndex): forces a full
// rebuild regardless of the fingerprint delta, for the case a file's (size, mtime) fingerprint
// didn't catch a real change -- e.g. a File Transfer/OPDS-written file replaced with one of
// identical size, since this firmware never sets real mtimes on files it writes itself (see
// HalFile::getModifyTime).
//
// Two entry points:
//  - Manual (default constructor): Settings > System > Rebuild Library Index. Shows a confirm
//    dialog first; Back/finish() returns to the caller via the activity stack.
//  - Auto (onDone constructor): used by HomeActivity::onFileBrowserOpen() when entering a grouped
//    Covers/Titles view with a stale index -- skips the confirm dialog, starts straight into
//    BUILDING, and calls `onDone` instead of finish() on any terminal outcome (success,
//    up-to-date, cancelled, or failed), since it isn't on the activity stack and needs to
//    continue to a specific next screen regardless of how the build ended. Covers/Titles fall
//    back to ungrouped rendering on their own if no valid index exists when they go to read it
//    (see LibraryGrouping::loadLibraryEntries) -- this activity doesn't need to signal success/
//    failure to its caller, only that it's done.
//
// The build is driven incrementally, one book per loop() tick (see LibraryIndexBuilder::step()),
// so it stays responsive to input the whole time it runs -- Back cancels mid-build, discarding
// the in-progress work without touching whatever index already exists on disk.
class LibraryIndexRebuildActivity final : public Activity {
 public:
  using DoneCallback = std::function<void()>;

  explicit LibraryIndexRebuildActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       DoneCallback onDone = nullptr)
      : Activity("LibraryIndexRebuild", renderer, mappedInput),
        builder(LibraryIndex::PATH),
        onDone(std::move(onDone)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode during the build.
  void render(RenderLock&&) override;

 private:
  enum State { WARNING, BUILDING, UP_TO_DATE, SUCCESS, CANCELLED, FAILED };
  State state = WARNING;

  LibraryIndexBuilder builder;
  OptionPopup confirmPopup;
  DoneCallback onDone;  // null = manual mode (Settings action); set = auto mode

  // Progress popup throttling: a redraw is only requested when BOTH at least
  // PROGRESS_MIN_INTERVAL_MS has elapsed AND progress has advanced by at least
  // PROGRESS_MIN_PERCENT_STEP since the last one -- requiring both (not either) is what bounds
  // the refresh count at min(build-duration-in-seconds, 100/step) rather than either alone, so a
  // large library never costs more than a couple dozen e-ink refreshes for the whole build. See
  // BaseTheme::fillPopupProgress, which refreshes unconditionally on every call.
  uint32_t lastPopupUpdateMs = 0;
  int lastPopupPercent = -1;
  bool popupShown = false;
  Rect popupRect{0, 0, 0, 0};
  // True once commit() has run this build -- distinguishes "cancel during the pending-book phase"
  // (discard in-progress work via builder.cancel()) from "cancel during cover backfill" (metadata
  // is already persisted; abandoning remaining covers is harmless, see loop()).
  bool metadataCommitted = false;

  bool isAutoMode() const { return static_cast<bool>(onDone); }
  // Manual mode: returns to the caller via the activity stack. Auto mode: continues to whatever
  // onDone() navigates to next.
  void finishOrContinue();
  void beginBuild();
  void maybeRequestProgressUpdate();
};
