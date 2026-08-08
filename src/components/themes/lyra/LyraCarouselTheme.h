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
  // X3 is 792x528, X4 is 800x480 -- both shorter than a 660px tile. Getting the cover meaningfully
  // larger than the ~150-205 range past chrome trimming meant cutting two more things: the
  // separate title text above the cover (dropped -- real cover art already shows the title; the
  // no-cover fallback still draws it inline, see drawRecentBookCover) and the dot-pagination row
  // (dropped -- the visible side covers already show "book before/after this one," making the
  // dots redundant), plus tightening the icon-menu's own padding (see kMenuRowDrop/kMenuIconSize/
  // kMenuIconPad in the .cpp). Deliberately NOT cut: the button-hints row -- X3/X4 have no touch,
  // so it's the only way a first-time user learns which physical button does what -- and the
  // progress bar, kept per instruction even without cutting the icon-only menu's own vertical
  // footprint. 320/335 below fit X4 (the tighter panel) with a 10px margin against the icon menu's
  // label; see LyraCarouselTheme::getMenuBottomEdge for why the icon-only button menu doesn't need
  // reserving additional space here for the pinned-book row. X3's 48px of extra headroom (792x528
  // vs X4's 800x480) becomes bottom margin, not a bigger cover -- both panels share one
  // compile-time constant, and the cache height has to be one fixed value regardless of which
  // panel is running.
  v.homeCoverHeight = 320;
  v.homeCoverTileHeight = 335;
  v.homeRecentBooksCount = 3;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace LyraCarouselMetrics

// Functional port of CrossInk's Lyra Carousel home theme (https://github.com/uxjulia/CrossInk,
// also a CrossPoint fork -- see NOTICE): a large center cover flanked by smaller side covers and a
// bottom icon-only button menu. Differences from CrossInk's version:
//  - True perspective-warped side covers. CrossInk added a drawPerspectiveBitmap primitive to its
//    GfxRenderer for this; this fork doesn't have it, and adding a new low-level framebuffer
//    primitive without hardware to verify it against felt like the wrong tradeoff. Side covers
//    here are plain scaled rectangles instead -- still reads as a carousel, just not skewed.
//  - The disk-backed frame cache that pre-renders adjacent carousel frames for instant scrolling
//    (CrossInk's HomeActivity.cpp: gCarouselCache, sliding-window pre-render, setPreRenderIndex).
//    This theme redraws normally on selection change, like every other CrossFade theme does.
//  - No separate title text above the cover and no dot-pagination row (both cut in favor of a
//    larger cover -- see homeCoverHeight's comment above); no reading-stats "time read" label
//    (this fork has no reading-stats subsystem to source it from).
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
  // Side covers: a fixed proportion of the center cover's height (not a second, never-pre
  // -generated cache entry -- drawn from the SAME cached thumbnail, downscaled), width at the
  // same 0.6:1 ratio as the center cover for the same reason.
  static constexpr int kSideCoverH = (LyraCarouselMetrics::values.homeCoverHeight * 70) / 100;
  static constexpr int kSideCoverW = static_cast<int>(kSideCoverH * 0.6f);

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
