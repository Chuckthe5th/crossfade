#include "BookStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "GlobalReadingStats.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Same compact "3h 20m" / "45m" / "<1m" style as LyraCarouselTheme's carousel label -- duplicated
// rather than shared: it's a stable ten-line formatter with two call sites, not worth a new shared
// header over.
void formatCompactDuration(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "<1m");
    return;
  }
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (hours == 0) {
    snprintf(buf, len, "%lum", static_cast<unsigned long>(minutes));
  } else {
    snprintf(buf, len, "%luh %lum", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  }
}
}  // namespace

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BookStatsActivity::onExit() { Activity::onExit(); }

void BookStatsActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void BookStatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS),
                 nullptr);

  const int rowX = 24;
  const int rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 12;
  int rowY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const auto drawRow = [&](const char* label, const std::string& value) {
    renderer.drawText(UI_10_FONT_ID, rowX, rowY, label, true, EpdFontFamily::BOLD);
    const int valueW = renderer.getTextWidth(UI_10_FONT_ID, value.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 24 - valueW, rowY, value.c_str(), true, EpdFontFamily::REGULAR);
    rowY += rowLineHeight;
  };

  if (stats.sessionCount == 0) {
    renderer.drawText(UI_10_FONT_ID, rowX, rowY, tr(STR_STATS_NO_DATA), true, EpdFontFamily::REGULAR);
  } else {
    char timeReadBuf[16];
    formatCompactDuration(stats.totalReadingSeconds, timeReadBuf, sizeof(timeReadBuf));
    drawRow(tr(STR_STATS_SESSIONS), std::to_string(stats.sessionCount));
    drawRow(tr(STR_STATS_TIME_READ), timeReadBuf);
    drawRow(tr(STR_STATS_PAGES_TURNED), std::to_string(stats.totalPagesTurned));
    if (stats.estimatedTimeLeftSeconds > 0) {
      char timeLeftBuf[16];
      formatCompactDuration(stats.estimatedTimeLeftSeconds, timeLeftBuf, sizeof(timeLeftBuf));
      drawRow(tr(STR_STATS_TIME_LEFT), timeLeftBuf);
    }
  }

  rowY += rowLineHeight / 2;
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  if (globalStats.totalSessions > 0) {
    char globalTimeBuf[16];
    formatCompactDuration(globalStats.totalReadingSeconds, globalTimeBuf, sizeof(globalTimeBuf));
    renderer.drawText(UI_10_FONT_ID, rowX, rowY, tr(STR_STATS_ALL_BOOKS), true, EpdFontFamily::BOLD);
    rowY += rowLineHeight;
    drawRow(tr(STR_STATS_SESSIONS), std::to_string(globalStats.totalSessions));
    drawRow(tr(STR_STATS_TIME_READ), globalTimeBuf);
    drawRow(tr(STR_STATS_PAGES_TURNED), std::to_string(globalStats.totalPagesTurned));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
