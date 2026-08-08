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
  // X3 is 792x528, X4 is 800x480 -- both shorter than a 660px tile. 205/275 below were sized to
  // fit X4 (the tighter of the two): title reserved to 1 line (not 2), dot row, progress bar (no
  // percentage label -- the bar alone conveys progress; dropped to make room), and the existing
  // icon-only button menu at the screen's real bottom all accounted for, leaving a 6px margin. See
  // LyraCarouselTheme::getMenuBottomEdge for why the icon-only button menu doesn't need reserving
  // additional space here for the pinned-book row. X3's 48px of extra headroom (792x528 vs X4's
  // 800x480) becomes bottom margin, not a bigger cover -- both panels share one compile-time
  // constant, and the cache height has to be one fixed value regardless of which panel is running.
  v.homeCoverHeight = 205;
  v.homeCoverTileHeight = 275;
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
  // Width is unaffected by the X3/X4 layout bug (both panels are 792-800px wide, plenty for a
  // 340px cover) -- kept exactly as originally sized, including the 86%+24 "shrink slightly, but
  // not below what a small clamp allows" visual proportion.
  static constexpr int kCenterCoverW = 340;
  static constexpr int kBaseDisplayCenterW = (kCenterCoverW * 86) / 100;
  static constexpr int kDisplayCenterW =
      ((kBaseDisplayCenterW + 24) < kCenterCoverW) ? (kBaseDisplayCenterW + 24) : kCenterCoverW;
  static constexpr int kCenterCoverVisualInset = 10;
  // Height has no separate "max vs display" tier (unlike width, above) -- that indirection is what
  // let the display height (488) drift from homeCoverHeight (600, the cache height) in the first
  // place. The cover is simply displayed at homeCoverHeight; see LyraCarouselMetrics's comment.
  static constexpr int kDisplayCenterH = LyraCarouselMetrics::values.homeCoverHeight;
  static constexpr int kSideCoverW = 200;
  // Side covers: a fixed proportion of the center cover's height, drawn from the SAME cached
  // thumbnail (downscaled) rather than a second, never-pre-generated cache entry.
  static constexpr int kSideCoverH = (LyraCarouselMetrics::values.homeCoverHeight * 70) / 100;

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
