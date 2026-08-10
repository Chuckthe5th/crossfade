#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right[, Up, Down].
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
// Duration to show temporary error text when reassigning a button.
constexpr unsigned long kErrorDisplayMs = 1500;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  for (uint8_t i = 0; i < roleCount(); i++) {
    tempMapping[i] = kUnassigned;
  }
  errorMessage.clear();
  errorUntil = 0;
  requestUpdate();
}

void ButtonRemapActivity::onExit() { Activity::onExit(); }

void ButtonRemapActivity::loop() {
  // Clear any temporary warning after its timeout.
  if (errorUntil > 0 && millis() > errorUntil) {
    errorMessage.clear();
    errorUntil = 0;
    requestUpdate();
    return;
  }

  if (forReader) {
    // Side buttons, reader table only (Up/Down aren't reassignable roles here, so they're free
    // to keep serving as a fixed escape hatch):
    // - Up: reset mapping to defaults and exit.
    // - Down: cancel without saving.
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      SETTINGS.readerFrontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      SETTINGS.readerFrontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      SETTINGS.readerFrontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
      SETTINGS.readerFrontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      SETTINGS.saveToFile();
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      finish();
      return;
    }
  } else {
    // Up/Down are themselves reassignable roles in this (menu-nav) mode, so they can't double as
    // a fixed escape hatch anymore. Power always bypasses remapping (see
    // MappedInputManager::mapButton), so it's the only button guaranteed available mid-wizard --
    // cancel only, no quick-reset shortcut (a short press can't trigger sleep; that needs a held
    // duration -- see main.cpp's power-button handling).
    if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
      finish();
      return;
    }
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a button press to assign to the current role -- front-only for the reader table,
    // front+side for the menu-nav one (which can assign Up/Down too).
    const int pressedButton = forReader ? mappedInput.getPressedFrontButton() : mappedInput.getPressedNavButton();
    if (pressedButton < 0) {
      return;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= roleCount()) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      SETTINGS.saveToFile();
      finish();
      return;
    }

    requestUpdate();
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < roleCount(); i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    return "-";
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 I18N.get(forReader ? StrId::STR_REMAP_READER_FRONT_BUTTONS : StrId::STR_REMAP_MENU_NAV));
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    tr(forReader ? STR_REMAP_PROMPT : STR_REMAP_PROMPT_NAV));

  int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  int contentHeight = pageHeight - topOffset - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const uint8_t roles = roleCount();
  GUI.drawList(
      renderer, Rect{0, topOffset, pageWidth, contentHeight}, roles, currentStep,
      [&](int index) { return getRoleName(static_cast<uint8_t>(index)); }, nullptr, nullptr,
      [&](int index) {
        uint8_t assignedButton = tempMapping[static_cast<uint8_t>(index)];
        return (assignedButton == kUnassigned) ? tr(STR_UNASSIGNED) : getHardwareName(assignedButton);
      },
      true);

  // Temporary warning banner for duplicates.
  if (!errorMessage.empty()) {
    GUI.drawHelpText(renderer,
                     Rect{0, pageHeight - metrics.buttonHintsHeight - metrics.contentSidePadding - 15, pageWidth, 20},
                     errorMessage.c_str());
  }

  // Escape-hatch hint(s) below the role list -- offset by the actual role count so it doesn't
  // overlap the last row(s), which differs between the 4-role reader table and the 6-role
  // menu-nav one.
  if (forReader) {
    GUI.drawHelpText(renderer,
                     Rect{0, topOffset + roles * metrics.listRowHeight + 4 * metrics.verticalSpacing, pageWidth, 20},
                     tr(STR_REMAP_RESET_HINT));
    GUI.drawHelpText(
        renderer,
        Rect{0, topOffset + roles * metrics.listRowHeight + 5 * metrics.verticalSpacing + 20, pageWidth, 20},
        tr(STR_REMAP_CANCEL_HINT));
  } else {
    GUI.drawHelpText(renderer,
                     Rect{0, topOffset + roles * metrics.listRowHeight + 4 * metrics.verticalSpacing, pageWidth, 20},
                     tr(STR_REMAP_CANCEL_HINT_POWER));
  }

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right. Up/Down have no
  // dedicated hint slot (no on-screen hint exists for side buttons anywhere in the app) -- their
  // live-assigned role is visible in the list above instead.
  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
  renderer.displayBuffer();
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  if (forReader) {
    SETTINGS.readerFrontButtonBack = tempMapping[0];
    SETTINGS.readerFrontButtonConfirm = tempMapping[1];
    SETTINGS.readerFrontButtonLeft = tempMapping[2];
    SETTINGS.readerFrontButtonRight = tempMapping[3];
  } else {
    SETTINGS.frontButtonBack = tempMapping[0];
    SETTINGS.frontButtonConfirm = tempMapping[1];
    SETTINGS.frontButtonLeft = tempMapping[2];
    SETTINGS.frontButtonRight = tempMapping[3];
    SETTINGS.frontButtonUp = tempMapping[4];
    SETTINGS.frontButtonDown = tempMapping[5];
  }
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < roleCount(); i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) const {
  switch (roleIndex) {
    case 0:
      return tr(STR_BACK);
    case 1:
      return tr(STR_CONFIRM);
    case 2:
      return tr(STR_DIR_LEFT);
    case 3:
      return tr(STR_DIR_RIGHT);
    case 4:
      return tr(STR_DIR_UP);
    case 5:
    default:
      return tr(STR_DIR_DOWN);
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return tr(STR_HW_BACK_LABEL);
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return tr(STR_HW_CONFIRM_LABEL);
    case CrossPointSettings::FRONT_HW_LEFT:
      return tr(STR_HW_LEFT_LABEL);
    case CrossPointSettings::FRONT_HW_RIGHT:
      return tr(STR_HW_RIGHT_LABEL);
    case CrossPointSettings::FRONT_HW_UP:
      return tr(STR_HW_UP_LABEL);
    case CrossPointSettings::FRONT_HW_DOWN:
      return tr(STR_HW_DOWN_LABEL);
    default:
      return "Unknown";
  }
}
