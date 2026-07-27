#include "LibraryIndexBuilder.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>
#include <iterator>

#include "util/BookMetadataResolver.h"

namespace {
// Cheap "does this book already have a thumbnail at this size" check for a carriedOver entry --
// no load()/parse, since Epub/Xtc::getThumbBmpPath() only depends on the constructor-computed
// cache path. Mirrors the per-extension dispatch in BookMetadataResolver::resolve() and
// clearBookCache(); TXT/MD have no cover concept and are never "missing" one.
bool hasCachedCoverAtSize(const std::string& path, const int coverWidth, const int coverHeight) {
  if (FsHelpers::hasEpubExtension(path)) {
    const Epub epub(path, "/.crosspoint");
    return Storage.exists(epub.getThumbBmpPath(coverWidth, coverHeight).c_str());
  }
  if (FsHelpers::hasXtcExtension(path)) {
    const Xtc xtc(path, "/.crosspoint");
    return Storage.exists(xtc.getThumbBmpPath(coverWidth, coverHeight).c_str());
  }
  return true;
}
}  // namespace

void LibraryIndexBuilder::begin(const int coverWidth, const int coverHeight) {
  carriedOver.clear();
  pending.clear();
  resolved.clear();
  coverBackfill.clear();
  nextPendingIndex = 0;
  nextCoverBackfillIndex = 0;
  this->coverWidth = coverWidth;
  this->coverHeight = coverHeight;
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
      // Unchanged metadata never goes through step(), so a missing-cover check has to happen
      // here instead -- cheap (a stat, no parse) for the common case where it's already cached.
      if (coverWidth > 0 && coverHeight > 0 && !hasCachedCoverAtSize(scannedEntry.path, coverWidth, coverHeight)) {
        coverBackfill.push_back(carriedOver.size() - 1);
      }
    } else {
      pending.push_back(scannedEntry);
    }
  }

  // Something changed if there's parse work to do, or an old entry didn't survive (its book was
  // deleted/moved and is no longer in the scan) -- both need a commit() even though only the
  // first costs visible time. !hadOldIndex covers the very first build: carriedOver/oldIndex are
  // both empty then, so the size comparison alone wouldn't otherwise flag "needs a write."
  dirty = !pending.empty() || !hadOldIndex || carriedOver.size() != oldIndex.entries.size();

  LOG_DBG("LIBIDX", "begin(): %zu carried over, %zu pending, %zu cover backfill, dirty=%d", carriedOver.size(),
          pending.size(), coverBackfill.size(), dirty);
}

bool LibraryIndexBuilder::step() {
  if (!hasWork()) {
    return false;
  }

  const LibraryScanner::Entry& scannedEntry = pending[nextPendingIndex];
  // Folds cover generation into the same resolve() call already paying to open/parse this book's
  // metadata -- coverWidth/coverHeight are 0 (BookMetadataResolver::resolve()'s default, meaning
  // "skip cover work") unless begin() was given a real size.
  const BookMetadataResolver::Result result = BookMetadataResolver::resolve(scannedEntry.path, coverWidth, coverHeight);

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

bool LibraryIndexBuilder::stepCover() {
  if (!hasCoverWork()) {
    return false;
  }

  const LibraryIndex::Entry& entry = carriedOver[coverBackfill[nextCoverBackfillIndex]];
  // Discards title/author/series -- carriedOver's are already correct and untouched by this call;
  // it exists only to generate the thumbnail found missing in begin(). Reaching the cover still
  // requires opening/parsing the book (the cover href comes from the same metadata), so this pays
  // a real parse cost, but only for the entries begin() actually flagged as missing a cover.
  BookMetadataResolver::resolve(entry.path, coverWidth, coverHeight);

  nextCoverBackfillIndex++;
  return hasCoverWork();
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
  coverBackfill.clear();
  nextPendingIndex = 0;
  nextCoverBackfillIndex = 0;
  dirty = false;
  // The on-disk index (if any) is never touched here -- only commit() writes. Any covers already
  // generated by a prior stepCover() call this run are real files on disk and are left in place --
  // there's nothing to roll back there either.
}
