#pragma once

#include <string>
#include <vector>

#include "util/LibraryIndex.h"

// Shared grouping/data-source layer for the Covers grid and Titles list: the seam where the two
// otherwise-identically-shaped views (they differ only in cell layout, 2D-vs-1D navigation, and
// draw style -- see CoverGridBrowserActivity/LibraryListActivity) diverge on "where entries come
// from," without that divergence leaking into either activity's rendering or pagination code.
//
// Ungrouped (SETTINGS.groupBySeries off, or no valid index yet): loadLibraryEntries() returns one
// standalone Entry per book in filename order, title/author left empty for lazy per-page
// resolution via resolvePage() -- functionally identical to this codebase's pre-grouping
// behavior. Exception: a caller that passes preferIndexWhenFlat=true and already has a valid
// on-disk index gets that index's entries (title/author pre-filled, see flatten()) instead of a
// fresh directory walk -- for a caller that just ran a delta rebuild (e.g. CoverGridBrowserActivity,
// which now always does so before loading Covers -- see HomeActivity::onFileBrowserOpen()), the
// index is already guaranteed current, so re-walking the SD card a second time in the same entry
// would be pure waste.
//
// Grouped: loadLibraryEntries() reads the persisted LibraryIndex and collapses it via collapse()
// into a single list where every entry (standalone or series) already has its title/author known
// -- no per-page text resolution needed, only covers (see resolvePage()). Falls back to the
// ungrouped path automatically if no valid index exists yet (e.g. groupBySeries was just turned
// on and nothing has built it, or a build was cancelled before committing) -- callers never need
// to know which case they're in.
namespace LibraryGrouping {

struct Entry {
  bool isSeries = false;
  // Standalone: the book itself. Series: the lowest-seriesIndex member's path -- used only to
  // anchor this entry's sort position and as the source path for cover/text resolution.
  std::string path;
  std::string title;   // standalone: book title. Series: series display name.
  std::string author;  // standalone: book author. Series: lowest-index member's author.
  // Only for series entries: each member as its own standalone-shaped Entry (isSeries=false),
  // sorted by seriesIndex ascending (a member with no index sorts last). Ready to render directly
  // once drilled into -- in grouped mode these are already fully resolved (title/author known),
  // never re-resolved on drill-down.
  std::vector<Entry> members;
  // Cover resolution result -- always lazy/per-page regardless of grouping mode, since a cover's
  // exact pixel size is geometry-dependent and isn't cached in the index (see LibraryIndex.h).
  // Empty/false until a Covers-view caller resolves it via resolvePage().
  std::string coverThumbPath;
  bool hasCover = false;
};

// Normalizes a series name for the grouping-key comparison only (trim, collapse internal
// whitespace, ASCII case-fold) -- never used for display, which always shows the original,
// unnormalized string from the anchor member.
std::string normalizeSeriesKey(const std::string& series);

// Collapses a delta-built LibraryIndex into a single filename-ordered list: books sharing a
// normalized series name collapse into one series Entry, but only when the group has >= 2
// members (a lone book "in a series of one" stays standalone -- an extra tap for no benefit).
// Within a group, members sort by seriesIndex ascending; a member with no index (-1 sentinel)
// sorts after every indexed member, ties broken by filename. A group's position in the OUTER list
// is the filename of its lowest-seriesIndex member -- the same member whose title/author/path
// becomes the group's own display title/author/cover source.
std::vector<Entry> collapse(const std::vector<LibraryIndex::Entry>& indexEntries);

// Loads the top-level entry list for the flattened whole-library view (never used for the
// RecentBooks source, which has its own semantics and never groups -- see
// CoverGridBrowserActivity). Reads+collapses the persisted index when groupBySeries is true AND a
// valid index exists; otherwise scans via LibraryScanner and returns one unresolved standalone
// Entry per book, filename-ordered -- identical to this codebase's pre-grouping behavior.
//
// preferIndexWhenFlat: when groupBySeries is false, try the persisted index first anyway (see
// flatten()) and only fall back to a live scan if none exists -- for a caller that just rebuilt
// the index itself and knows it's current. Ignored when groupBySeries is true (already reads the
// index in that case). Defaults to false so existing ungrouped callers keep their always-live-scan
// guarantee (LibraryListActivity's Titles view, which never rebuilds on its own and must reflect
// the SD card exactly, not a possibly-stale index from some earlier Covers visit).
std::vector<Entry> loadLibraryEntries(bool groupBySeries, bool preferIndexWhenFlat = false);

// Resolves entries[pageStart, pageEnd) in place: title/author for any entry that doesn't have
// them yet (a no-op for an already-resolved entry, whether that's because it came from the index
// or was resolved on a prior call -- lets grouped and ungrouped callers share one call site
// without checking which mode they're in). Cover resolution (generating the thumbnail if it's
// missing) only happens when coverWidth/coverHeight > 0 and the entry doesn't already have one --
// the one piece of lazy work grouped mode still needs, since covers aren't cached in the index.
// For a series entry, both title/author and cover resolve against members.front()'s path (the
// anchor). Returns true if any cover had to be generated (drives a loading popup), matching
// BookMetadataResolver::Result::coverGenerated's existing contract.
bool resolvePage(std::vector<Entry>& entries, int pageStart, int pageEnd, int coverWidth = 0, int coverHeight = 0);

}  // namespace LibraryGrouping
