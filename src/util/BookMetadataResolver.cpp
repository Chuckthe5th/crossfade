#include "BookMetadataResolver.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

namespace {

std::string filenameWithoutExtension(const std::string& path) {
  size_t start = path.find_last_of('/');
  start = (start == std::string::npos) ? 0 : start + 1;
  size_t end = path.find_last_of('.');
  if (end == std::string::npos || end < start) {
    end = path.size();
  }
  return path.substr(start, end - start);
}

}  // namespace

namespace BookMetadataResolver {

Result resolve(const std::string& path, const int coverWidth, const int coverHeight) {
  const uint32_t startMs = millis();
  const bool wantCover = coverWidth > 0 && coverHeight > 0;

  Result result;

  if (FsHelpers::hasEpubExtension(path)) {
    const uint32_t metaStartMs = millis();
    Epub epub(path, "/.crosspoint");
    bool haveMetadata = epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true);
    const bool wasCachedBook = haveMetadata;
    if (!haveMetadata) {
      // Never opened before: no book.bin yet. A full build (spine + TOC) would be too expensive to
      // pay per book just to resolve a title/author, so fall back to a lightweight OPF-only parse.
      epub.setupCacheDir();
      haveMetadata = epub.loadMetadataOnly();
    }
    const uint32_t metaMs = millis() - metaStartMs;
    if (haveMetadata) {
      result.title = epub.getTitle();
      result.author = epub.getAuthor();
      result.series = epub.getSeries();
      result.seriesIndex = epub.getSeriesIndex();
      if (wantCover) {
        const std::string thumbPath = epub.getThumbBmpPath(coverWidth, coverHeight);
        const bool thumbCached = Storage.exists(thumbPath.c_str());
        if (!thumbCached) {
          epub.generateThumbBmp(coverWidth, coverHeight);
          result.coverGenerated = true;
        }
        if (Storage.exists(thumbPath.c_str())) {
          result.coverThumbPath = thumbPath;
          result.hasCover = true;
        }
      }
      LOG_DBG("BMR-PERF", "resolve EPUB \"%s\": bookBinCached=%d metadataResolveMs=%lu coverRequested=%d totalMs=%lu",
              path.c_str(), wasCachedBook, static_cast<unsigned long>(metaMs), wantCover,
              static_cast<unsigned long>(millis() - startMs));
    } else {
      LOG_DBG("BMR-PERF", "resolve EPUB \"%s\": metadata FAILED, metadataResolveMs=%lu totalMs=%lu", path.c_str(),
              static_cast<unsigned long>(metaMs), static_cast<unsigned long>(millis() - startMs));
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    const uint32_t loadStartMs = millis();
    Xtc xtc(path, "/.crosspoint");
    const bool loaded = xtc.load();
    const uint32_t loadMs = millis() - loadStartMs;
    if (loaded) {
      result.title = xtc.getTitle();
      result.author = xtc.getAuthor();
      if (wantCover) {
        const std::string thumbPath = xtc.getThumbBmpPath(coverWidth, coverHeight);
        const bool thumbCached = Storage.exists(thumbPath.c_str());
        if (!thumbCached) {
          xtc.setupCacheDir();
          xtc.generateThumbBmp(coverWidth, coverHeight);
          result.coverGenerated = true;
        }
        if (Storage.exists(thumbPath.c_str())) {
          result.coverThumbPath = thumbPath;
          result.hasCover = true;
        }
      }
      LOG_DBG("BMR-PERF", "resolve XTC \"%s\": headerLoadMs=%lu coverRequested=%d totalMs=%lu", path.c_str(),
              static_cast<unsigned long>(loadMs), wantCover, static_cast<unsigned long>(millis() - startMs));
    }
  }
  // TXT/MD have no cover concept and no embedded metadata -- filename fallback below, no file open.

  if (result.title.empty()) {
    result.title = filenameWithoutExtension(path);
  }
  return result;
}

}  // namespace BookMetadataResolver
