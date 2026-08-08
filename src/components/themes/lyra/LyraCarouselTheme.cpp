#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
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
constexpr int kSideCoverMaxW = LyraCarouselTheme::kSideCoverW;
constexpr int kSideCoverMaxH = LyraCarouselTheme::kSideCoverH;
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
constexpr int kMenuRowDrop = 31;

constexpr int kFooterTopGap = 10;
constexpr int kFooterProgressBarHeight = 5;
constexpr int kFooterPercentTopGap = 2;

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
// deliberately narrower than a full UIIcon switch rather than a duplicate of that lookup.
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

MenuLayoutMetrics computeMenuLayout(const GfxRenderer& renderer, const int buttonCount) {
  const int tileH = kMenuIconPad + kMenuIconSize + kMenuIconPad;
  const int labelLineHeight = renderer.getLineHeight(kMenuLabelFontId);
  const int rowY = renderer.getScreenHeight() - kButtonHintsH - tileH - labelLineHeight - kMenuRowDrop;
  return {tileH, renderer.getScreenWidth() / std::max(1, buttonCount), labelLineHeight, rowY, rowY - labelLineHeight};
}

Rect shrinkCenterCoverRect(const Rect& rect) {
  const int width = std::max(0, rect.width - kCenterCoverVisualInset * 2);
  const int height = std::max(0, rect.height - kCenterCoverVisualInset * 2);
  return Rect{rect.x + (rect.width - width) / 2, rect.y + (rect.height - height) / 2, width, height};
}

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

// Loads book's cover thumbnail (cached by height, matching Lyra3CoversTheme's own approach --
// this fork's UITheme::getCoverThumbPath only takes a height, not CrossInk's explicit width+height
// variant) and draws it into targetRect, center-cropped to the target's aspect ratio. Returns false
// (having drawn nothing) if there's no cover or it can't be read.
bool drawCroppedCover(const GfxRenderer& renderer, const std::string& coverBmpPath, const Rect targetRect) {
  if (coverBmpPath.empty()) return false;
  const std::string thumbPath = UITheme::getCoverThumbPath(coverBmpPath, targetRect.height);
  HalFile file;
  if (!Storage.openFileForRead("HOME", thumbPath, file)) return false;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    file.close();
    return false;
  }
  const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
  const float tileRatio = static_cast<float>(targetRect.width) / static_cast<float>(targetRect.height);
  const float cropX = std::max(0.0f, 1.0f - (tileRatio / ratio));
  renderer.drawBitmap(bitmap, targetRect.x, targetRect.y, targetRect.width, targetRect.height, cropX);
  file.close();
  return true;
}
}  // namespace

void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                            const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                            bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                            float progressPercent) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }

  const int bookCount = static_cast<int>(recentBooks.size());
  const bool inCarouselRow = selectorIndex >= 0 && selectorIndex < bookCount;
  int centerIdx = inCarouselRow ? selectorIndex : (lastCenterIdx_ >= 0 ? lastCenterIdx_ : 0);
  if (centerIdx >= bookCount) centerIdx = bookCount - 1;
  if (centerIdx != lastCenterIdx_) {
    coverRendered = false;
    coverBufferStored = false;
  }
  lastCenterIdx_ = centerIdx;

  // Same book already fully drawn and buffered (or the buffer was just restored from a snapshot
  // of that same frame) -- nothing to redraw. Matches Lyra3CoversTheme's own coverRendered gate.
  if (coverRendered || bufferRestored) return;

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const int screenW = renderer.getScreenWidth();
  const Rect centerCoverSlotRect = computeCenterCoverSlotRect(renderer, rect);
  const Rect centerRect = shrinkCenterCoverRect(centerCoverSlotRect);

  // --- Side covers: plain scaled rectangles, not CrossInk's perspective-warped trapezoids (see
  // the header's class comment). Still reads as a carousel -- smaller, offset covers flanking the
  // center one -- just without the skew. ---
  const int sideY = centerCoverSlotRect.y + (kDisplayCenterH - kSideCoverMaxH) / 2;
  const int leftX = centerCoverSlotRect.x - kSideCoverMaxW + 20;
  const int rightX = centerCoverSlotRect.x + kDisplayCenterW - 20;

  auto drawSideCover = [&](const int idx, const int x) {
    const RecentBook& book = recentBooks[idx];
    renderer.fillRect(x, sideY, kSideCoverMaxW, kSideCoverMaxH, false);
    if (!drawCroppedCover(renderer, book.coverBmpPath, Rect{x, sideY, kSideCoverMaxW, kSideCoverMaxH})) {
      renderer.fillRect(x, sideY + kSideCoverMaxH / 3, kSideCoverMaxW, 2 * kSideCoverMaxH / 3, true);
      renderer.drawIcon(CoverIcon, x + kSideCoverMaxW / 2 - 16, sideY + kSideCoverMaxH / 3 + 14, 32);
    }
    renderer.drawRoundedRect(x, sideY, kSideCoverMaxW, kSideCoverMaxH, kSideOutlineW, kSideCornerRadius, true);
  };
  // Matches CrossInk's own gating: with exactly 2 books, only draw one side (both slots would
  // otherwise show the same "other" book).
  if (bookCount >= 2) drawSideCover((centerIdx + bookCount - 1) % bookCount, leftX);
  if (bookCount >= 3) drawSideCover((centerIdx + 1) % bookCount, rightX);

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

  // --- Title above the center cover ---
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

  // --- Dots: centered under the center cover, one per book ---
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

  // --- Progress bar: CrossFade's own real per-book percentage (HomeActivity computes it).
  // CrossInk's "total time read" label here comes from its BookReadingStats subsystem, which
  // this fork doesn't have -- dropped, not approximated. ---
  if (progressPercent >= 0.0f) {
    const int footerY = dotsY + kDotSize + kFooterTopGap;
    const int footerWidth = std::min(screenW - 2 * LyraCarouselMetrics::values.contentSidePadding, centerRect.width);
    const int footerX = centerRect.x + (centerRect.width - footerWidth) / 2;
    const float clamped = std::clamp(progressPercent, 0.0f, 100.0f);
    const int filledWidth = std::clamp(static_cast<int>((clamped / 100.0f) * footerWidth), 0, footerWidth);
    renderer.fillRectDither(footerX, footerY, footerWidth, kFooterProgressBarHeight, Color::LightGray);
    if (filledWidth > 0) {
      renderer.fillRect(footerX, footerY, filledWidth, kFooterProgressBarHeight, true);
    }
    char label[16];
    snprintf(label, sizeof(label), "%.0f%%", clamped);
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, footerX + footerWidth - labelW,
                      footerY + kFooterProgressBarHeight + kFooterPercentTopGap, label, true, EpdFontFamily::REGULAR);
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
