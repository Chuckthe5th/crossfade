#pragma once
#include "MappedInputManager.h"

// Fire-once "hold to trigger a secondary action" gesture, for the common case where a button's
// short-press already does something else (open/select) and a hold should trigger a different
// action instead of repeating the short-press one (that's ButtonNavigator::onContinuous's job).
// Firing is level-triggered -- it happens the instant the hold crosses the threshold, not on
// release -- then the button's release is swallowed so it can't ALSO fall through to the
// short-press handler once the caller un-arms by calling update() again.
//
// Extracted from the identical hand-rolled pattern RecentBooksActivity and FileBrowserActivity
// each had inline (see RecentBooksActivity's git history) -- now shared so a third, fourth, etc.
// caller can't independently drift on the swallow-guard step.
class LongPressAction {
 public:
  explicit LongPressAction(const unsigned long thresholdMs = 1000) : thresholdMs(thresholdMs) {}

  // Call once per loop() tick, gated by whatever "is there a valid target" check the caller needs.
  // Returns true exactly once, the instant the hold crosses the threshold; every call after that
  // (while still held, and the one covering the eventual release) returns false.
  bool update(const MappedInputManager& input, const MappedInputManager::Button button) {
    if (fired) {
      if (!input.isPressed(button)) {
        fired = false;
      }
      return false;
    }
    if (input.isPressed(button) && input.getHeldTime() >= thresholdMs) {
      fired = true;
      return true;
    }
    return false;
  }

 private:
  const unsigned long thresholdMs;
  bool fired = false;
};
