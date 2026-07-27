#pragma once

// Numeric semver comparison for OtaUpdater -- factored out of OtaUpdater.cpp so it can be unit
// tested on the host (see test/ota_version) without pulling in WiFi/esp_ota_ops/HttpDownloader.
namespace ota_version {

// Parses a version string of the form "vMAJOR.MINOR.PATCH[-suffix]" into three integers. A
// leading 'v'/'V' is optional and skipped; anything after the patch number (e.g. "-rc+abcdef") is
// ignored. Returns false -- with major/minor/patch all set to 0 -- if the string isn't an
// optional 'v' followed by three dot-separated digit runs. Callers must not compare the
// out-parameters when this returns false; the zeroing is only so a discarded failed parse can't
// leave them holding indeterminate values.
bool parse(const char* raw, int& major, int& minor, int& patch);

// True if `latest` is a strictly newer semver than `current` (major, then minor, then patch,
// each compared numerically -- so "v1.10.0" correctly beats "v1.9.0", unlike a lexicographic
// string compare). Either string failing to parse is treated as "not newer" rather than
// comparing garbage. `currentIsPrerelease` additionally treats a `latest` with an equal numeric
// version as newer: an RC build (whose own version string carries a "-rc" suffix stripped by
// parse()) should still be offered the real release it was cut from once that ships.
bool isNewer(const char* current, const char* latest, bool currentIsPrerelease);

}  // namespace ota_version
