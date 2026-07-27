#include "CoverGridGeometry.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"

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

  // No caption row -- titles are never drawn over a real cover (only on the no-cover fallback
  // card, inside its own box -- see CoverGridBrowserActivity::drawCell), so the cover fills the
  // full cell height.
  g.coverHeight = std::max(20, g.cellHeight - CARD_PADDING * 2);
  // SERIES_STRIP_WIDTH is reserved on every cell regardless of whether its entry is a series, so
  // there's still exactly one cover size for the pre-render and the grid to agree on -- just
  // narrower than the full cell width by that fixed strip.
  g.coverWidth = std::max(20, g.cellWidth - CARD_PADDING * 2 - SERIES_STRIP_WIDTH);

  return g;
}

}  // namespace CoverGridGeometry
