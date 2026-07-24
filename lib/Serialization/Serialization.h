#pragma once
#include <HalStorage.h>

#include <iostream>

namespace serialization {
template <typename T>
void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
void readPod(HalFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

inline void writeString(HalFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

inline void readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  s.resize(len);
  is.read(&s[0], len);
}

inline void readString(HalFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  s.resize(len);
  file.read(&s[0], len);
}

// Shared 4-byte magic this fork stamps at the start of its own on-disk binary cache formats
// (book.bin, library_index.bin), written before each format's own version byte. A numerically
// colliding version byte from a differently-shaped format -- upstream's own book.bin/
// library_index.bin, or a future upstream release that independently reaches the same version
// number this fork is already using -- is never silently misinterpreted: the magic check runs
// first and fails exactly like a version mismatch does, triggering the same safe, automatic
// rebuild every existing version-mismatch path already performs. Not used for
// crossfade-settings.json -- JSON's key-based fields are already self-describing (an unrecognized
// key is ignored, a missing one falls back to its default), so there's no positional-misread
// hazard for a marker to guard against there.
constexpr uint32_t FORK_MAGIC = 0x44465243;  // on disk (little-endian): 'C', 'R', 'F', 'D'

template <typename F>
void writeForkMagic(F& file) {
  writePod(file, FORK_MAGIC);
}

template <typename F>
bool readForkMagic(F& file) {
  uint32_t magic = 0;
  readPod(file, magic);
  return magic == FORK_MAGIC;
}
}  // namespace serialization
