#pragma once
#include <Print.h>

#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

#include "Epub.h"
#include "expat.h"

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
    // EPUB3 collection metadata (<meta property="...">text</meta>, unlike calibre's
    // attribute-only <meta name="..." content="...">) -- captures character data for
    // whichever property the current <meta> declared; see epub3Collections handling below.
    IN_META_TEXT,
  };

  // One EPUB3 belongs-to-collection group, correlated across possibly-separate
  // <meta id="c1" property="belongs-to-collection">Name</meta> and
  // <meta refines="#c1" property="collection-type">series</meta> /
  // <meta refines="#c1" property="group-position">1.5</meta> elements, which the
  // spec allows in either order. Small (0-2 collections per book), so a linear-scan
  // vector keyed by id is simpler than the hashed index used for manifest items below.
  struct CollectionInfo {
    std::string name;
    // EPUB3 defaults an unrefined collection to type "series" -- true until a
    // collection-type refinement explicitly says otherwise.
    bool isSeries = true;
    float position = -1.0f;
  };

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  HalFile tempItemStore;
  std::string coverItemId;
  bool hasExplicitStartReference = false;

  // Index for fast idref→href lookup (binary search over .items.bin)
  struct ItemIndexEntry {
    uint32_t idHash;      // FNV-1a hash of itemId
    uint16_t idLen;       // length for collision reduction
    uint32_t fileOffset;  // offset in .items.bin
  };
  std::deque<ItemIndexEntry> itemIndex;
  bool useItemIndex = false;

  // EPUB3 collection correlation state, active only while state == IN_META_TEXT.
  std::string pendingMetaProperty;
  std::string pendingMetaId;       // this meta's own id attribute (belongs-to-collection primary)
  std::string pendingMetaRefines;  // the '#id' this meta refines, '#' stripped (collection-type/group-position)
  std::string pendingMetaText;
  std::vector<std::pair<std::string, CollectionInfo>> collections;
  CollectionInfo& findOrCreateCollection(const std::string& id);

  // FNV-1a hash function
  static uint32_t fnvHash(const std::string& s) {
    uint32_t hash = 2166136261u;
    for (char c : s) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return hash;
  }

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  // Real book titles/authors/languages/series metadata are always well under this -- a few
  // hundred bytes at most. Bounds title/author/language/pendingMetaText accumulation in
  // characterData so a corrupt or pathological content.opf can't exhaust the heap via an
  // unbounded std::string::append (see appendBounded in ContentOpfParser.cpp for why that
  // specifically aborts the device instead of throwing catchably).
  static constexpr size_t MAX_ACCUMULATED_FIELD_LEN = 512;

  // Set when the corresponding field hit the accumulation cap and stopped growing (see
  // appendBounded/characterData in ContentOpfParser.cpp) -- checked once by
  // Epub::parseContentOpf after the whole document is parsed, to log which book and field hit
  // it, rather than on every XML chunk.
  bool titleTruncated = false;
  bool authorTruncated = false;
  bool languageTruncated = false;
  bool metaTextTruncated = false;

  std::string title;
  std::string author;
  std::string language;
  // Series name/index: calibre:series/calibre:series_index meta take priority (attribute-only,
  // resolved immediately, no character-data state needed); if absent, falls back to an EPUB3
  // belongs-to-collection entry whose collection-type is "series" (or unrefined, which defaults to
  // series per spec), resolved once the whole </metadata> block has been seen (see endElement).
  // seriesIndex is -1.0f when no index was given (e.g. calibre:series without calibre:series_index).
  std::string series;
  float seriesIndex = -1.0f;
  std::string tocNcxPath;
  std::string tocNavPath;  // EPUB 3 nav document path
  std::string coverItemHref;
  std::string guideCoverPageHref;  // Guide reference with type="cover" or "cover-page" (points to XHTML wrapper)
  std::string textReferenceHref;
  std::vector<std::string> cssFiles;  // CSS stylesheet paths

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache)
      : cachePath(cachePath), baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
