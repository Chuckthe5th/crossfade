#include "FinishedBooksStore.h"

#include <Logging.h>

#include <algorithm>

void FinishedBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["paths"].to<JsonArray>();
  for (const auto& path : finishedPaths) {
    arr.add(path);
  }
}

bool FinishedBooksStore::fromJson(JsonVariantConst doc) {
  finishedPaths.clear();
  JsonArrayConst arr = doc["paths"].as<JsonArrayConst>();
  finishedPaths.reserve(arr.size());
  for (JsonVariantConst v : arr) {
    finishedPaths.push_back(v.as<std::string>());
  }
  LOG_DBG("FBS", "Finished books loaded from file (%d entries)", static_cast<int>(finishedPaths.size()));
  return true;
}

bool FinishedBooksStore::isFinished(const std::string& path) const {
  return std::find(finishedPaths.begin(), finishedPaths.end(), path) != finishedPaths.end();
}

void FinishedBooksStore::setFinished(const std::string& path, const bool finished) {
  const auto it = std::find(finishedPaths.begin(), finishedPaths.end(), path);
  if (finished) {
    if (it == finishedPaths.end()) {
      finishedPaths.push_back(path);
    } else {
      return;  // Already finished -- nothing changed, no need to save.
    }
  } else {
    if (it == finishedPaths.end()) {
      return;  // Already not finished -- nothing changed, no need to save.
    }
    finishedPaths.erase(it);
  }
  saveToFile();
}

void FinishedBooksStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  const auto it = std::find(finishedPaths.begin(), finishedPaths.end(), oldPath);
  if (it == finishedPaths.end()) {
    return;
  }
  *it = newPath;
  saveToFile();
}
