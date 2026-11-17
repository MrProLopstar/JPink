#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

  static void drawCustomLogoArt(GfxRenderer& renderer, int cx, int cy, bool dark);
  static void renderJapaneseArtScreen(GfxRenderer& renderer, bool dark);
  static void renderSpaceArtScreen(GfxRenderer& renderer, bool dark);
  static void renderMountainArtScreen(GfxRenderer& renderer, bool dark);
  static void renderBookshelfArtScreen(GfxRenderer& renderer, bool dark);
  static void renderGeometricArtScreen(GfxRenderer& renderer, bool dark);
  static void renderLogoSleepScreen(GfxRenderer& renderer, bool dark);

 private:
  void renderDefaultSleepScreen() const;
  void renderJapaneseArtSleepScreen() const;
  void renderSpaceArtSleepScreen() const;
  void renderMountainArtSleepScreen() const;
  void renderBookshelfArtSleepScreen() const;
  void renderGeometricArtSleepScreen() const;
  void renderCustomSleepScreen() const;
  bool renderCustomSleepBmp(const std::string& path) const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  // Cover + Transparent: the cover under the overlay, and the same cover while its gray planes are redrawn.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
