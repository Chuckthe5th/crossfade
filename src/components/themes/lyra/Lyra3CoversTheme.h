

#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace Lyra3CoversMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeCoverTileHeight = 300;
  v.homeRecentBooksCount = 3;
  return v;
}();
}  // namespace Lyra3CoversMetrics

class Lyra3CoversTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, float progressPercent = -1.0f) const override;

 private:
  // Per-book completion percentage for the up-to-3 covers shown at once, computed once per
  // cover-set-change (inside the coverRendered-gated block, alongside the cover bitmap load) and
  // reused by every render until then -- recomputing via a real Epub::load() on every single
  // render, including ones that don't change which books are showing, was real, needless load on
  // a memory-constrained device. -1.0f = not yet computed / no progress available.
  mutable float cachedProgressPercent_[Lyra3CoversMetrics::values.homeRecentBooksCount] = {};
};
