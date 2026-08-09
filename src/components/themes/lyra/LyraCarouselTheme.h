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
  // CrossInk's own literal values, unchanged since the initial port. Run through CrossInk's own
  // formula chain (kCenterCoverH = homeCoverHeight-60, kDisplayCenterH = min(86%+24, kCenterCoverH))
  // the center cover rect alone (y=101, h=488 on X4) bottoms out at y=589. That fits comfortably:
  // 792x528 and 800x480 (X3/X4's raw panel dims, landscape-native) are NOT the screen height --
  // GfxRenderer runs Home in Portrait orientation, where getScreenHeight() returns the OTHER raw
  // dimension (X3: 792px tall, X4: 800px tall; the smaller number, 528/480, is the logical width
  // instead). Against the real 800px-tall (X4) / 792px-tall (X3) portrait screen, y=589 lands with
  // ~200px to spare before the icon-menu row -- matching CrossInk's own reference photo. 600 is
  // correct as-is; do not re-solve it for a smaller value.
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
// also a CrossPoint fork -- see NOTICE): a large center cover flanked by smaller perspective
// -skewed side covers, a title, dot pagination, a progress bar with percentage, and a bottom
// icon-only button menu alongside the normal button-hints row. Differences from CrossInk's
// version:
//  - The disk-backed frame cache that pre-renders adjacent carousel frames for instant scrolling
//    (CrossInk's HomeActivity.cpp: gCarouselCache, sliding-window pre-render, setPreRenderIndex).
//    This theme redraws normally on selection change, like every other CrossFade theme does.
//  - No reading-stats "time read" label (this fork has no reading-stats subsystem to source it
//    from) -- CrossFade's own real per-book percentage is used for the progress bar instead of
//    approximating CrossInk's stats-driven one.
//  - Menu icons are CrossFade's own existing 32px icon set (FolderIcon/RecentIcon/Settings2Icon/
//    TransferIcon/PinIcon -- the same assets LyraTheme's list menu uses), not CrossInk's.
// The perspective skew (GfxRenderer::drawPerspectiveBitmap, ported from CrossInk's own
// GfxRenderer -- see its declaration), the two-tier center/side cover sizing below, and the
// bidirectional (both-axis) cover cropping in the .cpp are straight ports of CrossInk's own
// formulas, using CrossInk's own literal pixel constants (including homeCoverHeight -- see its
// comment above: it fits both X3 and X4's real portrait screen height with room to spare, so
// none of this needed re-solving for a smaller value).
class LyraCarouselTheme : public LyraTheme {
 public:
  // Two-tier max/display sizing, ported from CrossInk exactly: kCenterCoverW/H is the "cache
  // -quality" ceiling (width is a fixed constant -- CrossInk never scales it with height at all;
  // height is homeCoverHeight minus a fixed 60px), kDisplayCenterW/H is 86% of that plus a 24px
  // clamp, floored at the ceiling itself. Because width and height derive independently (86%+24
  // of DIFFERENT base numbers), the resulting box is not aspect-locked to the cover art's own
  // portrait shape -- CrossInk's drawCenterCover crops whichever axis (width or height) has
  // surplus to fill the box exactly rather than requiring the box to match the image, which is
  // why the cover-drawing code here crops both axes (cropX AND cropY), not just cropX.
  static constexpr int kCenterCoverW = 340;
  static constexpr int kCenterCoverH = LyraCarouselMetrics::values.homeCoverHeight - 60;
  static constexpr int kCenterCoverVisualInset = 10;
  static constexpr int kBaseDisplayCenterW = (kCenterCoverW * 86) / 100;
  static constexpr int kBaseDisplayCenterH = (kCenterCoverH * 86) / 100;
  static constexpr int kDisplayCenterW =
      ((kBaseDisplayCenterW + 24) < kCenterCoverW) ? (kBaseDisplayCenterW + 24) : kCenterCoverW;
  static constexpr int kDisplayCenterH =
      ((kBaseDisplayCenterH + 24) < kCenterCoverH) ? (kBaseDisplayCenterH + 24) : kCenterCoverH;
  // Near-side cover proportions -- CrossInk's own tuned ratios (26% of the PRE-clamp base width;
  // 90%/82% of the pre-clamp base height for the edge closer to/farther from the center cover),
  // applied exactly as CrossInk computes them (off kBaseDisplayCenterW/H, not the final
  // kDisplayCenterW/H). "Far" side covers (a 4th/5th book, CrossInk's kFarSideW/kFarSideInnerH/
  // kFarSideOuterH) aren't ported: homeRecentBooksCount caps this fork's carousel at 3 books, so
  // far slots never show.
  static constexpr int kNearSideW = (kBaseDisplayCenterW * 26) / 100;
  static constexpr int kNearSideInnerH = (kBaseDisplayCenterH * 90) / 100;
  static constexpr int kNearSideOuterH = (kBaseDisplayCenterH * 82) / 100;

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // The icon-only menu (see drawButtonMenu) is anchored to the screen's real bottom edge and its
  // vertical footprint doesn't grow with itemCount -- adding the pinned-book row just narrows each
  // icon tile, it never needs another row's worth of height. BaseTheme's default assumes a
  // stacked list-style menu (itemCount rows below menuTop), which -- combined with the old
  // oversized homeCoverTileHeight -- is what made the pinned row's fit-check in
  // HomeActivity::onEnter() always fail for this theme. Returning menuTop directly makes that
  // check reduce to "does the (now correctly sized) cover tile itself fit," which is the only
  // real constraint here.
  int getMenuBottomEdge(const GfxRenderer& renderer, int menuTop, int itemCount) const override;

 private:
  // Remembers the last-centered book so the carousel doesn't reset to book 0 while the icon-menu
  // row below it is focused (selectorIndex >= recentBooks.size()). mutable: drawRecentBookCover is
  // const, matching every other theme's render methods.
  mutable int lastCenterIdx_ = -1;
  // The centered book's completion percentage, computed once when centerIdx changes (not from the
  // progressPercent parameter, which HomeActivity keys on selectorIndex -- a different, home-menu
  // -wide cursor that moves into the icon-menu range, and so goes to -1.0f, the instant the icon
  // menu gets focus, even though the carousel's own centerIdx correctly stays put; that mismatch
  // was why the progress bar disappeared as soon as you touched the button menu). Cached rather
  // than recomputed every render for the same reason Lyra3CoversTheme caches its own -- it's a
  // real Epub::load() otherwise repeated for no reason on every render.
  mutable float cachedCenterProgressPercent_ = -1.0f;
};
