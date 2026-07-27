#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  // Cancel/Update choice for WAITING_CONFIRMATION -- the same OptionPopup every other
  // confirmation dialog uses (see ConfirmationActivity, LibraryIndexRebuildActivity's WARNING
  // state), so button placement and the physical-button mapping (Back always cancels/dismisses,
  // Confirm selects whichever option is currently highlighted, Up/Down/Left/Right toggle between
  // them) match the rest of the firmware instead of this activity's own bespoke touch-zone layout.
  OptionPopup confirmPopup;

  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
