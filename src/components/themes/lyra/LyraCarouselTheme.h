#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Lyra Carousel theme metrics (zero runtime cost)
namespace LyraCarouselMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  v.listRowHeight = 35;
  v.menuRowHeight = 64;
  v.menuSpacing = 8;
  v.homeTopPadding = 28;
  v.homeCoverHeight = 600;
  v.homeCoverTileHeight = 660;
  v.homeRecentBooksCount = 3;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace LyraCarouselMetrics

// Functional port of CrossInk's Lyra Carousel home theme (https://github.com/uxjulia/CrossInk,
// also a CrossPoint fork -- see NOTICE): a large center cover flanked by smaller side covers, dot
// pagination, and a bottom icon-only button menu. Two things CrossInk's version has that this
// doesn't (yet, by request -- functional now, faithful later):
//  - True perspective-warped side covers. CrossInk added a drawPerspectiveBitmap primitive to its
//    GfxRenderer for this; this fork doesn't have it, and adding a new low-level framebuffer
//    primitive without hardware to verify it against felt like the wrong tradeoff. Side covers
//    here are plain scaled rectangles instead -- still reads as a carousel, just not skewed.
//  - The disk-backed frame cache that pre-renders adjacent carousel frames for instant scrolling
//    (CrossInk's HomeActivity.cpp: gCarouselCache, sliding-window pre-render, setPreRenderIndex).
//    This theme redraws normally on selection change, like every other CrossFade theme does.
// See the .cpp for the smaller adaptations (icon/API name differences, dropped
// BookReadingStats-only "time read" text -- this fork has no reading-stats subsystem).
class LyraCarouselTheme : public LyraTheme {
 public:
  static constexpr int kCenterCoverW = 340;
  static constexpr int kCenterCoverH = LyraCarouselMetrics::values.homeCoverHeight - 60;  // 540
  static constexpr int kCenterCoverVisualInset = 10;
  static constexpr int kBaseDisplayCenterW = (kCenterCoverW * 86) / 100;
  static constexpr int kBaseDisplayCenterH = (kCenterCoverH * 86) / 100;
  static constexpr int kDisplayCenterW =
      ((kBaseDisplayCenterW + 24) < kCenterCoverW) ? (kBaseDisplayCenterW + 24) : kCenterCoverW;
  static constexpr int kDisplayCenterH =
      ((kBaseDisplayCenterH + 24) < kCenterCoverH) ? (kBaseDisplayCenterH + 24) : kCenterCoverH;
  static constexpr int kSideCoverW = 200;
  static constexpr int kSideCoverH = LyraCarouselMetrics::values.homeCoverHeight - 210;  // 390

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;

 private:
  // Remembers the last-centered book so the carousel doesn't reset to book 0 while the icon-menu
  // row below it is focused (selectorIndex >= recentBooks.size()). mutable: drawRecentBookCover is
  // const, matching every other theme's render methods.
  mutable int lastCenterIdx_ = -1;
};
