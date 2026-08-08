#include "KOReaderAutoSync.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <WiFi.h>

#include <memory>
#include <optional>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "WifiCredentialStore.h"
#include "activities/reader/EpubReaderUtils.h"

namespace KOReaderAutoSync {

namespace {

// Direct connect to the last-known network, no scan -- a scan alone can cost several seconds,
// which this path can't afford. Bounded and abortable so a sleep transition or a book-open can
// never be stuck waiting on a network that isn't there.
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 3500;

bool userRequestedAbort() {
  gpio.update();
  return gpio.wasAnyPressed();
}

// Returns true once actually connected. Callers only reach here after confirming a saved
// credential exists, so a false return here always means "attempted and failed/timed out/aborted"
// -- never "never tried."
bool connectToSavedWifi() {
  if (WIFI_STORE.getCredentialCount() == 0) return false;

  std::optional<WifiCredential> cred;
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    cred = WIFI_STORE.findCredential(lastSsid);
  }
  if (!cred) {
    cred = WIFI_STORE.getCredentialAt(0);
  }
  if (!cred) return false;

  WiFi.mode(WIFI_STA);
  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }

  const unsigned long deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() >= deadline || userRequestedAbort()) {
      LOG_DBG("KOAuto", "WiFi connect timed out or aborted");
      return false;
    }
    delay(50);
  }
  return true;
}

void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// buildIfMissing=true: a book being pulled for the first time on this device has no book.bin cache
// yet, and a real spine build is the only way calculateProgress()/ProgressMapper get usable spine
// sizes (loadMetadataOnly() explicitly skips spine entries -- see Epub::loadMetadataOnly()). A book
// being pushed always already has one, from the reading session that just ended.
bool loadEpubForSync(Epub& epub, bool buildIfMissing) {
  if (buildIfMissing) epub.setupCacheDir();
  return epub.load(buildIfMissing, /*skipLoadingCss=*/true);
}

std::string computeDocumentHash(const std::string& path) {
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(path)
             : KOReaderDocumentId::calculate(path);
}

}  // namespace

void pushOnSleep() {
  if (!APP_STATE.lastSleepFromReader) return;
  const std::string& epubPath = APP_STATE.openEpubPath;
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath)) return;
  if (WIFI_STORE.getCredentialCount() == 0 || !KOREADER_STORE.hasCredentials()) return;

  const std::string hash = computeDocumentHash(epubPath);
  if (hash.empty()) return;

  if (!connectToSavedWifi()) return;

  KOReaderProgress remote;
  const auto getResult = KOReaderSyncClient::getProgress(hash, remote, userRequestedAbort);
  if (getResult != KOReaderSyncClient::OK && getResult != KOReaderSyncClient::NOT_FOUND) {
    LOG_DBG("KOAuto", "Push: remote fetch failed (%d), giving up", getResult);
    disconnectWifi();
    return;
  }

  // The book was just being read, so it was already fully loaded this session -- buildIfMissing
  // is false here, not true: if this unexpectedly can't load from cache, something is wrong
  // enough that guessing a position is worse than skipping the push.
  auto epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
  if (!loadEpubForSync(*epub, /*buildIfMissing=*/false)) {
    LOG_DBG("KOAuto", "Push: could not load epub from cache, skipping");
    disconnectWifi();
    return;
  }

  int spineIndex = 0;
  int pageNumber = 0;
  int pageCount = 0;
  const bool hadLocal = EpubReaderUtils::loadProgress(epub->getCachePath(), spineIndex, pageNumber, pageCount);
  const float chapterProgress = (hadLocal && pageCount > 0) ? static_cast<float>(pageNumber) / pageCount : 0.0f;
  const float localPercentage = hadLocal ? epub->calculateProgress(spineIndex, chapterProgress) : 0.0f;

  // Furthest-wins: only push when local is genuinely ahead. getResult == NOT_FOUND means there's
  // nothing to compare against, so any real local progress is worth uploading.
  if (getResult == KOReaderSyncClient::OK && remote.percentage >= localPercentage) {
    LOG_DBG("KOAuto", "Push: remote (%.4f) already >= local (%.4f), nothing to do", remote.percentage,
            localPercentage);
    disconnectWifi();
    return;
  }
  if (!hadLocal) {
    disconnectWifi();
    return;
  }

  const SavedProgressPosition saved = ProgressMapper::toSavedProgress(epub, CrossPointPosition{
                                                                                 spineIndex,
                                                                                 pageNumber,
                                                                                 pageCount,
                                                                             });

  KOReaderProgress toPush;
  toPush.document = hash;
  toPush.progress = saved.xpath;
  toPush.percentage = saved.percentage;
  toPush.device = SETTINGS.getEffectiveDeviceName();

  const auto putResult = KOReaderSyncClient::updateProgress(toPush, userRequestedAbort);
  LOG_DBG("KOAuto", "Push result: %d (local=%.4f)", putResult, localPercentage);
  disconnectWifi();
}

void pullFurthestOnOpen(const std::string& epubPath, GfxRenderer& renderer) {
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath)) return;
  if (WIFI_STORE.getCredentialCount() == 0 || !KOREADER_STORE.hasCredentials()) return;

  const std::string hash = computeDocumentHash(epubPath);
  if (hash.empty()) return;

  if (!connectToSavedWifi()) return;

  KOReaderProgress remote;
  const auto getResult = KOReaderSyncClient::getProgress(hash, remote, userRequestedAbort);
  disconnectWifi();  // one request either way; done with the radio regardless of outcome

  if (getResult != KOReaderSyncClient::OK) return;

  // First local book.bin build for this device happens here if it's never been opened locally --
  // see loadEpubForSync()'s comment. Deferred until after the network round trip so the common
  // "no WiFi" / "no remote progress" cases never pay for it.
  auto epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
  if (!loadEpubForSync(*epub, /*buildIfMissing=*/true)) {
    LOG_DBG("KOAuto", "Pull: could not load epub, skipping");
    return;
  }

  int spineIndex = 0;
  int pageNumber = 0;
  int pageCount = 0;
  const bool hadLocal = EpubReaderUtils::loadProgress(epub->getCachePath(), spineIndex, pageNumber, pageCount);
  const float chapterProgress = (hadLocal && pageCount > 0) ? static_cast<float>(pageNumber) / pageCount : 0.0f;
  const float localPercentage = hadLocal ? epub->calculateProgress(spineIndex, chapterProgress) : 0.0f;

  if (remote.percentage <= localPercentage) {
    LOG_DBG("KOAuto", "Pull: local (%.4f) already >= remote (%.4f), leaving local position", localPercentage,
            remote.percentage);
    return;
  }

  // Prefer the exact spine/page from a CrossPoint sync server when available (lossless); fall back
  // to xpath/percentage mapping otherwise -- store-and-forward, not re-derived: both paths apply
  // the server's own fields via ProgressMapper as-is.
  std::optional<CrossPointPosition> mapped;
  if (remote.position.has_value()) {
    mapped = ProgressMapper::fromRichPosition(epub, *remote.position, renderer);
  }
  if (!mapped.has_value()) {
    const SavedProgressPosition koPos{remote.progress, remote.percentage};
    mapped = ProgressMapper::toCrossPoint(epub, koPos, renderer, /*currentSpineIndex=*/-1,
                                          /*totalPagesInCurrentSpine=*/0);
  }
  if (!mapped.has_value()) return;

  if (!EpubReaderUtils::saveProgress(*epub, mapped->spineIndex, mapped->pageNumber, mapped->totalPages)) {
    LOG_ERR("KOAuto", "Pull: found further remote progress but failed to save it locally");
    return;
  }
  LOG_DBG("KOAuto", "Pull: applied remote position (local was %.4f, remote %.4f)", localPercentage,
          remote.percentage);
}

}  // namespace KOReaderAutoSync
