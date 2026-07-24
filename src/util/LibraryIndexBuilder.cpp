#include "LibraryIndexBuilder.h"

#include <Logging.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "util/BookMetadataResolver.h"

void LibraryIndexBuilder::begin() {
  carriedOver.clear();
  pending.clear();
  resolved.clear();
  nextPendingIndex = 0;
  dirty = false;
  startMs = millis();

  const auto scanned = LibraryScanner::scanAllBooks("/", LibraryScanner::MAX_LIBRARY_BOOKS,
                                                    /*collectFingerprint=*/true);

  LibraryIndex::Index oldIndex;
  const bool hadOldIndex = LibraryIndex::load(indexPath, oldIndex);

  // Sorted-by-path index over the old entries for a binary-search lookup, mirroring the
  // sorted+binary-search convention ContentOpfParser/BookMetadataCache already use for
  // idref/href lookups at a similar scale, rather than pulling in a hash map for the first time.
  std::vector<const LibraryIndex::Entry*> oldByPath;
  oldByPath.reserve(oldIndex.entries.size());
  std::transform(oldIndex.entries.begin(), oldIndex.entries.end(), std::back_inserter(oldByPath),
                 [](const LibraryIndex::Entry& entry) { return &entry; });
  std::sort(oldByPath.begin(), oldByPath.end(),
            [](const LibraryIndex::Entry* a, const LibraryIndex::Entry* b) { return a->path < b->path; });

  for (const auto& scannedEntry : scanned) {
    const auto it =
        std::lower_bound(oldByPath.begin(), oldByPath.end(), scannedEntry.path,
                         [](const LibraryIndex::Entry* e, const std::string& path) { return e->path < path; });
    if (it != oldByPath.end() && (*it)->path == scannedEntry.path && (*it)->size == scannedEntry.size &&
        (*it)->fatDateTime == scannedEntry.fatDateTime) {
      carriedOver.push_back(**it);
    } else {
      pending.push_back(scannedEntry);
    }
  }

  // Something changed if there's parse work to do, or an old entry didn't survive (its book was
  // deleted/moved and is no longer in the scan) -- both need a commit() even though only the
  // first costs visible time. !hadOldIndex covers the very first build: carriedOver/oldIndex are
  // both empty then, so the size comparison alone wouldn't otherwise flag "needs a write."
  dirty = !pending.empty() || !hadOldIndex || carriedOver.size() != oldIndex.entries.size();

  LOG_DBG("LIBIDX", "begin(): %zu carried over, %zu pending, dirty=%d", carriedOver.size(), pending.size(), dirty);
}

bool LibraryIndexBuilder::step() {
  if (!hasWork()) {
    return false;
  }

  const LibraryScanner::Entry& scannedEntry = pending[nextPendingIndex];
  const BookMetadataResolver::Result result = BookMetadataResolver::resolve(scannedEntry.path);

  LibraryIndex::Entry entry;
  entry.path = scannedEntry.path;
  entry.size = scannedEntry.size;
  entry.fatDateTime = scannedEntry.fatDateTime;
  entry.title = result.title;
  entry.author = result.author;
  entry.series = result.series;
  entry.seriesIndex = result.seriesIndex;
  resolved.push_back(std::move(entry));

  nextPendingIndex++;
  return hasWork();
}

bool LibraryIndexBuilder::commit() {
  if (!dirty) {
    return true;  // Already matches what's on disk -- nothing to write.
  }

  LibraryIndex::Index index;
  index.entries.reserve(carriedOver.size() + resolved.size());
  index.entries.insert(index.entries.end(), carriedOver.begin(), carriedOver.end());
  index.entries.insert(index.entries.end(), resolved.begin(), resolved.end());
  index.lastBuildDurationMs = millis() - startMs;

  const bool ok = LibraryIndex::save(indexPath, index);
  LOG_DBG("LIBIDX", "commit(): %zu entries, %lums, ok=%d", index.entries.size(),
          static_cast<unsigned long>(index.lastBuildDurationMs), ok);
  return ok;
}

uint32_t LibraryIndexBuilder::elapsedMs() const { return millis() - startMs; }

void LibraryIndexBuilder::cancel() {
  carriedOver.clear();
  pending.clear();
  resolved.clear();
  nextPendingIndex = 0;
  dirty = false;
  // The on-disk index (if any) is never touched here -- only commit() writes.
}

void checkLibraryIndexStaleness() {
  if (!SETTINGS.groupBySeries) {
    return;
  }
  LibraryIndexBuilder builder(LibraryIndex::PATH);
  builder.begin();
  if (!builder.upToDate()) {
    LOG_INF("LIBIDX", "Library index is stale (%d book(s) need updating); resolves on next Browse Books entry",
            builder.totalCount());
  }
}
