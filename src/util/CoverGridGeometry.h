#pragma once

class GfxRenderer;

// The exact cell/cover-size formula CoverGridBrowserActivity lays its grid out with, factored out
// so LibraryIndexRebuildActivity's cover pre-render (see LibraryIndexBuilder) can generate
// thumbnails at precisely the size the grid will look them up at -- one formula, read live off
// the running renderer/theme/settings each time, so there is no second copy that could drift and
// cause a silent cache-key mismatch (and therefore a regenerate-on-first-browse) on either device.
namespace CoverGridGeometry {

// Matches drawCell()'s inner padding between a cell's border and its cover/caption -- shared here
// (rather than a second copy of the literal) since it's part of the same coverWidth/coverHeight
// derivation below.
constexpr int CARD_PADDING = 6;

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
