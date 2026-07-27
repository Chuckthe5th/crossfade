#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LibraryIndex.h"
#include "util/LibraryScanner.h"

// Incremental, resumable, cancellable delta rebuild of a LibraryIndex. Driven one step (one book)
// at a time -- see LibraryIndexRebuildActivity -- so a long cold build stays responsive to input
// (Back cancels mid-build) and never blocks an Activity's render/input loop with a single long
// call. Not itself UI-aware: the caller owns the popup, progress throttling, and Back handling.
//
// The delta walk (begin()) is cheap: LibraryScanner's directory walk plus a fingerprint compare,
// no OPF/XTC parsing. Only step() -- called once per pending (new or changed) book -- does real
// parsing work, so the visible cost of a rebuild scales with what actually changed, not with
// library size.
//
// Optionally also pre-renders cover thumbnails at a caller-given size (see begin()), so
// CoverGridBrowserActivity never has to generate one at page-turn time. This has its own delta:
// pending books get their cover generated for free as part of the same BookMetadataResolver::
// resolve() call step() already makes for their metadata; carriedOver books (metadata unchanged,
// never touched by step()) get a cheap per-book "does a thumbnail already exist at this size"
// check in begin(), and only the ones missing one are queued for stepCover(). A book that already
// has its cover cached -- the common case after the first rebuild following a cover-size change
// -- costs one Storage.exists() stat, not a re-parse.
class LibraryIndexBuilder {
 public:
  explicit LibraryIndexBuilder(std::string indexPath) : indexPath(std::move(indexPath)) {}

  // Scans the library and computes the delta against the on-disk index (if any): unchanged
  // entries (matching path + size + fatDateTime) are carried over verbatim; new or changed paths
  // become pending. Call once before stepping.
  //
  // Pass coverWidth/coverHeight > 0 to also pre-render cover thumbnails at that exact size (see
  // class comment) -- callers should compute these live via CoverGridGeometry::compute(), never a
  // hardcoded/assumed size, so pre-rendered thumbnails match what the grid will actually look up
  // on this device. Pass 0, 0 (the default) to skip cover work entirely, matching the previous
  // metadata-only behavior.
  void begin(int coverWidth = 0, int coverHeight = 0);

  bool hasWork() const { return nextPendingIndex < pending.size(); }
  // Scoped to pending (parse-needed) work only -- carried-over entries cost no visible time, so
  // they're not part of the progress fraction a caller would show.
  int totalCount() const { return static_cast<int>(pending.size()); }
  int resolvedCount() const { return static_cast<int>(nextPendingIndex); }

  // Resolves exactly one pending book (title/author/series via BookMetadataResolver, plus its
  // cover if begin() was given a non-zero size). Returns true if more work remains, false once
  // every pending book has been resolved. A no-op returning false if called with hasWork() already
  // false.
  bool step();

  // The cover-backfill phase: only carriedOver entries found missing a thumbnail in begin() --
  // see class comment. Meaningless (always empty) if begin() was called with coverWidth/
  // coverHeight == 0. Intended to run after step()'s pending-book phase is exhausted.
  bool hasCoverWork() const { return nextCoverBackfillIndex < coverBackfill.size(); }
  int totalCoverCount() const { return static_cast<int>(coverBackfill.size()); }
  int resolvedCoverCount() const { return static_cast<int>(nextCoverBackfillIndex); }

  // Generates exactly one missing cover for a carriedOver entry queued by begin(). Returns true if
  // more cover work remains, false once every queued cover has been generated. Does not touch
  // title/author/series -- carriedOver's are already correct and untouched by this call. A no-op
  // returning false if called with hasCoverWork() already false.
  bool stepCover();

  // True if begin() found nothing to change and no cover backfill is needed: the on-disk index
  // already matches the scanned library and every book already has a cover cached at the
  // requested size (or the library is unreadable/empty and no index exists to build). Callers can
  // skip showing a build UI entirely in this case -- there is nothing to wait for.
  bool upToDate() const { return !dirty && !hasCoverWork(); }

  // Persists carriedOver + all step()-resolved entries atomically (LibraryIndex::save). Call once
  // hasWork() is false -- independent of hasCoverWork(), since cover backfill never changes index
  // content, only thumbnail files on disk. A no-op (returns true without writing) when dirty is
  // false -- the on-disk index already matches, so there is nothing to persist.
  bool commit();

  // Discards all in-progress work without touching the on-disk index at all -- an existing index,
  // if any, is left exactly as it was. The next begin() recomputes the delta from scratch.
  void cancel();

  // For a completion screen: total books in the index after commit() (carried-over + resolved),
  // and wall time since begin() was called.
  int totalEntries() const { return static_cast<int>(carriedOver.size() + resolved.size()); }
  uint32_t elapsedMs() const;

 private:
  std::string indexPath;
  std::vector<LibraryIndex::Entry> carriedOver;  // unchanged entries, copied verbatim from the old index
  std::vector<LibraryScanner::Entry> pending;    // paths needing (re)resolve, fingerprint carried through
  std::vector<LibraryIndex::Entry> resolved;     // step() results, appended in pending order
  size_t nextPendingIndex = 0;
  // Indices into carriedOver found missing a cover at coverWidth/coverHeight by begin() -- empty
  // whenever begin() was called with coverWidth/coverHeight == 0.
  std::vector<size_t> coverBackfill;
  size_t nextCoverBackfillIndex = 0;
  int coverWidth = 0;
  int coverHeight = 0;
  uint32_t startMs = 0;
  // Set by begin() when the on-disk index doesn't already match the scanned library exactly --
  // either something needs (re)parsing (pending non-empty) or an old entry was dropped (a
  // previously-indexed book was deleted/moved). Distinct from pending.empty(): a
  // deletion-only delta has no parse work but still needs a commit() to drop the stale entry.
  bool dirty = false;
};
