#pragma once

#include <string>

namespace BookMetadataResolver {

struct Result {
  std::string title;
  std::string author;          // empty for formats/entries with no embedded author (TXT/MD, unparsed EPUB)
  std::string coverThumbPath;  // only populated when a cover box was requested and a cover was found
  bool hasCover = false;
  // True if the cover thumbnail cache was missing and had to be generated -- callers use this to
  // gate a loading popup, since a resolve that only reads existing cache costs no visible delay.
  bool coverGenerated = false;
};

// Resolves a book's title/author (and, if a non-zero cover box is given, its thumbnail cache path)
// at the minimum cost available: a cached book.bin if the book has been opened before, otherwise a
// lightweight metadata-only parse (Epub::loadMetadataOnly() / Xtc::load(), no spine/TOC build) for
// EPUB/XTC. TXT/MD have no embedded metadata and fall back to the filename (title only, no author).
//
// Pass coverWidth/coverHeight > 0 to also resolve -- generating it if the cache is missing -- the
// WxH thumbnail; pass 0, 0 (the default) to skip cover work entirely, e.g. for a text-only list that
// never draws one.
Result resolve(const std::string& path, int coverWidth = 0, int coverHeight = 0);

}  // namespace BookMetadataResolver
