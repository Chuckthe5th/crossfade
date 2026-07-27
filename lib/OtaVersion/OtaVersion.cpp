#include "OtaVersion.h"

namespace ota_version {

namespace {
bool parseInt(const char*& s, int& out) {
  if (*s < '0' || *s > '9') return false;
  int val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    ++s;
  }
  out = val;
  return true;
}
}  // namespace

bool parse(const char* raw, int& major, int& minor, int& patch) {
  major = minor = patch = 0;
  if (!raw) return false;

  const char* s = raw;
  if (*s == 'v' || *s == 'V') ++s;

  if (!parseInt(s, major)) return false;
  if (*s != '.') return false;
  ++s;
  if (!parseInt(s, minor)) return false;
  if (*s != '.') return false;
  ++s;
  if (!parseInt(s, patch)) return false;
  return true;
}

bool isNewer(const char* current, const char* latest, const bool currentIsPrerelease) {
  int currentMajor, currentMinor, currentPatch;
  int latestMajor, latestMinor, latestPatch;
  if (!parse(current, currentMajor, currentMinor, currentPatch) ||
      !parse(latest, latestMajor, latestMinor, latestPatch)) {
    return false;
  }

  if (latestMajor != currentMajor) return latestMajor > currentMajor;
  if (latestMinor != currentMinor) return latestMinor > currentMinor;
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // Numerically equal: only a prerelease's own next matching release counts as "newer".
  return currentIsPrerelease;
}

}  // namespace ota_version
