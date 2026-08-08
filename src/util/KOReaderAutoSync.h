#pragma once
#include <string>

class GfxRenderer;

// Automatic, silent KOSync push-on-sleep / pull-on-open. Reuses the same client code the manual
// "Sync Progress" reader-menu action does (KOReaderSyncClient, KOReaderDocumentId, ProgressMapper,
// KOReaderCredentialStore) -- this is not a second sync client, just headless callers of the
// existing one. Both entry points:
//  - No-op (no radio, no delay) unless a saved WiFi network and KOReader credentials both exist.
//  - Use a short, direct connect (last-connected SSID, no scan) bounded by a few seconds.
//  - Are abortable mid-connect/mid-request via a button press, so they can never strand the
//    caller in a half-finished state.
//  - Never surface UI, never block their caller beyond the bounded timeout, and treat every
//    failure (no WiFi, auth, network, server) as "give up silently, proceed as if sync wasn't
//    attempted."
//  - Store-and-forward the KOReader progress/percentage fields verbatim on the pull side rather
//    than re-deriving them -- CrossPoint only computes its OWN side of a comparison, never the
//    remote's. See lib/KOReaderSync/ProgressMapper.h.
namespace KOReaderAutoSync {

// Call from main.cpp's enterDeepSleep(), after the sleep screen is already painted (so there's no
// visible indication anything is happening) and before its existing WiFi teardown -- this function
// manages the radio's on/off lifecycle itself, so that teardown is a harmless no-op afterward.
// Pushes APP_STATE.openEpubPath's current local progress if it's ahead of the server's; no-ops
// entirely unless APP_STATE.lastSleepFromReader is true.
void pushOnSleep();

// Call when a book is freshly opened from a browsing screen (Home/Covers/Titles) -- NOT on
// boot-resume or quick-resume-from-sleep, where this would defeat the point of "quick." Pulls the
// server's progress for epubPath and, if it's further than local, writes it into progress.bin
// before the caller constructs the reader activity, so EpubReaderActivity's own
// EpubReaderUtils::loadProgress() picks it up with no changes on its side. Only applies to EPUB
// files (matching the manual sync feature's own scope); a no-op for any other extension.
void pullFurthestOnOpen(const std::string& epubPath, GfxRenderer& renderer);

}  // namespace KOReaderAutoSync
