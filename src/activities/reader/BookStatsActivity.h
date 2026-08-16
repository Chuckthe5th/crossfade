#pragma once
#include <I18n.h>

#include "BookReadingStats.h"
#include "activities/Activity.h"

// Minimal reading-stats readout, reached from the reader menu (Main tab) when
// SETTINGS.shouldTrackReadingStats() is on. Deliberately not a port of CrossInk/CrumBLE's
// BookStatsActivity/BookStatsView dashboard -- no charts, streaks, or heatmap, just the numbers
// this fork's trimmed BookReadingStats/GlobalReadingStats actually track. Any key/tap dismisses,
// matching QrDisplayActivity's pattern.
class BookStatsActivity final : public Activity {
 public:
  explicit BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const BookReadingStats& stats)
      : Activity("BookStats", renderer, mappedInput), stats(stats) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Snapshot passed in by EpubReaderActivity -- already resident there (loaded in onEnter(), kept
  // current through the session), so this avoids a redundant stats.bin read for the same data.
  BookReadingStats stats;
};
