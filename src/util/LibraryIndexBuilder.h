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
class LibraryIndexBuilder {
 public:
  explicit LibraryIndexBuilder(std::string indexPath) : indexPath(std::move(indexPath)) {}

  // Scans the library and computes the delta against the on-disk index (if any): unchanged
  // entries (matching path + size + fatDateTime) are carried over verbatim; new or changed paths
  // become pending. Call once before stepping.
  void begin();

  bool hasWork() const { return nextPendingIndex < pending.size(); }
  // Scoped to pending (parse-needed) work only -- carried-over entries cost no visible time, so
  // they're not part of the progress fraction a caller would show.
  int totalCount() const { return static_cast<int>(pending.size()); }
  int resolvedCount() const { return static_cast<int>(nextPendingIndex); }

  // Resolves exactly one pending book (title/author/series via BookMetadataResolver, no cover
  // work). Returns true if more work remains, false once every pending book has been resolved.
  // A no-op returning false if called with hasWork() already false.
  bool step();

  // True if begin() found nothing to change: the on-disk index already matches the scanned
  // library (or the library is unreadable/empty and no index exists to build). Callers can skip
  // showing a build UI entirely in this case -- there is nothing to wait for.
  bool upToDate() const { return !dirty; }

  // Persists carriedOver + all step()-resolved entries atomically (LibraryIndex::save). Call once
  // hasWork() is false. A no-op (returns true without writing) when upToDate() -- the on-disk
  // index already matches, so there is nothing to persist.
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
  uint32_t startMs = 0;
  // Set by begin() when the on-disk index doesn't already match the scanned library exactly --
  // either something needs (re)parsing (pending non-empty) or an old entry was dropped (a
  // previously-indexed book was deleted/moved). Distinct from pending.empty(): a
  // deletion-only delta has no parse work but still needs a commit() to drop the stale entry.
  bool dirty = false;
};
