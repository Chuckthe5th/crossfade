#pragma once

#include <cstdint>
#include <string>
#include <vector>

// On-disk cache of resolved book metadata for the whole library (title/author/series/seriesIndex
// per book), keyed by path with a cheap (size, mtime) fingerprint so a delta rebuild (see
// LibraryIndexBuilder) only re-parses new or changed books instead of the whole library every
// time. Read by Covers/Titles instead of re-parsing each book's OPF/XTC header on every page turn.
//
// Deliberately does NOT cache a cover thumbnail path: cover thumbnails are already cached
// separately, keyed by their own exact (width, height) cell box (see BookMetadataResolver), which
// can differ by orientation or theme -- mixing a geometry-dependent path into this
// geometry-independent metadata cache would mean either the wrong size or a stale/nonexistent path
// after a layout change. Covers/Titles resolve cover thumbnails exactly as they do today.
namespace LibraryIndex {

// Single well-known location, shared by LibraryIndexBuilder, LibraryIndexRebuildActivity, and
// LibraryGrouping so none of them can drift onto a different path.
constexpr const char* PATH = "/.crosspoint/library_index.bin";

struct Entry {
  std::string path;
  // Fingerprint captured at the time this entry was last resolved -- see LibraryScanner::Entry
  // for what these mean (size in bytes; fatDateTime packed FAT date/time, 0 if never set).
  uint32_t size = 0;
  uint32_t fatDateTime = 0;
  std::string title;
  std::string author;
  std::string series;         // empty if the book has no series metadata (EPUB only)
  float seriesIndex = -1.0f;  // -1 = no index given
};

struct Index {
  std::vector<Entry> entries;
  // Diagnostics from the build that produced this index, persisted so the last build's cost is
  // inspectable without a serial monitor attached.
  uint32_t lastBuildDurationMs = 0;
};

// Loads the index from `path`. Returns false on a missing file, a schema version mismatch, or any
// read/size-sanity failure -- callers treat all of these identically: "no usable index," which
// triggers a full rebuild rather than a partial or best-effort load.
bool load(const std::string& path, Index& outIndex);

// Atomically replaces `path` with `index`'s contents: writes to `path + ".tmp"`, closes it, then
// removes any existing file at `path` and renames the temp file into place. Power loss mid-write
// leaves either the untouched old index or an orphaned .tmp -- never a half-written file at `path`.
bool save(const std::string& path, const Index& index);

}  // namespace LibraryIndex
