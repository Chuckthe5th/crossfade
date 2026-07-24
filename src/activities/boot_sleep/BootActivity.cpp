#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"
#include "util/LibraryIndexBuilder.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();

  // Splash is already on screen before this runs, so it never delays what the user sees first.
  // BootActivity is only entered for a genuine cold boot with the splash shown (see main.cpp's
  // setup()) -- the fast Silent/QuickResume paths skip it entirely and are unaffected, matching
  // "no index work on wake" from the sleep/wake design. A no-op unless SETTINGS.groupBySeries is
  // on; logs only, never blocks -- see checkLibraryIndexStaleness().
  checkLibraryIndexStaleness();
}
