#include "LibraryIndexRebuildActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t PROGRESS_MIN_INTERVAL_MS = 1000;
constexpr int PROGRESS_MIN_PERCENT_STEP = 5;

std::string formatSeconds(const uint32_t ms) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1fs", ms / 1000.0f);
  return buf;
}
}  // namespace

void LibraryIndexRebuildActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  const char* options[] = {tr(STR_CANCEL), tr(STR_REBUILD_BUTTON)};
  confirmPopup.show(tr(STR_REBUILD_LIBRARY_INDEX), options, 2, 0, [this](const int idx) {
    if (idx == 1) {
      beginBuild();
    } else {
      goBack();
    }
  });
  requestUpdate();
}

void LibraryIndexRebuildActivity::onExit() { Activity::onExit(); }

void LibraryIndexRebuildActivity::beginBuild() {
  state = BUILDING;
  popupShown = false;
  lastPopupUpdateMs = 0;
  lastPopupPercent = -1;
  requestUpdateAndWait();  // Show "Building..." before the (fast, but not instant) delta scan below.

  builder.begin();
  if (builder.upToDate()) {
    state = UP_TO_DATE;
  }
  requestUpdate();
}

void LibraryIndexRebuildActivity::maybeRequestProgressUpdate() {
  const int total = builder.totalCount();
  const int percent = total > 0 ? (builder.resolvedCount() * 100 / total) : 100;
  const uint32_t nowMs = millis();
  const bool timeElapsed = (nowMs - lastPopupUpdateMs) >= PROGRESS_MIN_INTERVAL_MS;
  const bool percentAdvanced = (percent - lastPopupPercent) >= PROGRESS_MIN_PERCENT_STEP;
  if (lastPopupPercent < 0 || (timeElapsed && percentAdvanced)) {
    lastPopupUpdateMs = nowMs;
    lastPopupPercent = percent;
    requestUpdate();
  }
}

void LibraryIndexRebuildActivity::loop() {
  if (state == WARNING) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      goBack();
    }
    return;
  }

  if (state == BUILDING) {
    // isPressed(), not wasPressed(): builder.step() blocks for hundreds of ms per book, so
    // gpio.update() (the only thing that samples the raw button state) only runs about once per
    // step() -- a quick tap-and-release can land entirely inside that gap and never register as
    // an edge. A held press is still caught because isPressed() reflects whatever the debounced
    // state was at the last sample, not a one-shot event that resets before the next check.
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
      builder.cancel();
      state = CANCELLED;
      requestUpdate();
      return;
    }
    if (builder.hasWork()) {
      builder.step();
      maybeRequestProgressUpdate();
    } else {
      const bool ok = builder.commit();
      state = ok ? SUCCESS : FAILED;
      requestUpdate();
    }
    return;
  }

  // UP_TO_DATE, SUCCESS, CANCELLED, FAILED are all terminal: any input dismisses.
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    goBack();
  }
}

void LibraryIndexRebuildActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == BUILDING) {
    // Compose the header once; afterward only the popup's own progress bar refreshes (via
    // fillPopupProgress below), matching CoverGridBrowserActivity::ensurePageLoaded()'s
    // throttled-popup convention -- not a full-page redraw per progress step.
    if (!popupShown) {
      renderer.clearScreen();
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                     tr(STR_REBUILD_LIBRARY_INDEX));
      popupRect = GUI.drawPopup(renderer, tr(STR_BUILDING_LIBRARY_INDEX));
      popupShown = true;
      // Drawn once here, not per progress tick -- matches the header above, and the hint text
      // itself never changes. isPressed() (see loop()) needs a genuine hold, not a tap.
      const auto labels = mappedInput.mapLabels(tr(STR_HOLD_TO_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // This first render happens before builder.begin() has run (see beginBuild()), so
      // totalCount()/resolvedCount() aren't meaningful yet -- skip the progress bar rather than
      // show a misleading 100% for one frame. The next render (after begin() completes) draws it.
      return;
    }
    const int total = builder.totalCount();
    const int percent = total > 0 ? (builder.resolvedCount() * 100 / total) : 100;
    GUI.fillPopupProgress(renderer, popupRect, percent);
    return;
  }

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_REBUILD_LIBRARY_INDEX));

  if (state == WARNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_REBUILD_LIBRARY_INDEX_WARNING));

    if (confirmPopup.processRender(renderer, mappedInput)) return;

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_REBUILD_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UP_TO_DATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LIBRARY_INDEX_UP_TO_DATE), true,
                              EpdFontFamily::BOLD);
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_LIBRARY_INDEX_BUILT), true,
                              EpdFontFamily::BOLD);
    const std::string resultText = std::to_string(builder.totalEntries()) + " " + std::string(tr(STR_BOOKS_INDEXED)) +
                                   ", " + formatSeconds(builder.elapsedMs());
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, resultText.c_str());
  } else if (state == CANCELLED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LIBRARY_INDEX_CANCELLED));
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_LIBRARY_INDEX_FAILED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
