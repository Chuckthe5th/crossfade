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
  if (!KOREADER_STORE.getAutoSyncEnabled()) {
    LOG_DBG("KOAuto", "Push: gated out -- auto-sync disabled in settings");
    return;
  }
  if (!APP_STATE.lastSleepFromReader) {
    LOG_DBG("KOAuto", "Push: gated out -- last sleep wasn't from the reader");
    return;
  }
  const std::string& epubPath = APP_STATE.openEpubPath;
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath)) {
    LOG_DBG("KOAuto", "Push: gated out -- no open EPUB (path='%s')", epubPath.c_str());
    return;
  }
  if (WIFI_STORE.getCredentialCount() == 0 || !KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOAuto", "Push: gated out -- %zu saved WiFi network(s), KOReader credentials %s",
            WIFI_STORE.getCredentialCount(), KOREADER_STORE.hasCredentials() ? "present" : "MISSING");
    return;
  }

  // Logged unconditionally, before any network attempt: this is the value most likely to explain
  // a silent cross-device mismatch (BINARY requires byte-identical files; a different match method
  // or a same-titled-but-not-identical file on each device produces two different hashes, so each
  // device's GET/PUT silently talks to a different server-side document with no error anywhere).
  const auto matchMethod = KOREADER_STORE.getMatchMethod();
  const std::string hash = computeDocumentHash(epubPath);
  LOG_DBG("KOAuto", "Push: doc='%s' match=%s hash=%s", epubPath.c_str(),
          matchMethod == DocumentMatchMethod::FILENAME ? "filename" : "binary",
          hash.empty() ? "(empty)" : hash.c_str());
  if (hash.empty()) {
    LOG_DBG("KOAuto", "Push: gated out -- empty document hash");
    return;
  }

  if (!connectToSavedWifi()) {
    LOG_DBG("KOAuto", "Push: WiFi connect failed or timed out, giving up");
    return;
  }
  LOG_DBG("KOAuto", "Push: WiFi connected (%s)", WiFi.SSID().c_str());

  KOReaderProgress remote;
  const auto getResult = KOReaderSyncClient::getProgress(hash, remote, userRequestedAbort);
  LOG_DBG("KOAuto", "Push: remote fetch -> %s (http=%d)", KOReaderSyncClient::errorString(getResult),
          KOReaderSyncClient::lastHttpCode);
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

  LOG_DBG("KOAuto", "Push: local=%.4f%% (hadLocal=%d spine=%d page=%d/%d) remote=%s%.4f%%", localPercentage * 100,
          hadLocal, spineIndex, pageNumber, pageCount,
          getResult == KOReaderSyncClient::NOT_FOUND ? "none-found " : "", remote.percentage * 100);

  // Furthest-wins: only push when local is genuinely ahead. getResult == NOT_FOUND means there's
  // nothing to compare against, so any real local progress is worth uploading.
  if (getResult == KOReaderSyncClient::OK && remote.percentage >= localPercentage) {
    LOG_DBG("KOAuto", "Push: remote (%.4f) already >= local (%.4f), nothing to do", remote.percentage,
            localPercentage);
    disconnectWifi();
    return;
  }
  if (!hadLocal) {
    LOG_DBG("KOAuto", "Push: no local progress saved yet, nothing to push");
    disconnectWifi();
    return;
  }
  LOG_DBG("KOAuto", "Push: local is ahead, uploading");

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

  LOG_DBG("KOAuto", "Push: sending document=%s device=%s percentage=%.4f xpath=%s", toPush.document.c_str(),
          toPush.device.c_str(), toPush.percentage, toPush.progress.c_str());

  const auto putResult = KOReaderSyncClient::updateProgress(toPush, userRequestedAbort);
  LOG_DBG("KOAuto", "Push result: %s (http=%d, local=%.4f)", KOReaderSyncClient::errorString(putResult),
          KOReaderSyncClient::lastHttpCode, localPercentage);
  disconnectWifi();
}

void pullFurthestOnOpen(const std::string& epubPath, GfxRenderer& renderer) {
  if (!KOREADER_STORE.getAutoSyncEnabled()) {
    LOG_DBG("KOAuto", "Pull: gated out -- auto-sync disabled in settings");
    return;
  }
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath)) {
    LOG_DBG("KOAuto", "Pull: gated out -- not an EPUB (path='%s')", epubPath.c_str());
    return;
  }
  if (WIFI_STORE.getCredentialCount() == 0 || !KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOAuto", "Pull: gated out -- %zu saved WiFi network(s), KOReader credentials %s",
            WIFI_STORE.getCredentialCount(), KOREADER_STORE.hasCredentials() ? "present" : "MISSING");
    return;
  }

  // See pushOnSleep()'s comment: this is the value most likely to explain a silent cross-device
  // mismatch. Compare this line's hash/match against the OTHER device's push-side log line for the
  // same book -- if they differ, that's the whole bug right there.
  const auto matchMethod = KOREADER_STORE.getMatchMethod();
  const std::string hash = computeDocumentHash(epubPath);
  LOG_DBG("KOAuto", "Pull: doc='%s' match=%s hash=%s", epubPath.c_str(),
          matchMethod == DocumentMatchMethod::FILENAME ? "filename" : "binary",
          hash.empty() ? "(empty)" : hash.c_str());
  if (hash.empty()) {
    LOG_DBG("KOAuto", "Pull: gated out -- empty document hash");
    return;
  }

  if (!connectToSavedWifi()) {
    LOG_DBG("KOAuto", "Pull: WiFi connect failed or timed out, giving up");
    return;
  }
  LOG_DBG("KOAuto", "Pull: WiFi connected (%s)", WiFi.SSID().c_str());

  KOReaderProgress remote;
  const auto getResult = KOReaderSyncClient::getProgress(hash, remote, userRequestedAbort);
  disconnectWifi();  // one request either way; done with the radio regardless of outcome
  LOG_DBG("KOAuto", "Pull: remote fetch -> %s (http=%d)", KOReaderSyncClient::errorString(getResult),
          KOReaderSyncClient::lastHttpCode);

  if (getResult != KOReaderSyncClient::OK) {
    // NOT_FOUND here means the server has never seen this hash -- on a cross-device mismatch this
    // is exactly what fires on every pull, silently, forever: check the hash logged above against
    // the other device's.
    LOG_DBG("KOAuto", "Pull: no usable remote progress (%s), leaving local position",
            KOReaderSyncClient::errorString(getResult));
    return;
  }

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

  LOG_DBG("KOAuto", "Pull: remote=%.4f%% (device=%s has_rich_position=%d) local=%.4f%% (hadLocal=%d spine=%d page=%d/%d)",
          remote.percentage * 100, remote.device.c_str(), remote.position.has_value(), localPercentage * 100, hadLocal,
          spineIndex, pageNumber, pageCount);

  if (remote.percentage <= localPercentage) {
    LOG_DBG("KOAuto", "Pull: local (%.4f) already >= remote (%.4f), leaving local position", localPercentage,
            remote.percentage);
    return;
  }
  LOG_DBG("KOAuto", "Pull: remote is further, mapping to a local position");

  // Prefer the exact spine/page from a CrossPoint sync server when available (lossless); fall back
  // to xpath/percentage mapping otherwise -- store-and-forward, not re-derived: both paths apply
  // the server's own fields via ProgressMapper as-is.
  std::optional<CrossPointPosition> mapped;
  if (remote.position.has_value()) {
    mapped = ProgressMapper::fromRichPosition(epub, *remote.position, renderer);
    LOG_DBG("KOAuto", "Pull: rich-position mapping %s", mapped.has_value() ? "succeeded" : "failed, falling back");
  }
  if (!mapped.has_value()) {
    const SavedProgressPosition koPos{remote.progress, remote.percentage};
    mapped = ProgressMapper::toCrossPoint(epub, koPos, renderer, /*currentSpineIndex=*/-1,
                                          /*totalPagesInCurrentSpine=*/0);
    LOG_DBG("KOAuto", "Pull: xpath/percentage mapping %s", mapped.has_value() ? "succeeded" : "failed");
  }
  if (!mapped.has_value()) {
    LOG_ERR("KOAuto", "Pull: found further remote progress but could not map it to a local position, giving up");
    return;
  }
  LOG_DBG("KOAuto", "Pull: mapped to spine=%d page=%d/%d", mapped->spineIndex, mapped->pageNumber, mapped->totalPages);

  if (!EpubReaderUtils::saveProgress(*epub, mapped->spineIndex, mapped->pageNumber, mapped->totalPages)) {
    LOG_ERR("KOAuto", "Pull: found further remote progress but failed to save it locally");
    return;
  }
  LOG_DBG("KOAuto", "Pull: applied remote position (local was %.4f, remote %.4f) -> spine=%d page=%d/%d",
          localPercentage, remote.percentage, mapped->spineIndex, mapped->pageNumber, mapped->totalPages);
}

}  // namespace KOReaderAutoSync
