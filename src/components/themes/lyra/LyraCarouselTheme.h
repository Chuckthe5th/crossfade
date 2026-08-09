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
  // The ONE height passed to HomeActivity::loadRecentCovers() (which pre-generates the SD-card
  // thumbnail cache) and to every UITheme::getCoverThumbPath() lookup in LyraCarouselTheme.cpp --
  // center and side covers alike draw a (downscaled) crop of this same cached bitmap rather than
  // each requesting their own derived size. The two used to drift (draw code asked for
  // kDisplayCenterH=488/kSideCoverH=390, cache only ever held homeCoverHeight=600), which is why
  // every cover missed the cache and fell back to the placeholder icon.
  //
  // 600 (CrossInk's own value, unchanged since the initial port) doesn't fit either panel's real
  // screen height (X3 792x528, X4 800x480) when run through CrossInk's OWN formula chain --
  // verified by re-deriving that chain exactly (kCenterCoverH = homeCoverHeight-60,
  // kBaseDisplayCenterH = 86% of that, kDisplayCenterH = min(+24, kCenterCoverH), same title
  // -reservation and footer-stacking math as CrossInk's computeCenterCoverSlotRect/
  // drawRecentBookCover) rather than the simplified 0.6-ratio, chrome-trimmed version this file
  // used previously. This IS the CrossInk layout -- title, dots, percentage label, and the normal
  // button-hints row all present, exactly as CrossInk ships it -- solved for the base height that
  // makes CrossInk's own formula fit X4 (the tighter panel) instead of a copy-pasted 600 that
  // doesn't. 263/325 below is that solve, ~10px margin against the icon menu's label on X4. X3's
  // extra headroom (792x528 vs X4's 800x480) becomes bottom margin, not a bigger cover -- both
  // panels share one compile-time constant, and the cache height has to be one fixed value
  // regardless of which panel is running.
  v.homeCoverHeight = 263;
  v.homeCoverTileHeight = 325;
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
// formulas, solved against this fork's actual homeCoverHeight rather than copying its literal
// pixel constants (which were tuned for a taller screen than X3/X4 -- see homeCoverHeight's
// comment above).
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
