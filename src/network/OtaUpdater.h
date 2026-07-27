#pragma once

#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
  };

 private:
  // Bridges installUpdate()'s caller-supplied (void* ctx)-only ProgressCallback across into
  // firmware_flash::flashFromSdPath()'s (written, total, ctx) callback shape during the
  // validate+flash+switch phase, and updates processedSize/totalSize from it so
  // getProcessedSize()/getTotalSize() stay accurate for that phase the same way they already are
  // for the download phase.
  struct ProgressBridge {
    OtaUpdater* self;
    ProgressCallback userCallback;
    void* userCtx;
    int lastReportedPct;
  };
  static void onFlashProgress(size_t written, size_t total, void* ctx);

 public:
  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
