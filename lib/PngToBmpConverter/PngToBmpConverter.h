#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
  static bool pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         bool oneBit, bool crop = true);

 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true);
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // crop=true (default, existing callers): scales to COVER the target box, overflowing
  // whichever dimension doesn't match the source aspect ratio -- the caller is expected to
  // crop/clip at draw time. crop=false: scales to fit WITHIN the box (letterbox), so the
  // output BMP's own dimensions never exceed targetMaxWidth/targetMaxHeight.
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                             bool crop = true);
};
