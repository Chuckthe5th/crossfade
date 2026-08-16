#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/reader/EpubReaderUtils.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/pin.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int kCenterCoverMaxW = LyraCarouselTheme::kCenterCoverW;
constexpr int kCenterCoverMaxH = LyraCarouselTheme::kCenterCoverH;
constexpr int kDisplayCenterW = LyraCarouselTheme::kDisplayCenterW;
constexpr int kDisplayCenterH = LyraCarouselTheme::kDisplayCenterH;
constexpr int kNearSideW = LyraCarouselTheme::kNearSideW;
constexpr int kNearSideInnerH = LyraCarouselTheme::kNearSideInnerH;
constexpr int kNearSideOuterH = LyraCarouselTheme::kNearSideOuterH;
// Purpose-sized cache dimensions -- see their declaration in the header. Center and side covers
// each look up their own cache; HomeActivity::loadRecentCovers() generates both per book.
constexpr int kCenterThumbW = LyraCarouselTheme::kCenterThumbW;
constexpr int kCenterThumbH = LyraCarouselTheme::kCenterThumbH;
constexpr int kSideCoverW = LyraCarouselTheme::kSideCoverW;
constexpr int kSideCoverH = LyraCarouselTheme::kSideCoverH;

constexpr int kCoverTopPad = 18;
constexpr int kCenterCoverVisualInset = LyraCarouselTheme::kCenterCoverVisualInset;
constexpr int kCarouselVerticalLift = 8;
constexpr int kSideOutlineW = 2;
constexpr int kSideCornerRadius = 5;
constexpr int kCoverStackLift = 15;
constexpr int kCenterCoverTopInset = (((kCenterCoverMaxH - kDisplayCenterH) / 2) > kCoverStackLift)
                                         ? ((kCenterCoverMaxH - kDisplayCenterH) / 2) - kCoverStackLift
                                         : 0;

constexpr int kTitleFontId = UI_12_FONT_ID;
constexpr int kMenuLabelFontId = SMALL_FONT_ID;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kTitleTopClearance = 4;
constexpr int kTitleDrawOffset = 5;
constexpr int kTitleBottomGap = 8;
constexpr int kMenuLabelTopGap = 3;
constexpr int kMenuLabelBottomGap = 4;
constexpr int kMenuRowDrop = 31;

constexpr int kFooterTopGap = 10;
constexpr int kFooterProgressBarHeight = 5;
constexpr int kFooterPercentTopGap = 2;
constexpr int kStatsLabelBottomGap = 4;

// Compact "3h 20m" / "45m" / "<1m" duration label -- no translated words, matching the equally
// compact "%.0f%%" progress label just below it (see the progress-bar block), not full prose.
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

// Cache path is derived from the path alone (Epub's constructor does no I/O), so this costs one
// small stats.bin read -- no Epub::load(). EPUB only for now: this fork's reading-stats hook only
// instruments EpubReaderActivity, so an XTC recent book would only ever load a default-constructed
// (empty) BookReadingStats, indistinguishable from truly no stats -- hasEpubExtension short-circuits
// that pointless read.
bool loadCenterBookStats(const std::string& path, BookReadingStats& outStats) {
  if (!SETTINGS.shouldTrackReadingStats() || !FsHelpers::hasEpubExtension(path)) {
    return false;
  }
  const Epub epub(path, "/.crosspoint");
  outStats = BookReadingStats::load(epub.getCachePath());
  return outStats.sessionCount > 0;
}

constexpr int kCornerRadius = 6;
constexpr int kThinOutlineW = 1;    // always-visible outline around the center cover
constexpr int kSelectionLineW = 3;  // thicker outline when the carousel row is focused
constexpr int kCenterOutlineW = 4;  // white ring around the center cover

constexpr int kMenuIconSize = 32;
constexpr int kMenuIconPad = 14;
constexpr int kHighlightPad = 7;
constexpr int kButtonHintsH = LyraCarouselMetrics::values.buttonHintsHeight;

// Same fixed set LyraTheme.cpp's own (private, size-keyed) iconForName keeps for its 32px menu
// row -- Home's button menu only ever shows Folder/Recent/Transfer/Settings/Pin, so this is
// deliberately narrower than a full UIIcon switch rather than a duplicate of that lookup. Uses
// CrossFade's own icon assets, the same ones LyraTheme's own list menu references at size 32 --
// not CrossInk's icon set. Pin (when the pinned-book row is active) reuses PinIcon the same way
// every other theme's menu does; there's no separate reading-stats icon to substitute it for
// since this fork has no stats subsystem.
const uint8_t* menuIconBitmap(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Pin:
      return PinIcon;
    default:
      return nullptr;
  }
}

// Draws bmp inverted (white ink on a filled black selection tile). Reimplements
// GfxRenderer::drawIcon()'s own pixel transform and bit convention (1bpp MSB-first, bit==0 = ink)
// with a foreground-polarity flag -- the same technique EpubReaderMenuActivity's tab icons use,
// NOT CrossInk's own drawIconInverted(), which manipulates the raw framebuffer directly and isn't
// something to port without verifying framebuffer internals this fork hasn't confirmed match.
void drawIconInverted(const GfxRenderer& renderer, const uint8_t bitmap[], const int x, const int y, const int size) {
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + (size - 1 - row), y + col, false);
      }
    }
  }
}

struct MenuLayoutMetrics {
  int tileH;
  int tileW;
  int labelLineHeight;
  int rowY;
  int labelY;
};

// Ported from CrossInk exactly, including the "+ kMenuRowDrop" sign -- an earlier version of this
// port had it subtracted, which was backwards (it made the icon row sit noticeably higher/tighter
// than CrossInk's own, eating into the cover's vertical budget for no reason).
MenuLayoutMetrics computeMenuLayout(const GfxRenderer& renderer, const int buttonCount) {
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int labelLineHeight = renderer.getLineHeight(kMenuLabelFontId);
  const int rowY = renderer.getScreenHeight() - kButtonHintsH - tileH - kMenuLabelTopGap - labelLineHeight -
                   kMenuLabelBottomGap + kMenuRowDrop;
  return {tileH, renderer.getScreenWidth() / std::max(1, buttonCount), labelLineHeight, rowY,
          rowY - kMenuLabelTopGap - labelLineHeight};
}

Rect shrinkCenterCoverRect(const Rect& rect) {
  const int width = std::max(0, rect.width - kCenterCoverVisualInset * 2);
  const int height = std::max(0, rect.height - kCenterCoverVisualInset * 2);
  return Rect{rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2, width, height};
}

// Ported from CrossInk exactly: title reservation determines centerTileY (2 lines' worth of
// clearance, or kCoverTopPad if that's taller), kCenterCoverTopInset nudges the cover down
// slightly when the display height is far below the cache-quality ceiling (see the header's
// kCenterCoverW comment -- currently 0 at this fork's solved size), then kCarouselVerticalLift
// pulls it back up a bit.
Rect computeCenterCoverSlotRect(const GfxRenderer& renderer, const Rect rect) {
  const int screenW = renderer.getScreenWidth();
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int reservedTitleBlockHeight = titleLineHeight * 2;
  const int titleY = rect.y + kTitleTopClearance;
  const int centerTileY = std::max(rect.y + kCoverTopPad, titleY + reservedTitleBlockHeight + kTitleBottomGap);
  const int centerDrawY = centerTileY + kCenterCoverTopInset - kCarouselVerticalLift;
  const int centerX = (screenW - kDisplayCenterW) / 2;
  return Rect{centerX, centerDrawY, kDisplayCenterW, kDisplayCenterH};
}

// Ported from CrossInk's LyraCarouselTheme.cpp verbatim (theme-local helper, not a GfxRenderer
// primitive -- unlike drawPerspectiveBitmap, this only needs drawLine/fillRect, both already
// public). Outlines a trapezoid: slanted top/bottom edges between (x, y+topLeft) and
// (rightX, y+topRight) / bottoms, vertical edges at each end's own height, with a 2px erase gap
// below so an adjacent cell's shadow doesn't visually merge with this one's border.
void drawPerspectiveOutline(const GfxRenderer& renderer, const int x, const int y, const int width,
                            const int leftHeight, const int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  const int topLeft = (maxHeight - leftHeight) / 2;
  const int topRight = (maxHeight - rightHeight) / 2;
  const int bottomLeft = topLeft + leftHeight - 1;
  const int bottomRight = topRight + rightHeight - 1;
  const int rightX = x + width - 1;

  renderer.drawLine(x, y + topLeft, rightX, y + topRight, kSideOutlineW, true);
  renderer.drawLine(x, y + bottomLeft, rightX, y + bottomRight, kSideOutlineW, true);
  renderer.fillRect(x, y + topLeft, kSideOutlineW, leftHeight, true);
  renderer.fillRect(rightX - kSideOutlineW + 1, y + topRight, kSideOutlineW, rightHeight, true);
  renderer.fillRect(x, y + maxHeight + 1, width, 2, false);
}

// Ported from CrossInk verbatim: the no-cover fallback for a perspective side slot -- fills each
// column to its own linearly-interpolated height (same per-column math drawPerspectiveBitmap uses
// for real cover art), producing a solid trapezoid silhouette instead of a plain rectangle.
void fillPerspectiveSilhouette(const GfxRenderer& renderer, const int x, const int y, const int width,
                               const int leftHeight, const int rightHeight) {
  const int maxHeight = std::max(leftHeight, rightHeight);
  renderer.fillRect(x, y, width, maxHeight, false);
  for (int dx = 0; dx < width; ++dx) {
    const int columnHeight =
        (width <= 1) ? leftHeight : (leftHeight + ((rightHeight - leftHeight) * dx) / (width - 1));
    const int top = y + (maxHeight - columnHeight) / 2;
    renderer.fillRect(x + dx, top, 1, columnHeight, true);
  }
}

// Loads the book's center-box cache thumbnail (kCenterThumbW x kCenterThumbH, generated
// letterbox-contained by HomeActivity::loadRecentCovers()) and draws it into targetRect, cropped
// on WHICHEVER axis (width or height) has surplus so the image fills the box exactly without
// stretching -- a small correction, since generation already sized the source close to targetRect.
// Ported from CrossInk's drawCenterCover
// lambda: unlike this file's earlier cropX-only version, targetRect is not required to match the
// source image's aspect ratio. CrossInk's own box (kDisplayCenterW x kDisplayCenterH, from the
// header's two-tier sizing) isn't aspect-locked to a portrait book cover at all -- width and
// height derive from independent base numbers -- so cropping only one axis would leave blank
// space on the other, which is exactly the bug the one-axis version had.
// GfxRenderer::drawBitmap already accepts a cropY parameter (it just wasn't being computed here
// before); no renderer change was needed for this fix.
bool drawCroppedCover(const GfxRenderer& renderer, const std::string& coverBmpPath, const Rect targetRect) {
  if (coverBmpPath.empty()) return false;
  const std::string thumbPath = UITheme::getCoverThumbPath(coverBmpPath, kCenterThumbW, kCenterThumbH);
  HalFile file;
  if (!Storage.openFileForRead("HOME", thumbPath, file)) return false;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    file.close();
    return false;
  }
  const float srcRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
  const float targetRatio =
      static_cast<float>(targetRect.width) / static_cast<float>(targetRect.height == 0 ? 1 : targetRect.height);
  float cropX = 0.0f;
  float cropY = 0.0f;
  if (srcRatio > targetRatio) {
    cropX = std::max(0.0f, 1.0f - (targetRatio / srcRatio));
  } else if (srcRatio < targetRatio) {
    cropY = std::max(0.0f, 1.0f - (srcRatio / targetRatio));
  }
  renderer.drawBitmap(bitmap, targetRect.x, targetRect.y, targetRect.width, targetRect.height, cropX, cropY);
  file.close();
  return true;
}
}  // namespace

void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                            const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                            bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                            float /*progressPercent*/) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = selectorIndex >= 0 && selectorIndex < bookCount;
  int centerIdx = inCarouselRow ? selectorIndex : (lastCenterIdx_ >= 0 ? lastCenterIdx_ : 0);
  if (centerIdx >= bookCount) centerIdx = bookCount - 1;
  const bool centerChanged = centerIdx != lastCenterIdx_;
  if (centerChanged) {
    coverRendered = false;
    coverBufferStored = false;
    // See cachedCenterProgressPercent_'s comment: computed here (only when the centered book
    // actually changes), not from the progressPercent parameter.
    cachedCenterProgressPercent_ = EpubReaderUtils::recentBookProgressPercent(recentBooks[centerIdx].path);
  }
  lastCenterIdx_ = centerIdx;
  // Unlike cachedCenterProgressPercent_, reloaded every render rather than only on a centerIdx
  // change: BookReadingStats::load() is a small (~19-byte) file read, not an Epub::load() parse,
  // so caching it isn't worth the staleness risk. That risk is real, not theoretical -- this
  // instance lives for the whole app session (UITheme::currentTheme), so gating it behind
  // centerChanged meant the label could stay stuck on a stats.bin snapshot from before the last
  // reading session (or from before SETTINGS.shouldTrackReadingStats() was even turned on) for as
  // long as the same book stayed centered, which is the common case.
  hasCachedCenterStats_ = loadCenterBookStats(recentBooks[centerIdx].path, cachedCenterStats_);

  // Same book already fully drawn and buffered, and the buffer HomeActivity just restored is
  // still for that same centerIdx -- nothing to redraw. If centerIdx just changed, bufferRestored
  // reflects a snapshot HomeActivity restored for the OLD centerIdx (it runs restoreCoverBuffer()
  // before calling in here, off last frame's coverBufferStored) -- trusting it here would suppress
  // the entire redraw below (sides, center, title, dots, progress bar all skip together) and leave
  // the stale frame on screen, perpetually one selection behind. The fillRect right below already
  // clears the whole tile, so forcing a real redraw here safely overwrites whatever stale pixels
  // that restore just wrote.
  if (coverRendered || (bufferRestored && !centerChanged)) return;

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const int screenW = renderer.getScreenWidth();
  const Rect centerCoverSlotRect = computeCenterCoverSlotRect(renderer, rect);
  const Rect centerRect = shrinkCenterCoverRect(centerCoverSlotRect);

  // --- Side covers: perspective-skewed trapezoids, ported from CrossInk (see the header's class
  // comment and GfxRenderer::drawPerspectiveBitmap's declaration). Near side only --
  // homeRecentBooksCount caps this fork's carousel at 3 books, so CrossInk's "far" slots (a
  // 4th/5th book) never apply here. ---
  const int sideMaxHeight = std::max(kNearSideInnerH, kNearSideOuterH);
  const int sideTileY = centerCoverSlotRect.y + (kDisplayCenterH - sideMaxHeight) / 2;
  constexpr int kNearOverlap = 4;
  constexpr int kNearCoverInset = 10;
  const int baseLeftNearX = centerCoverSlotRect.x - kNearSideW + kNearOverlap;
  const int baseRightNearX = centerCoverSlotRect.x + kDisplayCenterW - kNearOverlap;
  const int leftNearX = baseLeftNearX + kNearCoverInset;
  const int rightNearX = baseRightNearX - kNearCoverInset;

  auto drawSideCover = [&](const int idx, const int x, const int leftHeight, const int rightHeight) {
    const RecentBook& book = recentBooks[idx];
    const int sideHeight = std::max(leftHeight, rightHeight);
    if (!book.coverBmpPath.empty()) {
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, kSideCoverW, kSideCoverH);
      HalFile file;
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.fillRect(x, sideTileY, kNearSideW, sideHeight, false);
          renderer.drawPerspectiveBitmap(bitmap, x, sideTileY, kNearSideW, leftHeight, rightHeight);
          renderer.maskRoundedRectOutsideCorners(x, sideTileY, kNearSideW, sideHeight, kSideCornerRadius,
                                                 Color::White);
          file.close();
          drawPerspectiveOutline(renderer, x, sideTileY, kNearSideW, leftHeight, rightHeight);
          return;
        }
        file.close();
      }
    }
    fillPerspectiveSilhouette(renderer, x, sideTileY, kNearSideW, leftHeight, rightHeight);
    renderer.maskRoundedRectOutsideCorners(x, sideTileY, kNearSideW, sideHeight, kSideCornerRadius, Color::White);
  };
  // Matches CrossInk's own gating (2 books -> one side only, both slots would otherwise show the
  // same "other" book) and its exact parameter order: the left cover's near (right, facing
  // center) edge gets the taller Inner height, its far (left) edge the shorter Outer height,
  // receding away from center; the right cover mirrors this. Not rederived -- ported as given.
  if (bookCount >= 2) {
    drawSideCover((centerIdx + bookCount - 1) % bookCount, leftNearX, kNearSideInnerH, kNearSideOuterH);
  }
  if (bookCount >= 3) {
    drawSideCover((centerIdx + 1) % bookCount, rightNearX, kNearSideOuterH, kNearSideInnerH);
  }

  // --- Center cover ---
  const RecentBook& centerBook = recentBooks[centerIdx];
  renderer.fillRect(centerRect.x - kCenterOutlineW, centerRect.y - kCenterOutlineW, centerRect.width + 2 * kCenterOutlineW,
                    centerRect.height + 2 * kCenterOutlineW, false);
  if (drawCroppedCover(renderer, centerBook.coverBmpPath, centerRect)) {
    renderer.maskRoundedRectOutsideCorners(centerRect.x, centerRect.y, centerRect.width, centerRect.height,
                                           kCornerRadius, Color::White);
  } else {
    renderer.drawRoundedRect(centerRect.x, centerRect.y, centerRect.width, centerRect.height, 1, kCornerRadius, true);
    renderer.fillRoundedRect(centerRect.x, centerRect.y + centerRect.height / 3, centerRect.width,
                             2 * centerRect.height / 3, kCornerRadius, /*roundTopLeft=*/false, /*roundTopRight=*/false,
                             /*roundBottomLeft=*/true, /*roundBottomRight=*/true, Color::Black);
    const int iconX = centerRect.x + centerRect.width / 2 - 16;
    const int iconY = centerRect.y + centerRect.height / 3 + 14;
    renderer.drawIcon(CoverIcon, iconX, iconY, 32);
    const int fallbackLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const auto fallbackLines =
        renderer.wrappedText(UI_10_FONT_ID, centerBook.title.c_str(), centerRect.width - 28, 3, EpdFontFamily::BOLD);
    int lineY = iconY + 32 + 10;
    for (const auto& line : fallbackLines) {
      const int lineW = renderer.getTextWidth(UI_10_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, centerRect.x + (centerRect.width - lineW) / 2, lineY, line.c_str(), false,
                        EpdFontFamily::BOLD);
      lineY += fallbackLineHeight;
    }
  }

  // --- Title above the center cover -- restored to CrossInk's original 2-line wrap+ellipsize
  // (an earlier version of this port dropped it to 1 line to reclaim cover height; that trade
  // isn't needed at this fork's solved size). ---
  const auto titleLines = renderer.wrappedText(kTitleFontId, centerBook.title.c_str(),
                                               std::min(screenW - 40, kCenterCoverMaxW + 40), 2, EpdFontFamily::BOLD);
  const int titleLineHeight = renderer.getLineHeight(kTitleFontId);
  const int textCenterX = centerRect.x + centerRect.width / 2;
  int currentTitleY = rect.y + kTitleTopClearance + kTitleDrawOffset;
  for (const auto& line : titleLines) {
    const int titleW = renderer.getTextWidth(kTitleFontId, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(kTitleFontId, textCenterX - titleW / 2, currentTitleY, line.c_str(), true, EpdFontFamily::BOLD);
    currentTitleY += titleLineHeight;
  }

  // --- Dots: centered under the center cover, one per book -- restored (CrossInk keeps these;
  // an earlier version of this port dropped them as "redundant with the visible side covers,"
  // which wasn't the right trade to make at this fork's solved size either). ---
  const int dotsY = centerCoverSlotRect.y + centerCoverSlotRect.height + 8;
  const int totalDotsW = bookCount * kDotSize + (bookCount - 1) * kDotGap;
  int dotX = centerCoverSlotRect.x + (centerCoverSlotRect.width - totalDotsW) / 2;
  for (int i = 0; i < bookCount; ++i) {
    if (i == centerIdx) {
      renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
    } else {
      renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
    }
    dotX += kDotSize + kDotGap;
  }

  // --- Progress bar + percentage label: CrossFade's own real per-book percentage, computed for
  // centerIdx (see cachedCenterProgressPercent_'s comment -- not the passed-in progressPercent
  // parameter). ---
  int footerBottomY = dotsY + kDotSize + kFooterTopGap;
  if (cachedCenterProgressPercent_ >= 0.0f) {
    const int footerY = footerBottomY;
    const int footerWidth = std::min(screenW - 2 * LyraCarouselMetrics::values.contentSidePadding, centerRect.width);
    const int footerX = centerRect.x + (centerRect.width - footerWidth) / 2;
    const float clamped = std::clamp(cachedCenterProgressPercent_, 0.0f, 100.0f);
    const int filledWidth = std::clamp(static_cast<int>((clamped / 100.0f) * footerWidth), 0, footerWidth);
    renderer.fillRectDither(footerX, footerY, footerWidth, kFooterProgressBarHeight, Color::LightGray);
    if (filledWidth > 0) {
      renderer.fillRect(footerX, footerY, filledWidth, kFooterProgressBarHeight, true);
    }
    char label[16];
    snprintf(label, sizeof(label), "%.0f%%", clamped);
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::REGULAR);
    const int percentLabelY = footerY + kFooterProgressBarHeight + kFooterPercentTopGap;
    renderer.drawText(UI_10_FONT_ID, footerX + footerWidth - labelW, percentLabelY, label, true,
                      EpdFontFamily::REGULAR);
    footerBottomY = percentLabelY + renderer.getLineHeight(UI_10_FONT_ID) + kStatsLabelBottomGap;
  }

  // --- Reading-stats time label (time read, and time left once a pace estimate exists), below
  // the progress bar and its percentage label -- sourced from cachedCenterStats_ (centerIdx-keyed,
  // same reasoning as cachedCenterProgressPercent_ -- see its comment, and reloaded fresh every
  // render -- see hasCachedCenterStats_'s assignment above). Omitted entirely when reading-stats
  // tracking is off (SETTINGS.shouldTrackReadingStats()) or no qualifying session has been
  // recorded yet for this book, rather than drawing a misleading "0m read".
  if (hasCachedCenterStats_) {
    char timeReadBuf[16];
    formatCompactDuration(cachedCenterStats_.totalReadingSeconds, timeReadBuf, sizeof(timeReadBuf));
    std::string timeLabel = std::string(timeReadBuf) + " read";
    if (cachedCenterStats_.estimatedTimeLeftSeconds > 0) {
      char timeLeftBuf[16];
      formatCompactDuration(cachedCenterStats_.estimatedTimeLeftSeconds, timeLeftBuf, sizeof(timeLeftBuf));
      timeLabel += " - " + std::string(timeLeftBuf) + " left";
    }
    const int timeLabelW = renderer.getTextWidth(UI_10_FONT_ID, timeLabel.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, centerRect.x + (centerRect.width - timeLabelW) / 2, footerBottomY,
                      timeLabel.c_str(), true, EpdFontFamily::REGULAR);
  }

  // --- Outline around the center cover, thicker when the carousel row itself is focused ---
  const int outlineW = inCarouselRow ? kSelectionLineW : kThinOutlineW;
  renderer.drawRoundedRect(centerRect.x, centerRect.y, centerRect.width, centerRect.height, outlineW, kCornerRadius,
                           true);

  coverBufferStored = storeCoverBuffer();
  coverRendered = coverBufferStored;
}

// ---------------------------------------------------------------------------
// Horizontal icon-only menu row, anchored near the bottom of the screen
// ---------------------------------------------------------------------------
void LyraCarouselTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, const int buttonCount, const int selectedIndex,
                                       const std::function<std::string(int index)>& buttonLabel,
                                       const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rect;  // retained by the BaseTheme interface; this menu anchors to the screen bottom
  if (buttonCount <= 0) return;

  const MenuLayoutMetrics metrics = computeMenuLayout(renderer, buttonCount);

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = i * metrics.tileW;
    const int iconX = tileX + (metrics.tileW - kMenuIconSize) / 2;
    const int iconY = metrics.rowY + kMenuIconPad;
    const bool selected = (selectedIndex == i);

    if (selected) {
      const int highlightSize = kMenuIconSize + 2 * kHighlightPad;
      const int highlightY = metrics.rowY + (metrics.tileH - highlightSize) / 2;
      renderer.fillRoundedRect(iconX - kHighlightPad, highlightY, highlightSize, highlightSize, kCornerRadius,
                               Color::Black);
    }

    if (rowIcon != nullptr) {
      const uint8_t* bmp = menuIconBitmap(rowIcon(i));
      if (bmp != nullptr) {
        if (selected) {
          drawIconInverted(renderer, bmp, iconX, iconY, kMenuIconSize);
        } else {
          renderer.drawIcon(bmp, iconX, iconY, kMenuIconSize);
        }
      }
    }
  }

  renderer.fillRect(0, metrics.labelY, renderer.getScreenWidth(), metrics.labelLineHeight, false);
  if (selectedIndex >= 0 && selectedIndex < buttonCount && buttonLabel != nullptr) {
    const std::string label = renderer.truncatedText(kMenuLabelFontId, buttonLabel(selectedIndex).c_str(),
                                                      renderer.getScreenWidth() - 40, EpdFontFamily::REGULAR);
    const int labelWidth = renderer.getTextWidth(kMenuLabelFontId, label.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(kMenuLabelFontId, (renderer.getScreenWidth() - labelWidth) / 2, metrics.labelY + 2, label.c_str(),
                      true, EpdFontFamily::REGULAR);
  }
}

int LyraCarouselTheme::getMenuBottomEdge(const GfxRenderer&, const int menuTop, const int) const {
  // See the declaration's comment: this menu is an icon row anchored to the screen's real bottom
  // (computeMenuLayout), not a list stacked below menuTop, so it has no itemCount-dependent height
  // to add here -- returning menuTop makes HomeActivity::onEnter()'s pinned-row fit-check reduce to
  // "does the cover tile fit," the only real constraint for this theme.
  return menuTop;
}
