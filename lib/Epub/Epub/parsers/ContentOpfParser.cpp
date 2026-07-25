#include "ContentOpfParser.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Serialization.h>
#include <XmlParserUtils.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "Epub/BookMetadataCache.h"

namespace {
constexpr char MEDIA_TYPE_NCX[] = "application/x-dtbncx+xml";
constexpr char MEDIA_TYPE_CSS[] = "text/css";
constexpr char MEDIA_TYPE_IMAGE_PREFIX[] = "image/";
constexpr char itemCacheFile[] = "/.items.bin";

// Appends up to `len` bytes of `s` into `dest`, never growing `dest` past
// ContentOpfParser::MAX_ACCUMULATED_FIELD_LEN -- std::string::append's internal growth
// (_M_create et al.) lives in a prebuilt libstdc++ that still has real exception support, while
// this project's own code compiles -fno-exceptions and has no handler for it, so an unbounded
// append that fails to allocate throws std::bad_alloc/length_error uncaught, aborting the whole
// device rather than failing this one book gracefully. Once the cap is hit, a field simply stops
// growing (keeping whatever was accumulated so far, not discarding it) instead of continuing to
// append. Returns true if `dest` is at the cap after this call, so the caller can flag it for a
// once-per-book diagnostic log rather than re-checking on every subsequent XML chunk.
bool appendBounded(std::string& dest, const char* s, const int len) {
  constexpr size_t cap = ContentOpfParser::MAX_ACCUMULATED_FIELD_LEN;
  if (len > 0 && dest.size() < cap) {
    dest.append(s, std::min(static_cast<size_t>(len), cap - dest.size()));
  }
  return dest.size() >= cap;
}

// Parses a decimal series index, tolerating fractional values (e.g. "1.5" for a novella).
// Returns -1.0f (the "no index" sentinel) if the text isn't a valid number.
float parseSeriesIndex(const std::string& text) {
  if (text.empty()) {
    return -1.0f;
  }
  char* end = nullptr;
  const float value = strtof(text.c_str(), &end);
  return (end != text.c_str()) ? value : -1.0f;
}

bool startsWithImageMediaType(const std::string& mediaType) {
  constexpr size_t prefixLen = sizeof(MEDIA_TYPE_IMAGE_PREFIX) - 1;
  if (mediaType.size() < prefixLen) {
    return false;
  }

  for (size_t i = 0; i < prefixLen; ++i) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(mediaType[i])));
    if (c != MEDIA_TYPE_IMAGE_PREFIX[i]) {
      return false;
    }
  }

  return true;
}
}  // namespace

bool ContentOpfParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_DBG("COF", "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

ContentOpfParser::~ContentOpfParser() {
  destroyXmlParser(parser);
  if (tempItemStore) {
    tempItemStore.close();
  }
  const auto itemCachePath = cachePath + itemCacheFile;
  if (Storage.exists(itemCachePath.c_str())) {
    Storage.remove(itemCachePath.c_str());
  }
}

size_t ContentOpfParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContentOpfParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);

    if (!buf) {
      LOG_ERR("COF", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_DBG("COF", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }

  return size;
}

ContentOpfParser::CollectionInfo& ContentOpfParser::findOrCreateCollection(const std::string& id) {
  for (auto& entry : collections) {
    if (entry.first == id) {
      return entry.second;
    }
  }
  collections.emplace_back(id, CollectionInfo{});
  return collections.back().second;
}

void XMLCALL ContentOpfParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)atts;

  if (self->state == START && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:title") == 0) {
    // Only capture the first dc:title element; subsequent ones are subtitles
    if (self->title.empty()) {
      self->state = IN_BOOK_TITLE;
    }
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:creator") == 0) {
    self->state = IN_BOOK_AUTHOR;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:language") == 0) {
    self->state = IN_BOOK_LANGUAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_MANIFEST;
    if (!Storage.openFileForWrite("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for writing. This is probably going to be a fatal error.");
    }
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_SPINE;
    if (!Storage.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
    }

    // Sort the (unconditionally-built) item index so every idref lookup uses binary
    // search. Without this, small/medium manifests fell back to an O(spine × manifest)
    // linear rescan of .items.bin per itemref (up to ~200ms/item at large scale).
    if (!self->itemIndex.empty()) {
      std::sort(self->itemIndex.begin(), self->itemIndex.end(), [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
        return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
      });
      self->useItemIndex = true;
      LOG_DBG("COF", "Using fast index for %zu manifest items", self->itemIndex.size());
    }
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_GUIDE;
    // TODO Remove print
    LOG_DBG("COF", "Entering guide state.");
    if (!Storage.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
    }
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "meta") == 0 || strcmp(name, "opf:meta") == 0)) {
    bool isCover = false;
    std::string metaName;
    std::string content;
    std::string property;
    std::string id;
    std::string refines;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0) {
        metaName = atts[i + 1];
        if (metaName == "cover") {
          isCover = true;
        }
      } else if (strcmp(atts[i], "content") == 0) {
        content = atts[i + 1];
      } else if (strcmp(atts[i], "property") == 0) {
        property = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        id = atts[i + 1];
      } else if (strcmp(atts[i], "refines") == 0) {
        // '#id' -> id, matching how idref/hrefs are dereferenced elsewhere in this parser.
        refines = (atts[i + 1][0] == '#') ? atts[i + 1] + 1 : atts[i + 1];
      }
    }

    if (isCover) {
      self->coverItemId = content;
    }

    // calibre:series / calibre:series_index: attribute-only, resolved immediately, no character
    // data to wait for. Takes priority over any EPUB3 collection found below (see </metadata>).
    if (metaName == "calibre:series" && self->series.empty()) {
      self->series = content;
    } else if (metaName == "calibre:series_index") {
      self->seriesIndex = parseSeriesIndex(content);
    }

    // EPUB3 belongs-to-collection / collection-type / group-position: the actual value is
    // character data between the tags, not an attribute, so capture it via IN_META_TEXT and
    // dispatch on close. Only meta elements with a `property` attribute use this path; calibre's
    // (and any other purely attribute-based meta) are already fully handled above.
    if (!property.empty()) {
      self->pendingMetaProperty = property;
      self->pendingMetaId = id;
      self->pendingMetaRefines = refines;
      self->pendingMetaText.clear();
      self->state = IN_META_TEXT;
    }
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "item") == 0 || strcmp(name, "opf:item") == 0)) {
    std::string itemId;
    std::string href;
    std::string mediaType;
    std::string properties;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        itemId = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + atts[i + 1]));
      } else if (strcmp(atts[i], "media-type") == 0) {
        mediaType = atts[i + 1];
      } else if (strcmp(atts[i], "properties") == 0) {
        properties = atts[i + 1];
      }
    }

    // Record index entry for fast lookup later
    if (self->tempItemStore) {
      ItemIndexEntry entry;
      entry.idHash = fnvHash(itemId);
      entry.idLen = static_cast<uint16_t>(itemId.size());
      entry.fileOffset = static_cast<uint32_t>(self->tempItemStore.position());
      self->itemIndex.push_back(entry);
    }

    // Write items down to SD card
    serialization::writeString(self->tempItemStore, itemId);
    serialization::writeString(self->tempItemStore, href);

    if (itemId == self->coverItemId) {
      // Some EPUBs set meta name="cover" to an XHTML wrapper item.
      // Only treat it as a cover image when the manifest media-type is image/*.
      if (startsWithImageMediaType(mediaType)) {
        self->coverItemHref = href;
      } else {
        LOG_DBG("COF", "Ignoring meta cover item '%s' with non-image media type: %s", itemId.c_str(),
                mediaType.c_str());
      }
    }

    if (mediaType == MEDIA_TYPE_NCX) {
      if (self->tocNcxPath.empty()) {
        self->tocNcxPath = href;
      } else {
        LOG_DBG("COF", "Warning: Multiple NCX files found in manifest. Ignoring duplicate: %s", href.c_str());
      }
    }

    // Collect CSS files
    if (mediaType == MEDIA_TYPE_CSS) {
      self->cssFiles.push_back(href);
    }

    // EPUB 3: Check for nav document (properties contains "nav")
    if (!properties.empty() && self->tocNavPath.empty()) {
      // Properties is space-separated, check if "nav" is present as a word
      if (properties == "nav" || properties.find("nav ") == 0 || properties.find(" nav") != std::string::npos) {
        self->tocNavPath = href;
        LOG_DBG("COF", "Found EPUB 3 nav document: %s", href.c_str());
      }
    }

    // EPUB 3: Check for cover image (properties contains "cover-image")
    if (!properties.empty() && self->coverItemHref.empty()) {
      if (properties == "cover-image" || properties.find("cover-image ") == 0 ||
          properties.find(" cover-image") != std::string::npos) {
        self->coverItemHref = href;
      }
    }
    return;
  }

  // NOTE: This relies on spine appearing after item manifest (which is pretty safe as it's part of the EPUB spec)
  // Only run the spine parsing if there's a cache to add it to
  if (self->cache) {
    if (self->state == IN_SPINE && (strcmp(name, "itemref") == 0 || strcmp(name, "opf:itemref") == 0)) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "idref") == 0) {
          const std::string idref = atts[i + 1];
          std::string href;
          bool found = false;

          if (self->useItemIndex) {
            // Fast path: binary search
            uint32_t targetHash = fnvHash(idref);
            uint16_t targetLen = static_cast<uint16_t>(idref.size());

            auto it = std::lower_bound(self->itemIndex.begin(), self->itemIndex.end(),
                                       ItemIndexEntry{targetHash, targetLen, 0},
                                       [](const ItemIndexEntry& a, const ItemIndexEntry& b) {
                                         return a.idHash < b.idHash || (a.idHash == b.idHash && a.idLen < b.idLen);
                                       });

            // Check for match (may need to check a few due to hash collisions)
            while (it != self->itemIndex.end() && it->idHash == targetHash) {
              self->tempItemStore.seek(it->fileOffset);
              std::string itemId;
              serialization::readString(self->tempItemStore, itemId);
              if (itemId == idref) {
                serialization::readString(self->tempItemStore, href);
                found = true;
                break;
              }
              ++it;
            }
          } else {
            // Fallback linear scan, only reached when the index is empty (no manifest
            // items). The fast binary-search path above is used for all real manifests.
            self->tempItemStore.seek(0);
            std::string itemId;
            while (self->tempItemStore.available()) {
              serialization::readString(self->tempItemStore, itemId);
              serialization::readString(self->tempItemStore, href);
              if (itemId == idref) {
                found = true;
                break;
              }
            }
          }

          if (found && self->cache) {
            self->cache->createSpineEntry(href);
          }
        }
      }
      return;
    }
  }
  // parse the guide
  if (self->state == IN_GUIDE && (strcmp(name, "reference") == 0 || strcmp(name, "opf:reference") == 0)) {
    std::string type;
    std::string guideHref;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "type") == 0) {
        type = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        guideHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + atts[i + 1]));
      }
    }
    if (!guideHref.empty()) {
      // EPUB 2 guides often mark every content file as "text", so that type
      // does not identify a reliable first-reading location. Only use the
      // explicit "start" semantic; otherwise the reader opens at spine index 0.
      if (type == "start" && !self->hasExplicitStartReference) {
        LOG_DBG("COF", "Found %s reference in guide: %s", type.c_str(), guideHref.c_str());
        self->textReferenceHref = guideHref;
        self->hasExplicitStartReference = type == "start";
      } else if ((type == "cover" || type == "cover-page") && self->guideCoverPageHref.empty()) {
        LOG_DBG("COF", "Found cover reference in guide: %s", guideHref.c_str());
        self->guideCoverPageHref = guideHref;
      }
    }
    return;
  }
}

void XMLCALL ContentOpfParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->state == IN_BOOK_TITLE) {
    if (appendBounded(self->title, s, len)) self->titleTruncated = true;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR) {
    if (!self->author.empty()) {
      self->author.append(", ");  // Add separator for multiple authors -- fixed 2 bytes, no cap needed
    }
    if (appendBounded(self->author, s, len)) self->authorTruncated = true;
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE) {
    if (appendBounded(self->language, s, len)) self->languageTruncated = true;
    return;
  }

  if (self->state == IN_META_TEXT) {
    if (appendBounded(self->pendingMetaText, s, len)) self->metaTextTruncated = true;
    return;
  }
}

void XMLCALL ContentOpfParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)name;

  if (self->state == IN_SPINE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_GUIDE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_BOOK_TITLE && strcmp(name, "dc:title") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR && strcmp(name, "dc:creator") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE && strcmp(name, "dc:language") == 0) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_META_TEXT && (strcmp(name, "meta") == 0 || strcmp(name, "opf:meta") == 0)) {
    if (self->pendingMetaProperty == "belongs-to-collection" && !self->pendingMetaId.empty()) {
      self->findOrCreateCollection(self->pendingMetaId).name = self->pendingMetaText;
    } else if (self->pendingMetaProperty == "collection-type" && !self->pendingMetaRefines.empty()) {
      self->findOrCreateCollection(self->pendingMetaRefines).isSeries = (self->pendingMetaText == "series");
    } else if (self->pendingMetaProperty == "group-position" && !self->pendingMetaRefines.empty()) {
      self->findOrCreateCollection(self->pendingMetaRefines).position = parseSeriesIndex(self->pendingMetaText);
    }
    // Other `property=` meta (e.g. dcterms:modified) are captured then discarded here -- not
    // one of the three we track.
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    // EPUB3 collection fallback: only consulted if calibre:series never set series (calibre
    // always wins). First collection whose type resolved to "series" (explicit or the EPUB3
    // default for an unrefined collection) with a non-empty name.
    if (self->series.empty()) {
      for (const auto& entry : self->collections) {
        if (entry.second.isSeries && !entry.second.name.empty()) {
          self->series = entry.second.name;
          self->seriesIndex = entry.second.position;
          break;
        }
      }
    }
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = START;
    return;
  }
}
