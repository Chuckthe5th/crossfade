#include "LibraryGrouping.h"

#include <FsHelpers.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <numeric>

#include "util/BookMetadataResolver.h"
#include "util/LibraryScanner.h"

namespace {

// True if seriesIndex `a` sorts before `b`: ascending by value, with the "no index" sentinel
// (< 0) always sorting last regardless of magnitude.
bool seriesIndexLess(const float a, const float b) {
  if (a < 0 && b < 0) {
    return false;  // both unindexed: tie, let stable_sort keep the incoming (filename) order
  }
  if (a < 0) {
    return false;  // a unindexed, b indexed -> b sorts first
  }
  if (b < 0) {
    return true;  // a indexed, b unindexed -> a sorts first
  }
  return a < b;
}

// Maps a persisted index straight into the flat (ungrouped) Entry shape, no series collapsing --
// title/author come along for free since the index already resolved them. Used by
// loadLibraryEntries()'s preferIndexWhenFlat path to avoid a second LibraryScanner walk when the
// caller already knows the index is current.
std::vector<LibraryGrouping::Entry> flatten(const std::vector<LibraryIndex::Entry>& indexEntries) {
  std::vector<LibraryGrouping::Entry> result;
  result.reserve(indexEntries.size());
  for (const auto& ie : indexEntries) {
    LibraryGrouping::Entry e;
    e.path = ie.path;
    e.title = ie.title;
    e.author = ie.author;
    result.push_back(std::move(e));
  }
  LibraryScanner::sortByFilename(result);
  return result;
}

}  // namespace

namespace LibraryGrouping {

std::string normalizeSeriesKey(const std::string& series) {
  const size_t start = series.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const size_t end = series.find_last_not_of(" \t\r\n");

  std::string result;
  result.reserve(end - start + 1);
  bool lastWasSpace = false;
  for (size_t i = start; i <= end; i++) {
    const unsigned char c = static_cast<unsigned char>(series[i]);
    if (std::isspace(c)) {
      if (!lastWasSpace) {
        result += ' ';
      }
      lastWasSpace = true;
    } else {
      result += static_cast<char>(std::tolower(c));
      lastWasSpace = false;
    }
  }
  return result;
}

std::vector<Entry> collapse(const std::vector<LibraryIndex::Entry>& indexEntries) {
  const size_t n = indexEntries.size();

  std::vector<std::string> keys(n);
  std::vector<std::string> names(n);
  for (size_t i = 0; i < n; i++) {
    keys[i] = normalizeSeriesKey(indexEntries[i].series);
    names[i] = LibraryScanner::basenameOf(indexEntries[i].path);
  }

  // Sort indices by (normalized series key, filename) so same-series entries become one
  // contiguous run below, already filename-ordered within it as a stable tiebreak for the
  // seriesIndex sort that follows -- O(n log n), avoiding an O(n * distinct series) bucket scan.
  std::vector<int> byKey(n);
  std::iota(byKey.begin(), byKey.end(), 0);
  std::sort(byKey.begin(), byKey.end(), [&](const int a, const int b) {
    if (keys[a] != keys[b]) {
      return keys[a] < keys[b];
    }
    return FsHelpers::naturalLess(names[a], names[b]);
  });

  std::vector<bool> grouped(n, false);
  std::vector<Entry> seriesEntries;
  size_t i = 0;
  while (i < n) {
    size_t j = i;
    while (j < n && keys[byKey[j]] == keys[byKey[i]]) {
      j++;
    }
    // [i, j) is one contiguous run sharing this normalized key (empty key = no series at all,
    // never grouped regardless of run length).
    if (!keys[byKey[i]].empty() && (j - i) >= 2) {
      std::vector<int> members(byKey.begin() + static_cast<long>(i), byKey.begin() + static_cast<long>(j));
      std::stable_sort(members.begin(), members.end(), [&](const int a, const int b) {
        return seriesIndexLess(indexEntries[a].seriesIndex, indexEntries[b].seriesIndex);
      });

      Entry group;
      group.isSeries = true;
      const int anchor = members.front();
      group.path = indexEntries[anchor].path;
      group.title = indexEntries[anchor].series;
      group.author = indexEntries[anchor].author;
      group.members.reserve(members.size());
      for (const int m : members) {
        Entry member;
        member.path = indexEntries[m].path;
        member.title = indexEntries[m].title;
        member.author = indexEntries[m].author;
        group.members.push_back(std::move(member));
        grouped[static_cast<size_t>(m)] = true;
      }
      seriesEntries.push_back(std::move(group));
    }
    i = j;
  }

  std::vector<Entry> result;
  result.reserve(n);
  for (size_t idx = 0; idx < n; idx++) {
    if (grouped[idx]) {
      continue;
    }
    Entry e;
    e.path = indexEntries[idx].path;
    e.title = indexEntries[idx].title;
    e.author = indexEntries[idx].author;
    result.push_back(std::move(e));
  }
  std::move(seriesEntries.begin(), seriesEntries.end(), std::back_inserter(result));

  LibraryScanner::sortByFilename(result);
  return result;
}

std::vector<Entry> loadLibraryEntries(const bool groupBySeries, const bool preferIndexWhenFlat) {
  if (groupBySeries || preferIndexWhenFlat) {
    LibraryIndex::Index index;
    if (LibraryIndex::load(LibraryIndex::PATH, index)) {
      return groupBySeries ? collapse(index.entries) : flatten(index.entries);
    }
    // No valid index yet (never built, or a build was cancelled before committing) -- fall back
    // to the ungrouped scan below rather than showing an empty screen.
  }

  const auto scanned = LibraryScanner::scanAllBooks("/", LibraryScanner::MAX_LIBRARY_BOOKS);
  std::vector<Entry> entries;
  entries.reserve(scanned.size());
  std::transform(scanned.begin(), scanned.end(), std::back_inserter(entries), [](const LibraryScanner::Entry& s) {
    Entry e;
    e.path = s.path;
    return e;
  });
  LibraryScanner::sortByFilename(entries);
  return entries;
}

bool resolvePage(std::vector<Entry>& entries, const int pageStart, const int pageEnd, const int coverWidth,
                 const int coverHeight) {
  bool anyGenerated = false;
  const bool wantCover = coverWidth > 0 && coverHeight > 0;
  for (int i = pageStart; i < pageEnd && i < static_cast<int>(entries.size()); i++) {
    Entry& e = entries[i];
    const bool needText = e.title.empty();
    const bool needCover = wantCover && !e.hasCover;
    if (!needText && !needCover) {
      continue;
    }

    const std::string& resolvePath = (e.isSeries && !e.members.empty()) ? e.members.front().path : e.path;
    const BookMetadataResolver::Result result = BookMetadataResolver::resolve(resolvePath, coverWidth, coverHeight);
    if (needText) {
      e.title = result.title;
      e.author = result.author;
    }
    if (needCover) {
      e.coverThumbPath = result.coverThumbPath;
      e.hasCover = result.hasCover;
      anyGenerated = anyGenerated || result.coverGenerated;
    }
  }
  return anyGenerated;
}

}  // namespace LibraryGrouping
