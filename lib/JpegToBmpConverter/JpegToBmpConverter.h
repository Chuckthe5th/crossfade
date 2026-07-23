#pragma once

#include <HalStorage.h>

class Print;
class ZipFile;

class JpegToBmpConverter {
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          bool oneBit, bool crop = true);

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true);
  // Convert with custom target size (for thumbnails)
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering.
  // crop=true (default, existing callers): scales to COVER the target box, overflowing
  // whichever dimension doesn't match the source aspect ratio -- the caller is expected to
  // crop/clip at draw time. crop=false: scales to fit WITHIN the box (letterbox), so the
  // output BMP's own dimensions never exceed targetMaxWidth/targetMaxHeight and a caller
  // that draws it at that exact box size needs no further scaling.
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                              bool crop = true);
};
