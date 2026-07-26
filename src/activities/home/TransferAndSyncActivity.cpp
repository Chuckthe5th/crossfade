#include "TransferAndSyncActivity.h"

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"

namespace {
// Fixed order: index 0 is File Transfer, index 1 is OPDS Browser -- selectAction below matches it.
constexpr StrId kOptions[] = {StrId::STR_FILE_TRANSFER, StrId::STR_OPDS_BROWSER};
constexpr int kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
}  // namespace

TransferAndSyncActivity::TransferAndSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("TransferAndSync", renderer, mappedInput) {}

void TransferAndSyncActivity::onEnter() {
  Activity::onEnter();
  optionPopup.show(StrId::STR_TRANSFER_AND_SYNC, kOptions, kOptionCount, 0,
                   [this](const int index) { selectAction(index); });
  requestUpdate();
}

void TransferAndSyncActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): return to Home.
  onGoHome();
}

void TransferAndSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}

void TransferAndSyncActivity::selectAction(const int index) {
  if (index == 1) {
    activityManager.goToBrowser();
  } else {
    activityManager.goToFileTransfer();
  }
}
