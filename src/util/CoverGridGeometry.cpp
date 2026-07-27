#include "CoverGridGeometry.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "CrossPointSettings.h"
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

  // With titles off, the caption row's space folds back into the cover box itself -- taller
  // covers, not blank space -- which also means a different cache box size (see drawCell()'s
  // Epub::generateThumbBmp(width, height) call): old with-titles thumbnails are simply never
  // looked up at the new size, not explicitly invalidated.
  const int titleAreaHeight = SETTINGS.coversShowTitles ? renderer.getLineHeight(SMALL_FONT_ID) + CARD_PADDING : 0;
  g.coverHeight = std::max(20, g.cellHeight - CARD_PADDING * 2 - titleAreaHeight);
  g.coverWidth = g.cellWidth - CARD_PADDING * 2;

  return g;
}

}  // namespace CoverGridGeometry
