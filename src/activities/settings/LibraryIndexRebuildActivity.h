#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/LibraryIndexBuilder.h"

// Manual escape hatch for the library index (see LibraryIndexBuilder / LibraryIndex): forces a
// full rebuild regardless of the fingerprint delta, for the case a file's (size, mtime)
// fingerprint didn't catch a real change -- e.g. a File Transfer/OPDS-written file replaced with
// one of identical size, since this firmware never sets real mtimes on files it writes itself
// (see HalFile::getModifyTime). Reached from Settings > System > Rebuild Library Index.
//
// The build is driven incrementally, one book per loop() tick (see LibraryIndexBuilder::step()),
// so it stays responsive to input the whole time it runs -- Back cancels mid-build, discarding
// the in-progress work without touching whatever index already exists on disk.
class LibraryIndexRebuildActivity final : public Activity {
 public:
  static const char* indexPath() { return "/.crosspoint/library_index.bin"; }

  explicit LibraryIndexRebuildActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibraryIndexRebuild", renderer, mappedInput), builder(indexPath()) {}

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

  void goBack() { finish(); }
  void beginBuild();
  void maybeRequestProgressUpdate();
};
