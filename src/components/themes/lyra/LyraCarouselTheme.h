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
  // 600/660 (the original values, unchanged since the initial port) also didn't fit either panel:
  // X3 is 792x528, X4 is 800x480 -- both shorter than a 660px tile. Getting the cover close to
  // CrossInk's own proportions (near-full-height) took, on top of the earlier chrome trims
  // (dropped title text and dot row, tightened icon-menu padding -- see the .cpp), one more cut:
  // buttonHintsHeight is 0 for this theme specifically (see drawButtonHints's override) -- the
  // icon-only menu already names the selected action, unlike every other theme's plain icon row,
  // so the hint row CrossFade normally shows non-touch users which physical button does what is
  // judged redundant here. Every other theme keeps it. 355/375 below fit X4 (the tighter panel)
  // with a ~15px margin against the icon menu's label; see LyraCarouselTheme::getMenuBottomEdge
  // for why the icon-only button menu doesn't need reserving additional space here for the
  // pinned-book row. X3's 48px of extra headroom (792x528 vs X4's 800x480) becomes bottom margin,
  // not a bigger cover -- both panels share one compile-time constant, and the cache height has to
  // be one fixed value regardless of which panel is running.
  v.homeCoverHeight = 355;
  v.homeCoverTileHeight = 375;
  v.buttonHintsHeight = 0;
  v.homeRecentBooksCount = 3;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace LyraCarouselMetrics

// Functional port of CrossInk's Lyra Carousel home theme (https://github.com/uxjulia/CrossInk,
// also a CrossPoint fork -- see NOTICE): a large center cover flanked by smaller perspective
// -skewed side covers and a bottom icon-only button menu. Differences from CrossInk's version:
//  - The disk-backed frame cache that pre-renders adjacent carousel frames for instant scrolling
//    (CrossInk's HomeActivity.cpp: gCarouselCache, sliding-window pre-render, setPreRenderIndex).
//    This theme redraws normally on selection change, like every other CrossFade theme does.
//  - No separate title text above the cover and no dot-pagination row (both cut in favor of a
//    larger cover -- see homeCoverHeight's comment above); no reading-stats "time read" label
//    (this fork has no reading-stats subsystem to source it from).
// The perspective skew itself (GfxRenderer::drawPerspectiveBitmap, ported from CrossInk's own
// GfxRenderer -- see its declaration) and the near-side cover proportions below (26%/90%/82%) are
// straight ports of CrossInk's tuned values, applied to this fork's own cover dimensions.
class LyraCarouselTheme : public LyraTheme {
 public:
  static constexpr int kCenterCoverVisualInset = 10;
  // Cover box dimensions both derive from homeCoverHeight, at the SAME 0.6:1 width:height ratio
  // Epub::generateThumbBmp(height) actually crops the cached thumbnail to (see its comment) --
  // previously width was a fixed 316px independent of height, which was fine while height was
  // 488 (ratio 0.65, close enough to look right) but badly wrong once height shrank to 150-205
  // chasing the X3/X4 fit (ratio up to 1.54 -- landscape, for a portrait book cover). The bitmap
  // only fills part of a box shaped like that, and anything sized off centerRect.width (e.g. the
  // progress bar) comes out oversized too. Matching the generation ratio here means the cached
  // bitmap fills its box exactly, regardless of what homeCoverHeight is tuned to.
  static constexpr int kDisplayCenterH = LyraCarouselMetrics::values.homeCoverHeight;
  static constexpr int kDisplayCenterW = static_cast<int>(kDisplayCenterH * 0.6f);
  // Near-side cover proportions -- CrossInk's own tuned ratios (26% of center width; 90%/82% of
  // center height for the edge closer to/farther from the center cover), applied to this fork's
  // own kDisplayCenterW/H instead of CrossInk's separate pre-shrink "max" dimensions (this fork
  // has no equivalent -- see kDisplayCenterH's comment on why that indirection was dropped).
  // "Far" side covers (a 4th/5th book, CrossInk's kFarSideW/kFarSideInnerH/kFarSideOuterH) aren't
  // ported: homeRecentBooksCount caps this fork's carousel at 3 books, so far slots never show.
  static constexpr int kNearSideW = (kDisplayCenterW * 26) / 100;
  static constexpr int kNearSideInnerH = (kDisplayCenterH * 90) / 100;
  static constexpr int kNearSideOuterH = (kDisplayCenterH * 82) / 100;

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  // No-op: see homeCoverHeight's comment on why this theme drops the button-hints row that every
  // other theme shows non-touch users. BaseTheme::drawButtonHints doesn't consult per-theme
  // metrics (it draws at a fixed position derived from BaseMetrics), so zeroing
  // buttonHintsHeight alone only fixes this theme's OWN layout math -- drawButtonHints itself
  // still has to be suppressed here, or it would draw over content this theme now puts in that
  // reclaimed space.
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
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
