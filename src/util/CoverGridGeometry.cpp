#include "CoverGridGeometry.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace CoverGridGeometry {

namespace {
// Target cell size grid columns/rows are derived from at runtime -- never a
// hardcoded panel width/height, so the same binary lays out correctly on X3
// (792x528) and X4 (800x480).
constexpr int TARGET_CELL_WIDTH = 140;
constexpr int TARGET_CELL_HEIGHT = 190;
}  // namespace

Geometry compute(GfxRenderer& renderer) {
  Geometry g;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  int viewTop, viewRight, viewBottom, viewLeft;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  g.gridTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  g.gridLeft = viewLeft;
  const int gridWidth = pageWidth - viewLeft - viewRight;
  const int gridBottom = pageHeight - viewBottom - metrics.buttonHintsHeight;
  const int gridHeight = std::max(TARGET_CELL_HEIGHT, gridBottom - g.gridTop);

  g.cols = std::max(2, gridWidth / TARGET_CELL_WIDTH);
  g.cellWidth = gridWidth / g.cols;
  g.rows = std::max(1, gridHeight / TARGET_CELL_HEIGHT);
  g.cellHeight = gridHeight / g.rows;
  g.itemsPerPage = g.cols * g.rows;

  // Reserves room for drawCell()'s caption below the cover -- titles are always drawn, so this is
  // unconditional. Keeping it a fixed part of the formula (rather than a toggle) matters for the
  // pre-render in LibraryIndexBuilder: one size, always, means the rebuild and the grid can never
  // disagree on what to look up.
  const int titleAreaHeight = renderer.getLineHeight(SMALL_FONT_ID) + CARD_PADDING;
  g.coverHeight = std::max(20, g.cellHeight - CARD_PADDING * 2 - titleAreaHeight);
  g.coverWidth = g.cellWidth - CARD_PADDING * 2;

  return g;
}

}  // namespace CoverGridGeometry
