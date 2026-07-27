#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include "FirmwareFlasher.h"
#include <HalStorage.h>
#include <Logging.h>
#include <OtaVersion.h>
#include <ReleaseJsonParser.h>
#include <esp_wifi.h>
// clang-format on

#include <cstring>
#include <string>

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/Chuckthe5th/crossfade/releases/latest";
// Same directory every book cache already lives under -- guaranteed to exist (or cheaply
// recreated by the mkdir below) rather than a new top-level convention.
constexpr char kOtaStagingDir[] = "/.crosspoint";
constexpr char kOtaStagingPath[] = "/.crosspoint/ota_download.bin";
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser;
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
    releaseParser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  // Release tags are "vMAJOR.MINOR.PATCH" -- ota_version::parse() skips the leading 'v' and
  // compares each field numerically (see its header comment for why a plain sscanf on a
  // 'v'-prefixed string is unsafe). CROSSPOINT_VERSION itself has no 'v' prefix but may carry an
  // "-rc+<hash>" suffix, which parse() also ignores past the patch number.
  const bool currentIsRc = strstr(CROSSPOINT_VERSION, "-rc") != nullptr;
  return ota_version::isNewer(CROSSPOINT_VERSION, latestVersion.c_str(), currentIsRc);
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

void OtaUpdater::onFlashProgress(const size_t written, const size_t total, void* ctx) {
  auto* bridge = static_cast<ProgressBridge*>(ctx);
  bridge->self->processedSize = written;
  bridge->self->totalSize = total;
  if (!bridge->userCallback || total == 0) return;
  const int pct = static_cast<int>(static_cast<uint64_t>(written) * 100 / total);
  if (pct != bridge->lastReportedPct) {
    bridge->lastReportedPct = pct;
    bridge->userCallback(bridge->userCtx);
  }
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // Two phases: download to an SD staging file, then hand off to
  // firmware_flash::flashFromSdPath() -- the same validate-then-flash-then-switch path
  // SdFirmwareUpdateActivity already uses successfully on real X3/X4 hardware. The previous
  // version of this function streamed straight into esp_ota_write()/esp_ota_end()/
  // esp_ota_set_boot_partition(), which invokes esp_image_verify() -- already found (see
  // OtaBootSwitch.h) to reject this project's own valid images on this silicon with a bogus
  // efuse-blk-rev error, so that path would fail at the very last step on real hardware every
  // time. flashFromSdPath() validates the whole image (magic/segments/checksum/SHA256) before
  // ever touching the OTA partition, and switches boot via ota_boot::switchTo() -- a raw otadata
  // write that bypasses esp_image_verify entirely -- instead.
  //
  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this package can't
  // negotiate TLS 1.3 (see SecureClient.h), so the download itself still goes through
  // HttpDownloader (wolfSSL-backed when FREEINK_NET_WOLFSSL is set), not esp_https_ota.
  Storage.mkdir(kOtaStagingDir, /*pFlag=*/true);

  HalFile file;
  if (!Storage.openFileForWrite("OTA", kOtaStagingPath, file) || !file) {
    LOG_ERR("OTA", "Failed to open staging file: %s", kOtaStagingPath);
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize = 0;
  int lastReportedDownloadPct = -1;
  bool writeOk = true;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (file.write(data, len) != len) {
      writeOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedDownloadPct) {
        lastReportedDownloadPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });
  file.close();

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  if (!fetchOk || !writeOk) {
    LOG_ERR("OTA", "Firmware download failed (%s)", writeOk ? "http" : "sd write");
    Storage.remove(kOtaStagingPath);
    return HTTP_ERROR;
  }

  // Second phase: validate + flash the inactive OTA partition + switch boot. Reuses the same
  // onProgress callback via the bridge above -- the caller's progress bar resets to 0% for this
  // phase (a visible "downloading, then installing" two-phase sweep) rather than one continuous
  // bar, since flashFromSdPath reports its own written/total independent of the download's.
  ProgressBridge bridge{this, onProgress, ctx, -1};
  const firmware_flash::Result flashResult = firmware_flash::flashFromSdPath(
      kOtaStagingPath, &OtaUpdater::onFlashProgress, &bridge, /*alreadyValidated=*/false);
  Storage.remove(kOtaStagingPath);

  if (flashResult != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "Install failed: %s", firmware_flash::resultName(flashResult));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
