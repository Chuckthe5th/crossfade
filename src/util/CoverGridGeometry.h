#pragma once

class GfxRenderer;

// The exact cell/cover-size formula CoverGridBrowserActivity lays its grid out with, factored out
// so LibraryIndexRebuildActivity's cover pre-render (see LibraryIndexBuilder) can generate
// thumbnails at precisely the size the grid will look them up at -- one formula, read live off
// the running renderer/theme/settings each time, so there is no second copy that could drift and
// cause a silent cache-key mismatch (and therefore a regenerate-on-first-browse) on either device.
namespace CoverGridGeometry {

// Matches drawCell()'s inner padding between a cell's border and its cover -- shared here (rather
// than a second copy of the literal) since it's part of the same coverWidth/coverHeight derivation
// below.
constexpr int CARD_PADDING = 6;

// Fixed width reserved on every cell's right edge for the series-stack indicator (see
// CoverGridBrowserActivity::drawStackOverlay) -- reserved unconditionally, not just on series
// entries, so every cover is the same size whether or not its entry is a series. Sized to the
// indicator's own 3-line geometry (2px stroke, 2px gaps, 2px right inset -> ~12px footprint) plus
// the 2px the selected-cell ring overdraws into the strip's left edge; tightened deliberately (no
// halo, minimal insets) since the strip's background is always blank -- unlike the old on-cover
// overlay, it never has to survive being drawn over unpredictable cover art.
constexpr int SERIES_STRIP_WIDTH = 14;

struct Geometry {
  int cols = 0;
  int rows = 0;
  int itemsPerPage = 0;
  int cellWidth = 0;
  int cellHeight = 0;
  // Exact pixel box thumbnails are generated at and drawn into -- see CoverGridBrowserActivity's
  // coverWidth/coverHeight members for the same guarantee this mirrors.
  int coverWidth = 0;
  int coverHeight = 0;
  int gridLeft = 0;
  int gridTop = 0;
};

Geometry compute(GfxRenderer& renderer);

}  // namespace CoverGridGeometry
