#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"

class ButtonRemapActivity final : public Activity {
 public:
  // forReader: targets SETTINGS.readerFrontButton* instead of the system frontButton* fields --
  // everything else (the press-to-assign wizard, duplicate checking, reset/cancel) is identical,
  // only which four settings fields get read from and written to differs.
  explicit ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool forReader = false)
      : Activity("ButtonRemap", renderer, mappedInput), forReader(forReader) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  const bool forReader;

  // Rendering task state.

  // Index of the logical role currently awaiting input.
  uint8_t currentStep = 0;
  // Temporary mapping from logical role -> hardware button index.
  uint8_t tempMapping[4] = {0xFF, 0xFF, 0xFF, 0xFF};
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
