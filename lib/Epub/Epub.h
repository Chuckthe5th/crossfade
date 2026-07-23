#pragma once

#include <Print.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;

class Epub {
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Uniq cache key based on filepath
  std::string cachePath;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;

  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool writeSpineEntries = true);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  void discoverCssFilesFromZip();
  void parseCssFiles() const;
  // Shared by both generateThumbBmp() overloads: decodes the cover image and
  // writes it to outputPath at (targetWidth, targetHeight), cover-cropped or
  // letterbox-contained per `crop`. Writes an empty placeholder on failure so
  // generation isn't retried every call.
  bool generateThumbBmpAtSize(const std::string& outputPath, int targetWidth, int targetHeight, bool crop) const;

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
    // create a cache key based on the filepath
    cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
  }
  ~Epub() = default;
  std::string& getBasePath() { return contentBasePath; }
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);
  // Lightweight alternative to load(true, ...) for a book that has never been
  // opened: parses content.opf for title/author/cover only (no spine/TOC, no
  // book.bin written), then generateThumbBmp() works normally off the result.
  // Returns true immediately if a real or metadata-only cache is already loaded.
  bool loadMetadataOnly();
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string getCoverBmpPath(bool cropped = false) const;
  bool generateCoverBmp(bool cropped = false) const;
  std::string getThumbBmpPath() const;
  // Continue Reading card thumbnail: height-only, 0.6:1 width:height assumption,
  // cover-crop scaled (may overflow the nominal width -- see generateThumbBmp(int)).
  std::string getThumbBmpPath(int height) const;
  // Exact-size thumbnail (e.g. a grid cell): both dimensions explicit, so the
  // cache filename changes whenever the target box does -- a stale entry from a
  // different box size is simply never looked up, not misrendered.
  std::string getThumbBmpPath(int width, int height) const;
  // Cover-crop scaled to (height*0.6, height); may overflow the nominal width
  // to fill both dimensions (see JpegToBmpConverter's crop=true doc). Intended
  // for the Continue Reading card, which crops at draw time.
  bool generateThumbBmp(int height) const;
  // Letterbox-contained to exactly (width, height): neither output dimension
  // ever exceeds the target, so a caller drawing at that exact box size needs
  // no further scaling. Intended for grid cells, which center instead of crop.
  bool generateThumbBmp(int width, int height) const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                bool allowEarlyStop = false) const;
  // Extract an item to a file on SD. On failure the partial file is removed.
  bool extractItemToFile(const std::string& itemHref, const std::string& destPath) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  int resolveHrefToSpineIndex(const std::string& href) const;
};
