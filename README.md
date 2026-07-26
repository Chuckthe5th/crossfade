# CrossFade

A personal fork of [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) firmware for the Xteink X3 and X4 e-readers, focused on making your book covers and library the center of the experience.

CrossFade keeps CrossPoint's on-disk format and MIT license, and is based on CrossPoint 1.4.1.

---

## ⚠️ Read this before flashing

**Flashing custom firmware can permanently brick a locked device.**

- Some Xteink units — especially those bought from third-party stores like AliExpress — ship with **USB flashing locked**. On a locked unit, a bad flash can leave the device stuck with no recovery path. Units bought directly from xteink.com are generally not locked.
- **Check first:** connect the device over USB-C and try the web flasher. If the device shows up and can be read, you're not locked. If it never appears, do not proceed without the Xteink Unlocker.
- **Back up first:** use the flasher's full-flash backup/read option and save the file somewhere safe. This is your only guaranteed way back.
- **You flash at your own risk.** This is a hobbyist fork provided as-is, with no warranty. See the license.

### Device support status

| Device | Status |
|--------|--------|
| **Xteink X3** | ✅ Built and tested. This is the primary target. |
| **Xteink X4** | ⚠️ **Builds but currently UNTESTED on hardware.** X4 testing coming shortly. Flash on X4 at your own risk until this note is updated. |

The X3 and X4 share one binary with runtime device detection, so the same release works on both — but only the X3 has been verified on real hardware so far.

---

## What CrossFade adds

Everything below is on top of stock CrossPoint.

### Browse Books — three ways to see your library

Selectable in **Settings → System → Browse Books view**:

- **Files** — the stock file browser, unchanged.
- **Titles** — a text list showing each book's title and author.
- **Covers** — a paginated 3×3 grid of cover thumbnails.

Covers and Titles show the same library in the same order, with a page-position indicator. Covers view has a **titles on / off** option (with titles off, covers render slightly larger) and a choice of **vertical or horizontal pagination** — vertical turns pages with the side buttons, horizontal with the front rocker. Thumbnails are generated once at the exact cell size and cached, so the grid stays responsive.

### Series grouping

Optional — **Settings → System → Group by series**. When enabled, books that share a series collapse into a single entry, and selecting one opens a page of just that series in reading order.

- Reads series and index from Calibre metadata (`calibre:series` / `calibre:series_index`), with EPUB3 collections as a fallback.
- In Covers view, a series is marked with a stepped stack of lines on the cover.
- In Titles view, a series entry is labelled `(series)`.
- Only groups when two or more books share a series.
- Opening Browse Books lands you in the series of whatever you were last reading.

Powered by a library index cached on the SD card and rebuilt incrementally — only new or changed books are re-parsed, so it stays fast after the first build. A manual **Rebuild Library Index** action is available, and the build can be cancelled.

### Recent Books

Gains its own **List / Covers** view, sharing the same cover-grid presentation.

### Per-book context menu

Long-press **Confirm** on a book (in Covers, Titles, or Recent Books) opens a context menu with per-book actions, including **Mark as finished**, which is also available from the in-book reader menu.

### Pin a book to Home *(new in v1.1.0)*

Optional — **Settings → System → Pin Book to Home**. Long-press **Confirm** on any book and choose **Pin to Home** to give it its own permanent Home-screen entry, independent of Recent Books — useful for a book you return to daily (a devotional, journal, or reference) that isn't necessarily what you read most recently. Selecting it resumes at its saved position, the same as Continue Reading. The entry only appears when there's genuinely enough room for it on screen for your theme and settings; otherwise it stays cleanly hidden rather than crowding out other rows.

### Transfer & Sync *(new in v1.1.0)*

OPDS Browser and File Transfer are combined into a single **Transfer & Sync** Home entry. With no OPDS servers configured it goes straight to File Transfer, the same one-tap experience as before; with servers configured it opens a small picker between the two.

### Hide button hints *(new in v1.1.0)*

Optional — **Settings → System → Hide Button Hints**. Removes the on-screen row showing what each physical button does, and lets whatever's above it reclaim that space — the same behavior touch-capable devices already get automatically.

### Smarter sleep behavior

Quick Resume now only applies when you're actually reading. If the device sleeps while browsing a grid, list, or menu, it shows the normal sleep screen instead of trying to resume a page that doesn't exist — which also avoids a wasted state-save and a flash of stale content on wake. Upstream's timeout-vs-manual Quick Resume options still apply while reading.

### Reader button remapping

Configurable front-button mapping that applies specifically in the reader.

### Performance

- Thumbnails are generated once at the exact cell size and cached as 1-bit BMPs, so rendering is pure streaming with no resize work at page-turn time.

---

## Installing

### Flash a release (recommended)

1. Read the warning above and take a full-flash backup.
2. Download the latest `firmware.bin` from the [Releases page](https://github.com/Chuckthe5th/crossfade/releases).
3. In **Chrome or Edge** (Firefox is not supported), open the CrossPoint web flasher at [crosspointreader.com](https://crosspointreader.com/#flash-tools), select your device, choose **Custom .bin**, and upload the downloaded file.

If something goes wrong: press Reset, then hold **Back + Power** to reach the home screen. If it boots but behaves oddly around covers, delete the `.crosspoint` folder on the SD card to clear the caches. Worst case, re-flash an official CrossPoint release or restore your backup from the same flasher.

### Build from source

CrossFade builds with [pioarduino](https://github.com/pioarduino/pioarduino) (a PlatformIO fork for the ESP32 Arduino 3.x core).

```
git clone --recursive https://github.com/Chuckthe5th/crossfade
cd crossfade
pio run -e default
```

The build artifact is `firmware.bin`, which you flash via the web flasher's Custom .bin option. Requires Python 3.8+, clang-format 21, and a USB-C data cable.

---

## Compatibility

CrossFade uses CrossPoint's standard `.crosspoint` SD-card folder, so switching between the two firmwares does not lose your covers, caches, or reading positions. Its own settings and cache files carry a fork marker so they can't be confused with upstream's, and settings are stored separately (`crossfade-settings.json`) so the two firmwares don't overwrite each other's preferences.

The over-the-air update check is currently disabled in CrossFade, so devices will not attempt to update themselves. Update by flashing a new release manually.

---

## Credit

CrossFade exists because CrossPoint's codebase is clean and well-documented enough that this was a spare-time project rather than a slog. Huge thanks to [Dave Allie](https://github.com/crosspoint-reader/crosspoint-reader) and the CrossPoint contributors, and to the wider fork community (CrossInk, Carousel, CrumBLE) whose work informed several of these features.

This is an independent fork and is not affiliated with or endorsed by the CrossPoint project.

---

## License

MIT. CrossFade retains CrossPoint's original copyright and MIT terms; see [LICENSE](LICENSE).

```
Copyright (c) 2026 Chuckthe5th
Portions copyright (c) the CrossPoint authors and contributors.
```
