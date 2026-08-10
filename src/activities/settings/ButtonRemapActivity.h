#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"

class ButtonRemapActivity final : public Activity {
 public:
  // forReader: targets SETTINGS.readerFrontButton* (4 roles: Back/Confirm/Left/Right) instead of
  // the system frontButton* fields (6 roles: those 4 plus Up/Down -- menu-nav remapping). The
  // press-to-assign wizard, duplicate checking, and labels are otherwise identical; only the role
  // count, which settings fields get read/written, and the escape-hatch input differ (see
  // roleCount() and loop()): the reader wizard keeps the original fixed Up=reset/Down=cancel side
  // -button shortcut since it never reassigns Up/Down; the 6-role wizard can't rely on Up/Down
  // being fixed anymore, so it uses Power=cancel instead, with no quick-reset shortcut.
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forReader = false)
      : Activity("ButtonRemap", renderer, mappedInput), forReader(forReader) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const bool forReader;
  // 4 roles (Back/Confirm/Left/Right) for the reader table, 6 (plus Up/Down) for the system one.
  uint8_t roleCount() const { return forReader ? 4 : 6; }

  // Rendering task state.

  // Index of the logical role currently awaiting input.
  uint8_t currentStep = 0;
  // Temporary mapping from logical role -> hardware button index. Sized for the larger (6-role)
  // case; the reader wizard only ever touches indices [0, roleCount()).
  uint8_t tempMapping[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  // Error banner timing (used when reassigning duplicate buttons).
  unsigned long errorUntil = 0;
  std::string errorMessage;

  // Commit temporary mapping to settings.
  void applyTempMapping();
  // Returns false if a hardware button is already assigned to a different role.
  bool validateUnassigned(uint8_t pressedButton);
  // Labels for UI display.
  const char* getRoleName(uint8_t roleIndex) const;
  const char* getHardwareName(uint8_t buttonIndex) const;
};
