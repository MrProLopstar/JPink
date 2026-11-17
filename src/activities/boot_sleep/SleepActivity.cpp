#include "SleepActivity.h"

#include <BitmapHelpers.h>
#include <BmpToBmpConverter.h>
#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"

namespace {
// Metalio: B/W sleep images use the full 0xF7 waveform instead of HALF (two
// 0xFC partials), matching the clean pass before absolute gray below.
#if FREEINK_DEVICE_METALIO_EINK4
constexpr auto kSleepClean = HalDisplay::FULL_REFRESH;
#else
constexpr auto kSleepClean = HalDisplay::HALF_REFRESH;
#endif

// The absolute gray paint has no base pass. On Metalio the prior screen shows
// through it after the panel sits unpowered, so clean with 0xF7 first.
void cleanBeforeAbsoluteGray(GfxRenderer& renderer) {
#if FREEINK_DEVICE_METALIO_EINK4
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
#else
  (void)renderer;
#endif
}

HalDisplay::GrayscaleMode sleepGrayscaleMode(const GfxRenderer& renderer) {
  return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
             ? HalDisplay::GrayscaleMode::Direct
             : HalDisplay::GrayscaleMode::Absolute;
}

// SSD absolute images use the new thresholds; other panels retain legacy tuning.
bool useOriginalCoverThresholds(const GfxRenderer& renderer) {
  return renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported() &&
         display.getController() == HalDisplay::Controller::SSD1677 &&
         SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}

// Kept separate from normal custom sleep sources so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
        case TransparentOverlayPass::GrayscaleMsb: {
          const auto planePixel =
              grayPlanePixel(level, pass == TransparentOverlayPass::GrayscaleMsb, renderer.grayPlanesAreAbsolute());
          if (planePixel.write) renderer.drawPixel(screenX, screenY, planePixel.black);
          break;
        }
      }
    }
  }

  return true;
}

// Starts a gray plane under an overlay. Absolute planes keep the B/W base; other gray planes carry only
// the overlay's gray. Cover + Transparent redraws a gray cover (set only for absolute planes) to keep its gray levels.
// Overlay gray leaves a mask plane in the framebuffer, so a later B/W update of
// the retained sleep frame (the Picco USB charge strip) needs the B/W frame back.
// Absolute planes must keep the gray baseline: restoring the B/W frame there
// makes the next fast refresh drop the grays (Picco SSD1677).
class BwFrameKeeper {
 public:
  BwFrameKeeper(GfxRenderer& renderer, const bool keep)
      : renderer(renderer), keep(keep), stored(!keep || renderer.storeBwBuffer()) {}
  ~BwFrameKeeper() {
    if (keep && stored) renderer.restoreBwBuffer();
  }
  // False when the B/W frame could not be kept; callers must skip the gray passes.
  bool ready() const { return stored; }
  BwFrameKeeper(const BwFrameKeeper&) = delete;
  BwFrameKeeper& operator=(const BwFrameKeeper&) = delete;

 private:
  GfxRenderer& renderer;
  const bool keep;
  const bool stored;
};

bool beginOverlayGrayPlane(GfxRenderer& renderer, const Bitmap* grayCover, const GfxRenderer::RenderMode plane,
                           const bool absolute) {
  if (grayCover) {
    if (grayCover->rewindToData() != BmpReaderError::Ok) return false;
    renderer.clearScreen();
  } else if (!absolute) {
    renderer.clearScreen(0x00);
  }
  renderer.setRenderMode(plane);
  if (!grayCover) return true;
  const auto placement = calculateBitmapPlacement(grayCover->getWidth(), grayCover->getHeight(), renderer);
  return renderer.drawBitmap(*grayCover, placement.x, placement.y, renderer.getScreenWidth(),
                             renderer.getScreenHeight(), placement.cropX, placement.cropY);
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog,
                                                  const Bitmap* grayCover) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(kSleepClean);
    return AlphaOverlayResult::Rendered;
  }
  if (absolute) {
    cleanBeforeAbsoluteGray(renderer);
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return AlphaOverlayResult::Error;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!beginOverlayGrayPlane(renderer, grayCover, GfxRenderer::GRAYSCALE_LSB, absolute) ||
      !renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Keep the current display instead of trying another overlay with a
    // framebuffer that now contains an incomplete gray plane.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!beginOverlayGrayPlane(renderer, grayCover, GfxRenderer::GRAYSCALE_MSB, absolute) ||
      !renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

void getLCGState(const SleepRecentKind kind, uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                 uint16_t& indexesTotal) {
  switch (kind) {
    case SleepRecentKind::Overlay:
      APP_STATE.getSleepOverlayLCG(seed, state, indexesLeft, indexesTotal);
      break;
    case SleepRecentKind::Standard:
      APP_STATE.getSleepLCG(seed, state, indexesLeft, indexesTotal);
      break;
  }
}

void setLCGState(const SleepRecentKind kind, uint32_t seed, uint32_t state, uint16_t indexesLeft,
                 uint16_t indexesTotal) {
  switch (kind) {
    case SleepRecentKind::Overlay:
      APP_STATE.setSleepOverlayLCG(seed, state, indexesLeft, indexesTotal);
      break;
    case SleepRecentKind::Standard:
      APP_STATE.setSleepLCG(seed, state, indexesLeft, indexesTotal);
      break;
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* firstDirPath, const char* secondDirPath, const SleepRecentKind recentKind,
                           std::string& selectedPath) {
  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  constexpr size_t DIRECTORY_COUNT = 2;
  const char* dirPaths[DIRECTORY_COUNT] = {firstDirPath, secondDirPath};
  uint16_t directoryCounts[DIRECTORY_COUNT] = {};
  uint16_t fileCount = 0;
  for (size_t i = 0; i < DIRECTORY_COUNT; i++) {
    auto dir = Storage.open(dirPaths[i]);
    if (!dir || !dir.isDirectory()) continue;

    const auto remainingCapacity = static_cast<uint16_t>(UINT16_MAX - fileCount);
    while (directoryCounts[i] < remainingCapacity && findNextValidSleepImage(dir, recentKind, name.get())) {
      directoryCounts[i]++;
    }
    fileCount = static_cast<uint16_t>(fileCount + directoryCounts[i]);
  }

  if (fileCount == 0) return false;

  uint16_t randomFileIndex = 0;

  // Hull-Dobell theorem conditions for full period over mod 2^k: odd increment c and multiplier a = 5
  // (a - 1 divisible by 4) give period m for any seed; m = 1 is trivial.
  // https://en.wikipedia.org/wiki/Linear_congruential_generator#Period_length
  // Cycle-walking skips states >= fileCount, visiting every image once per cycle and finding a valid index
  // within at most lcgM steps.
  uint16_t lcgIndexesLeft, lcgIndexesTotal;
  uint32_t lcgSeed, lcgState;
  uint32_t lcgM = 1;

  getLCGState(recentKind, lcgSeed, lcgState, lcgIndexesLeft, lcgIndexesTotal);

  while (lcgM < fileCount) {
    lcgM <<= 1;
  }

  uint32_t lcgC = (lcgSeed * 2u + 1u) % lcgM;

  if (lcgIndexesLeft == 0 || lcgIndexesTotal != fileCount || lcgIndexesLeft > fileCount || lcgState >= lcgM) {
    uint16_t previousIndexes[CrossPointState::SLEEP_AVOID_REPEAT_LOOKBACK];
    uint8_t previousCount = 0;
    const uint8_t window =
        static_cast<uint8_t>(std::min<uint16_t>(fileCount / 2, CrossPointState::SLEEP_AVOID_REPEAT_LOOKBACK));
    if (lcgIndexesLeft == 0 && lcgIndexesTotal == fileCount && lcgState < fileCount) {
      // Invert the completed cycle (5 * 0xCCCCCCCD = 1 mod 2^32) to obtain recently shown image indexes
      static constexpr uint32_t LCG_MULTIPLIER_INVERSE = 0xCCCCCCCDu;
      uint32_t previousState = lcgState;
      for (uint32_t step = 0; step < lcgM && previousCount < window; ++step) {
        if (previousState < fileCount) previousIndexes[previousCount++] = static_cast<uint16_t>(previousState);
        previousState = (LCG_MULTIPLIER_INVERSE * (previousState - lcgC)) & (lcgM - 1u);
      }
    }

    lcgIndexesLeft = lcgIndexesTotal = fileCount;
    const auto isPreviousIndex = [&](uint32_t index) {
      return std::find(previousIndexes, previousIndexes + previousCount, index) != previousIndexes + previousCount;
    };

    // Find a new seed that
    // 1. doesn't immediately repeat an image
    // 2. has the fewest overlaps with previousIndexes in the remaining first "window" generations
    // 3. ideally isn't the same seed as the previous one
    uint8_t bestOverlapCount = window + 1;
    const uint32_t prevLcgSeed = lcgSeed;
    static constexpr uint8_t MAX_SEED_ATTEMPTS = 16;
    for (uint8_t attempt = 0; attempt < MAX_SEED_ATTEMPTS; ++attempt) {
      const uint32_t candidateSeed = static_cast<uint32_t>(random(lcgM));
      const uint32_t candidateC = (candidateSeed * 2u + 1u) % lcgM;
      uint32_t candidateState = candidateSeed;
      for (uint32_t step = 0; step < lcgM; ++step) {
        candidateState = (5u * candidateState + candidateC) % lcgM;
        if (candidateState < fileCount && !isPreviousIndex(candidateState)) break;
      }

      const uint32_t firstState = candidateState;
      uint8_t overlapCount = 0;

      if (previousCount > 0 && candidateSeed == prevLcgSeed) {
        // assume some overlaps for the previous seed to penalize it for selection.
        overlapCount = window > 1 ? window / 2 : 1;
      } else {
        uint8_t generatedCount = 1;
        for (uint32_t step = 0; step < lcgM && generatedCount < window && previousCount > 0; ++step) {
          candidateState = (5u * candidateState + candidateC) % lcgM;
          if (candidateState >= fileCount) continue;
          ++generatedCount;
          if (isPreviousIndex(candidateState)) ++overlapCount;
        }
      }

      if (overlapCount < bestOverlapCount) {
        bestOverlapCount = overlapCount;
        lcgSeed = candidateSeed;
        lcgState = firstState;
      }
      if (overlapCount == 0) break;
    }
    randomFileIndex = static_cast<uint16_t>(lcgState);
  } else {
    for (uint32_t attempt = 0; attempt < lcgM; ++attempt) {
      lcgState = (5u * lcgState + lcgC) % lcgM;
      if (lcgState < fileCount) {
        randomFileIndex = lcgState;
        break;
      }
    }
  }

  size_t directoryIndex = 0;
  uint16_t localIndex = randomFileIndex;
  if (randomFileIndex >= directoryCounts[0]) {
    directoryIndex = 1;
    localIndex = static_cast<uint16_t>(randomFileIndex - directoryCounts[0]);
  }

  auto dir = Storage.open(dirPaths[directoryIndex]);
  if (!dir || !dir.isDirectory()) {
    LOG_ERR("SLP", "Failed to open selected sleep image directory: %s", dirPaths[directoryIndex]);
    return false;
  }
  for (uint16_t index = 0; index <= localIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  const char* selectedDirPath = dirPaths[directoryIndex];
  selectedPath.reserve(strlen(selectedDirPath) + 1 + strlen(name.get()));
  selectedPath = selectedDirPath;
  selectedPath += "/";
  selectedPath += name.get();
  setLCGState(recentKind, lcgSeed, lcgState, lcgIndexesLeft - 1, lcgIndexesTotal);
  APP_STATE.saveToFile();
  return true;
}

bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int frameThickness = metrics.popupFrameThickness;
  const int popupY = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int popupHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.popupMarginY * 2;
  const int bandTop = std::max(0, popupY - frameThickness);
  const int bandBottom = std::min(renderer.getScreenHeight(), popupY + popupHeight + frameThickness);
  const int bandHeight = bandBottom - bandTop;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

void drawCircle(const GfxRenderer& renderer, int cx, int cy, int r, bool state) {
  int x = 0;
  int y = r;
  int d = 3 - 2 * r;
  auto plot = [&](int px, int py) {
    renderer.drawPixel(cx + px, cy + py, state);
    renderer.drawPixel(cx - px, cy + py, state);
    renderer.drawPixel(cx + px, cy - py, state);
    renderer.drawPixel(cx - px, cy - py, state);
    renderer.drawPixel(cx + py, cy + px, state);
    renderer.drawPixel(cx - py, cy + px, state);
    renderer.drawPixel(cx + py, cy - px, state);
    renderer.drawPixel(cx - py, cy - px, state);
  };
  plot(x, y);
  while (y >= x) {
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
    plot(x, y);
  }
}

void fillCircle(const GfxRenderer& renderer, int cx, int cy, int r, Color color) {
  for (int dy = -r; dy <= r; ++dy) {
    int dx = static_cast<int>(std::sqrt(r * r - dy * dy));
    if (color == Color::LightGray || color == Color::DarkGray) {
      renderer.fillRectDither(cx - dx, cy + dy, dx * 2 + 1, 1, color);
    } else {
      renderer.fillRect(cx - dx, cy + dy, dx * 2 + 1, 1, color == Color::Black);
    }
  }
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    // Quick Resume keeps the current frame as-is, so the driver's inversion
    // state stays too: a night-mode page sleeps in night polarity, and the
    // moon icon inverts with it at transfer like any other draw.
    return renderLastScreenSleepScreen();
  }

  const bool frameWasInverted = display.isInverted();

  // The remaining sleep screens draw fresh content in normal polarity. This
  // activity draws directly from onEnter (outside ActivityManager's
  // per-render polarity resolution), so clear any inversion left over from a
  // night-mode reader render.
  display.setInverted(false);

  const uint8_t sleepScreen = SETTINGS.sleepScreen;
  if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM ||
      sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT) {
    // Transparent mode retains the current framebuffer. Materialize any
    // output-level inversion first so the retained content keeps its visible
    // polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    drawSleepPopupPreservingFrame(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releaseSdFontCachesForDecode(renderer);
    if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT) {
      return renderCoverSleepScreen();
    }
    return renderTransparentCustomSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ART_JAPANESE):
      return renderJapaneseArtSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ART_SPACE):
      return renderSpaceArtSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ART_MOUNTAIN):
      return renderMountainArtSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ART_BOOKSHELF):
      return renderBookshelfArtSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::ART_GEOMETRIC):
      return renderGeometricArtSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

bool SleepActivity::renderCustomSleepBmp(const std::string& path) const {
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  Bitmap source(file);
  if (source.parseHeaders() != BmpReaderError::Ok) return false;
  LOG_DBG("SLP", "Loading: %s", path.c_str());

  const bool thresholds = useOriginalCoverThresholds(renderer);
  const bool screenReady = source.getWidth() == renderer.getScreenWidth() &&
                           source.getHeight() == renderer.getScreenHeight() && source.getBpp() <= 2;
  if (!screenReady) {
    // Like book covers: scale and dither once into a cached screen-sized BMP, then draw it without dithering.
    const bool crop = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;
    // Same-sized BMPs share a byte count, so the key also needs the modification time.
    const std::string key = path + '|' + std::to_string(file.size()) + '|' + std::to_string(file.modificationTime()) +
                            '|' + std::to_string(renderer.getScreenWidth()) + 'x' +
                            std::to_string(renderer.getScreenHeight()) + (crop ? "|c" : "|f") +
                            (thresholds ? "|t" : "");
    const std::string cachePath = "/.jpink/sleep_" + std::to_string(std::hash<std::string>{}(key)) + ".bmp";
    if (!Storage.exists(cachePath.c_str())) {
      const std::string tmpPath = cachePath + ".tmp";
      HalFile out;
      bool ok = Storage.openFileForWrite("SLP", tmpPath, out) &&
                BmpToBmpConverter::bmpFileToBmpStream(file, out, crop, thresholds);
      if (out.isOpen()) ok = out.close() && ok;
      ok = ok && Storage.rename(tmpPath.c_str(), cachePath.c_str());
      if (!ok) Storage.remove(tmpPath.c_str());
    }
    HalFile cached;
    if (Storage.openFileForRead("SLP", cachePath, cached)) {
      Bitmap bitmap(cached);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        return true;
      }
      cached.close();
      Storage.remove(cachePath.c_str());
    }
    LOG_ERR("SLP", "Sleep image conversion failed; drawing %s directly", path.c_str());
  }

  Bitmap bitmap(file, true, thresholds);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  renderBitmapSleepScreen(bitmap);
  return true;
}

void SleepActivity::renderCustomSleepScreen() const {
  if (SETTINGS.customSleepPath[0] != '\0' && Storage.exists(SETTINGS.customSleepPath)) {
    if (renderCustomSleepBmp(SETTINGS.customSleepPath)) return;
  }

  // /sleep.bmp on the SD root takes priority over both custom sleep directories.
  if (renderCustomSleepBmp("/sleep.bmp")) return;

  std::string selectedPath;
  selectRandomSleepFile("/.sleep", "/sleep", SleepRecentKind::Standard, selectedPath);
  if (!selectedPath.empty()) {
    delay(100);
    if (renderCustomSleepBmp(selectedPath)) return;
  }

  renderDefaultSleepScreen();
}

void SleepActivity::drawCustomLogoArt(GfxRenderer& renderer, int cx, int cy, bool dark) {
  // Check if a custom user logo exists on SD card (/sleep/logo.bmp, /logo.bmp, /.sleep/logo.bmp)
  HalFile logoFile;
  if (Storage.openFileForRead("SLP", "/sleep/logo.bmp", logoFile) ||
      Storage.openFileForRead("SLP", "/logo.bmp", logoFile) ||
      Storage.openFileForRead("SLP", "/.sleep/logo.bmp", logoFile)) {
    Bitmap logoBmp(logoFile);
    if (logoBmp.parseHeaders() == BmpReaderError::Ok) {
      renderer.drawBitmap(logoBmp, cx - logoBmp.getWidth() / 2, cy - logoBmp.getHeight() / 2, logoBmp.getWidth(),
                          logoBmp.getHeight());
      return;
    }
  }

  // Draw custom crafted aesthetic e-reader emblem (120x120 bounds centered at cx, cy)
  // 1. Crescent Moon
  int moonX = cx;
  int moonY = cy - 28;
  fillCircle(renderer, moonX, moonY, 20, Color::Black);
  fillCircle(renderer, moonX + 7, moonY - 4, 17, Color::White);

  // 2. Stars
  int s1x = cx + 38, s1y = cy - 38;
  renderer.drawLine(s1x - 6, s1y, s1x + 6, s1y);
  renderer.drawLine(s1x, s1y - 6, s1x, s1y + 6);
  renderer.fillRect(s1x - 1, s1y - 1, 3, 3, true);

  int s2x = cx - 36, s2y = cy - 42;
  renderer.drawLine(s2x - 4, s2y, s2x + 4, s2y);
  renderer.drawLine(s2x, s2y - 4, s2x, s2y + 4);

  renderer.fillRect(cx + 22, cy - 54, 2, 2, true);
  renderer.fillRect(cx - 20, cy - 20, 2, 2, true);

  // 3. Open Book with curved pages
  int bookTop = cy + 2;
  int bookH = 34;
  int bookHalfW = 48;
  renderer.fillRoundedRect(cx - bookHalfW, bookTop, bookHalfW * 2, bookH, 5, Color::White);
  renderer.drawRoundedRect(cx - bookHalfW, bookTop, bookHalfW * 2, bookH, 1, 5, true);
  renderer.drawRoundedRect(cx - bookHalfW + 1, bookTop + 1, bookHalfW * 2 - 2, bookH - 2, 1, 4, true);

  // Center spine divider
  renderer.drawLine(cx, bookTop, cx, bookTop + bookH);
  renderer.drawLine(cx - 1, bookTop, cx - 1, bookTop + bookH);

  // Decorative text lines on pages
  for (int row = 0; row < 3; ++row) {
    int ly = bookTop + 8 + row * 8;
    renderer.drawLine(cx - 38, ly, cx - 8, ly);
    renderer.drawLine(cx + 8, ly, cx + 38, ly);
  }

  // Bookmark ribbon hanging from center
  int ribbonX = cx + 5;
  int ribbonTop = bookTop + bookH - 4;
  int ribbonH = 16;
  renderer.fillRect(ribbonX, ribbonTop, 8, ribbonH, true);
  renderer.drawLine(ribbonX, ribbonTop + ribbonH, ribbonX + 4, ribbonTop + ribbonH - 4);
  renderer.drawLine(ribbonX + 4, ribbonTop + ribbonH - 4, ribbonX + 8, ribbonTop + ribbonH);

  // Sakura petal accent on left
  fillCircle(renderer, cx - 40, bookTop + bookH + 4, 3, Color::LightGray);
  drawCircle(renderer, cx - 40, bookTop + bookH + 4, 3, true);
}

void SleepActivity::renderLogoSleepScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  int centerY = pageHeight / 2 - 25;
  drawCustomLogoArt(renderer, pageWidth / 2, centerY, dark);

  // Decorative divider
  int divY = centerY + 62;
  renderer.drawLine(pageWidth / 2 - 70, divY, pageWidth / 2 - 14, divY);
  renderer.fillRect(pageWidth / 2 - 3, divY - 3, 7, 7, true);
  renderer.drawLine(pageWidth / 2 + 14, divY, pageWidth / 2 + 70, divY);

  // Typography
  renderer.drawCenteredText(UI_12_FONT_ID, divY + 16, "JPink", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, divY + 44, "(-.-)zzZ", true);
  renderer.drawCenteredText(SMALL_FONT_ID, divY + 70, tr(STR_SLEEPING));

  if (dark) {
    renderer.invertScreen();
  }
}

void SleepActivity::renderJapaneseArtScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  renderer.clearScreen();

  // 1. Radiant Sun / Moon
  fillCircle(renderer, cx, 230, 52, Color::LightGray);
  drawCircle(renderer, cx, 230, 52, true);

  // 2. Mount Fuji Silhouette in background
  renderer.drawLine(cx - 30, 310, 20, 510);
  renderer.drawLine(cx - 31, 310, 19, 510);
  renderer.drawLine(cx + 30, 310, pageWidth - 20, 510);
  renderer.drawLine(cx + 31, 310, pageWidth - 19, 510);
  renderer.drawLine(cx - 30, 310, cx + 30, 310);
  for (int i = 0; i < 5; ++i) {
    int zx = cx - 25 + i * 12;
    renderer.drawLine(zx, 324, zx + 6, 332);
    renderer.drawLine(zx + 6, 332, zx + 12, 324);
  }

  // 3. Water line with gentle horizontal wave ripples
  int waterY = 500;
  renderer.drawLine(0, waterY, pageWidth, waterY);
  renderer.drawLine(0, waterY + 1, pageWidth, waterY + 1);
  for (int r = 0; r < 5; ++r) {
    int wy = waterY + 16 + r * 14;
    int len = 80 - r * 10;
    renderer.drawLine(cx - len, wy, cx + len, wy);
    renderer.drawLine(cx - len - 40, wy + 7, cx - len + 10, wy + 7);
    renderer.drawLine(cx + len - 10, wy + 7, cx + len + 40, wy + 7);
  }

  // 4. Torii Gate in Foreground
  int toriiY = 405;
  int kasagiW = 150;
  renderer.fillRoundedRect(cx - kasagiW / 2, toriiY, kasagiW, 9, 3, Color::Black);
  renderer.drawRect(cx - kasagiW / 2, toriiY, kasagiW, 9, true);
  renderer.drawLine(cx - kasagiW / 2 - 4, toriiY - 2, cx - kasagiW / 2, toriiY + 4);
  renderer.drawLine(cx + kasagiW / 2 + 4, toriiY - 2, cx + kasagiW / 2, toriiY + 4);

  int nukiY = toriiY + 22;
  int nukiW = 126;
  renderer.fillRect(cx - nukiW / 2, nukiY, nukiW, 6, true);

  renderer.fillRect(cx - 7, toriiY + 9, 14, 14, true);

  int pillarDist = 38;
  int pillarH = 110;
  renderer.fillRect(cx - pillarDist - 5, toriiY + 9, 9, pillarH, true);
  renderer.fillRect(cx + pillarDist - 4, toriiY + 9, 9, pillarH, true);

  renderer.fillRect(cx - pillarDist - 7, toriiY + pillarH - 4, 13, 8, true);
  renderer.fillRect(cx + pillarDist - 6, toriiY + pillarH - 4, 13, 8, true);

  // 5. Hanging Sakura Branch from top right
  renderer.drawLine(pageWidth, 90, cx + 80, 160);
  renderer.drawLine(cx + 120, 135, cx + 90, 195);
  auto drawBlossom = [&](int bx, int by) {
    fillCircle(renderer, bx, by, 6, Color::LightGray);
    drawCircle(renderer, bx, by, 6, true);
    fillCircle(renderer, bx, by, 2, Color::Black);
  };
  drawBlossom(cx + 82, 165);
  drawBlossom(cx + 125, 140);
  drawBlossom(cx + 95, 200);
  drawBlossom(cx + 145, 120);

  // 6. Inscription (safe strings without missing font glyphs)
  auto currentLang = I18n::getInstance().getLanguage();
  const char* quietMsg = "Quiet rest (-.-)zzZ";
  if (currentLang == Language::RU) {
    quietMsg = "Тихий сон (-.-)zzZ";
  } else if (currentLang == Language::JA) {
    quietMsg = "おやすみなさい (-.-)zzZ";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, quietMsg, true);
  std::string sleepText = std::string("JPink") + " • " + std::string(tr(STR_SLEEPING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 92, sleepText.c_str());

  if (dark) {
    renderer.invertScreen();
  }
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  renderLogoSleepScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderJapaneseArtSleepScreen() const {
  renderJapaneseArtScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderSpaceArtScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  renderer.clearScreen();

  // 1. Crescent Moon with craters
  const int moonX = cx - 60;
  const int moonY = 220;
  const int moonR = 52;
  fillCircle(renderer, moonX, moonY, moonR, Color::Black);
  fillCircle(renderer, moonX + 18, moonY - 10, moonR - 4, Color::White);
  drawCircle(renderer, moonX, moonY, moonR, true);
  drawCircle(renderer, moonX - 22, moonY + 8, 5, true);
  drawCircle(renderer, moonX - 30, moonY - 14, 4, true);
  drawCircle(renderer, moonX - 12, moonY + 26, 6, true);

  // 2. Saturn with tilted rings
  const int satX = cx + 110;
  const int satY = 300;
  fillCircle(renderer, satX, satY, 22, Color::LightGray);
  drawCircle(renderer, satX, satY, 22, true);
  for (int ro = -2; ro <= 2; ++ro) {
    renderer.drawLine(satX - 52, satY + 16 + ro, satX + 52, satY - 16 + ro);
  }
  renderer.drawLine(satX - 60, satY + 18, satX + 60, satY - 18);

  // 3. Constellation Ursa Major (Big Dipper)
  const int um[7][2] = {
      {80, 130}, {125, 150}, {160, 170}, {195, 205},
      {240, 215}, {250, 260}, {200, 245}
  };
  for (int i = 0; i < 6; ++i) {
    renderer.drawLine(um[i][0], um[i][1], um[i + 1][0], um[i + 1][1]);
  }
  renderer.drawLine(um[6][0], um[6][1], um[3][0], um[3][1]);
  for (int i = 0; i < 7; ++i) {
    fillCircle(renderer, um[i][0], um[i][1], 3, Color::Black);
  }

  // 4. Cassiopeia (W)
  const int cas[5][2] = {
      {cx + 80, 110}, {cx + 105, 135}, {cx + 130, 115}, {cx + 155, 140}, {cx + 185, 120}
  };
  for (int i = 0; i < 4; ++i) {
    renderer.drawLine(cas[i][0], cas[i][1], cas[i + 1][0], cas[i + 1][1]);
  }
  for (int i = 0; i < 5; ++i) {
    fillCircle(renderer, cas[i][0], cas[i][1], 3, Color::Black);
  }

  // 5. Sparkling diamond stars
  auto drawSparkle = [&](int sx, int sy, int r) {
    renderer.drawLine(sx - r, sy, sx + r, sy);
    renderer.drawLine(sx, sy - r, sx, sy + r);
    renderer.drawLine(sx - r / 2, sy - r / 2, sx + r / 2, sy + r / 2);
    renderer.drawLine(sx - r / 2, sy + r / 2, sx + r / 2, sy - r / 2);
    fillCircle(renderer, sx, sy, 1, Color::Black);
  };
  drawSparkle(cx, 160, 7);
  drawSparkle(cx - 140, 280, 8);
  drawSparkle(cx + 40, 240, 6);
  drawSparkle(cx - 80, 360, 7);
  drawSparkle(cx + 150, 210, 5);
  drawSparkle(cx + 50, 370, 7);
  drawSparkle(90, 420, 6);
  drawSparkle(cx + 140, 430, 6);

  // Tiny scattered stars
  const int dots[][2] = {
      {50, 180}, {70, 240}, {140, 110}, {210, 140}, {290, 180},
      {340, 250}, {390, 170}, {430, 230}, {60, 320}, {170, 330},
      {270, 340}, {330, 360}, {410, 340}, {120, 460}, {320, 460}
  };
  for (const auto& dot : dots) {
    fillCircle(renderer, dot[0], dot[1], 1, Color::Black);
  }

  // 6. Shooting star
  renderer.drawLine(50, 380, 140, 435);
  renderer.drawLine(52, 380, 120, 423);
  renderer.drawLine(50, 382, 110, 417);
  fillCircle(renderer, 140, 435, 2, Color::Black);

  // 7. Earth horizon / observatory silhouette at bottom
  renderer.drawLine(0, 540, pageWidth, 540);
  renderer.drawLine(0, 541, pageWidth, 541);
  fillCircle(renderer, cx - 20, 538, 22, Color::Black);
  renderer.fillRect(cx - 42, 538, 44, 12, true);
  renderer.fillRect(cx - 22, 520, 4, 18, false);

  // Inscription
  auto currentLang = I18n::getInstance().getLanguage();
  const char* quietMsg = "Under the stars (-.-)zzZ";
  if (currentLang == Language::RU) {
    quietMsg = "Под звёздным небом (-.-)zzZ";
  } else if (currentLang == Language::JA) {
    quietMsg = "ほしのしたで (-.-)zzZ";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, quietMsg, true);
  std::string sleepText = std::string("JPink") + " • " + std::string(tr(STR_SLEEPING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 92, sleepText.c_str());

  if (dark) {
    renderer.invertScreen();
  }
}

void SleepActivity::renderSpaceArtSleepScreen() const {
  renderSpaceArtScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderMountainArtScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  renderer.clearScreen();

  // 1. Sun / Moon glowing over the peaks
  fillCircle(renderer, cx + 50, 220, 44, Color::LightGray);
  drawCircle(renderer, cx + 50, 220, 44, true);

  // 2. High migrating flock of birds
  auto drawBird = [&](int bx, int by, int span) {
    renderer.drawLine(bx - span, by + span / 2, bx, by);
    renderer.drawLine(bx, by, bx + span, by + span / 2);
  };
  drawBird(130, 160, 8);
  drawBird(160, 145, 9);
  drawBird(195, 155, 8);
  drawBird(225, 140, 10);
  drawBird(255, 150, 7);

  // 3. Majestic mountain peaks
  renderer.drawLine(120, 310, 10, 490);
  renderer.drawLine(121, 310, 11, 490);
  renderer.drawLine(120, 310, 210, 490);

  renderer.drawLine(360, 300, 270, 490);
  renderer.drawLine(360, 300, 470, 490);
  renderer.drawLine(359, 300, 469, 490);

  renderer.drawLine(cx, 245, 90, 490);
  renderer.drawLine(cx - 1, 245, 89, 490);
  renderer.drawLine(cx, 245, 390, 490);
  renderer.drawLine(cx + 1, 245, 391, 490);
  renderer.drawLine(cx, 245, cx - 10, 490);

  for (int i = 0; i < 4; ++i) {
    int zx = cx - 24 + i * 14;
    renderer.drawLine(zx, 280, zx + 7, 290);
    renderer.drawLine(zx + 7, 290, zx + 14, 280);
  }
  for (int y = 300; y < 480; y += 12) {
    int xLeft = cx - 10 + (y - 245) * 10 / 245;
    int xRight = cx + (y - 245) * 150 / 245;
    renderer.drawLine(xLeft, y, std::min(xRight, xLeft + 45), y);
  }

  // 4. Conifer / Pine forest line
  auto drawPineTree = [&](int tx, int ty, int th) {
    renderer.drawLine(tx, ty - th, tx, ty);
    renderer.drawLine(tx + 1, ty - th, tx + 1, ty);
    int tiers = 4;
    int step = th / tiers;
    for (int t = 1; t <= tiers; ++t) {
      int branchY = ty - th + t * step;
      int bw = t * 4 + 2;
      renderer.drawLine(tx - bw, branchY, tx, branchY - step / 2);
      renderer.drawLine(tx, branchY - step / 2, tx + bw, branchY);
    }
  };

  const int treeData[][3] = {
      {30, 495, 32}, {55, 495, 38}, {85, 495, 30}, {115, 495, 42},
      {150, 495, 35}, {185, 495, 40}, {220, 495, 45}, {255, 495, 38},
      {290, 495, 42}, {325, 495, 36}, {360, 495, 40}, {395, 495, 34},
      {425, 495, 38}, {455, 495, 30}
  };
  for (const auto& td : treeData) {
    drawPineTree(td[0], td[1], td[2]);
  }

  // 5. Lake shoreline & tranquil water ripples
  renderer.drawLine(0, 500, pageWidth, 500);
  renderer.drawLine(0, 501, pageWidth, 501);

  for (int r = 0; r < 6; ++r) {
    int wy = 512 + r * 10;
    int len = 100 - r * 12;
    renderer.drawLine(cx - len, wy, cx + len, wy);
    renderer.drawLine(cx - len - 45, wy + 5, cx - len + 10, wy + 5);
    renderer.drawLine(cx + len - 10, wy + 5, cx + len + 45, wy + 5);
  }

  // Inscription
  auto currentLang = I18n::getInstance().getLanguage();
  const char* quietMsg = "Mountain silence (-.-)zzZ";
  if (currentLang == Language::RU) {
    quietMsg = "Горная тишина (-.-)zzZ";
  } else if (currentLang == Language::JA) {
    quietMsg = "しずかなやま (-.-)zzZ";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, quietMsg, true);
  std::string sleepText = std::string("JPink") + " • " + std::string(tr(STR_SLEEPING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 92, sleepText.c_str());

  if (dark) {
    renderer.invertScreen();
  }
}

void SleepActivity::renderMountainArtSleepScreen() const {
  renderMountainArtScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderBookshelfArtScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  renderer.clearScreen();

  // 1. Top wooden shelf
  const int shelf1Y = 280;
  renderer.fillRect(40, shelf1Y, pageWidth - 80, 10, true);
  renderer.drawLine(35, shelf1Y + 10, 45, shelf1Y + 22);
  renderer.drawLine(pageWidth - 35, shelf1Y + 10, pageWidth - 45, shelf1Y + 22);

  // Books on top shelf
  int bx = 65;
  const int topBooks[][2] = {{18, 75}, {24, 85}, {14, 70}, {28, 90}, {20, 80}};
  for (const auto& tb : topBooks) {
    renderer.drawRect(bx, shelf1Y - tb[1], tb[0], tb[1], true);
    renderer.drawLine(bx + 2, shelf1Y - tb[1] + 12, bx + tb[0] - 2, shelf1Y - tb[1] + 12);
    renderer.drawLine(bx + 2, shelf1Y - 14, bx + tb[0] - 2, shelf1Y - 14);
    bx += tb[0] + 4;
  }

  // 3 leaning books on top shelf
  for (int i = 0; i < 3; ++i) {
    int lx = 200 + i * 18;
    renderer.drawLine(lx, shelf1Y, lx + 30, shelf1Y - 75);
    renderer.drawLine(lx + 10, shelf1Y, lx + 40, shelf1Y - 75);
    renderer.drawLine(lx + 30, shelf1Y - 75, lx + 40, shelf1Y - 75);
  }
  renderer.fillRect(265, shelf1Y - 40, 8, 40, true);

  // Globe or clock on top shelf
  fillCircle(renderer, 350, shelf1Y - 45, 24, Color::LightGray);
  drawCircle(renderer, 350, shelf1Y - 45, 24, true);
  drawCircle(renderer, 350, shelf1Y - 45, 20, true);
  renderer.drawLine(350, shelf1Y - 21, 350, shelf1Y);
  renderer.fillRect(340, shelf1Y - 4, 20, 4, true);

  // 2. Main wooden shelf
  const int shelf2Y = 510;
  renderer.fillRect(40, shelf2Y, pageWidth - 80, 12, true);
  renderer.drawLine(50, shelf2Y + 12, 65, shelf2Y + 30);
  renderer.drawLine(pageWidth - 50, shelf2Y + 12, pageWidth - 65, shelf2Y + 30);

  // Stack of 3 horizontal books on main shelf
  const int stackY = shelf2Y - 14;
  renderer.drawRect(60, stackY, 80, 14, true);
  renderer.drawRect(65, stackY - 14, 72, 14, true);
  renderer.drawRect(62, stackY - 28, 76, 14, true);

  // Steaming coffee mug
  const int mugX = 85;
  const int mugY = stackY - 28 - 26;
  renderer.fillRoundedRect(mugX, mugY, 28, 26, 3, Color::LightGray);
  renderer.drawRect(mugX, mugY, 28, 26, true);
  renderer.drawLine(mugX + 28, mugY + 4, mugX + 35, mugY + 8);
  renderer.drawLine(mugX + 35, mugY + 8, mugX + 35, mugY + 18);
  renderer.drawLine(mugX + 35, mugY + 18, mugX + 28, mugY + 22);
  renderer.drawLine(mugX + 8, mugY - 4, mugX + 11, mugY - 12);
  renderer.drawLine(mugX + 11, mugY - 12, mugX + 8, mugY - 20);
  renderer.drawLine(mugX + 18, mugY - 4, mugX + 21, mugY - 14);
  renderer.drawLine(mugX + 21, mugY - 14, mugX + 18, mugY - 24);

  // Vertical books in center
  bx = 160;
  const int mainBooks[][2] = {{22, 100}, {30, 120}, {18, 95}, {26, 110}, {32, 125}, {20, 105}};
  for (const auto& mb : mainBooks) {
    renderer.drawRect(bx, shelf2Y - mb[1], mb[0], mb[1], true);
    if (bx == 160 + 22 + 30) {
      renderer.drawLine(bx + 8, shelf2Y, bx + 8, shelf2Y + 18);
      renderer.drawLine(bx + 8, shelf2Y + 18, bx + 12, shelf2Y + 14);
      renderer.drawLine(bx + 12, shelf2Y + 14, bx + 16, shelf2Y + 18);
      renderer.drawLine(bx + 16, shelf2Y + 18, bx + 16, shelf2Y);
    }
    renderer.drawLine(bx + 2, shelf2Y - mb[1] + 16, bx + mb[0] - 2, shelf2Y - mb[1] + 16);
    renderer.drawLine(bx + 2, shelf2Y - 18, bx + mb[0] - 2, shelf2Y - 18);
    bx += mb[0] + 4;
  }

  // Potted plant on right
  const int potX = 345;
  const int potY = shelf2Y - 34;
  renderer.drawLine(potX, potY, potX + 5, potY + 34);
  renderer.drawLine(potX + 35, potY, potX + 30, potY + 34);
  renderer.drawLine(potX + 5, potY + 34, potX + 30, potY + 34);
  renderer.drawLine(potX - 2, potY, potX + 37, potY);
  renderer.drawLine(potX + 8, potY, potX - 5, shelf2Y + 35);
  renderer.drawLine(potX + 20, potY, potX + 15, shelf2Y + 45);
  auto drawLeaf = [&](int lx, int ly) {
    fillCircle(renderer, lx, ly, 4, Color::LightGray);
    drawCircle(renderer, lx, ly, 4, true);
  };
  drawLeaf(potX - 4, shelf2Y + 15);
  drawLeaf(potX - 6, shelf2Y + 30);
  drawLeaf(potX + 16, shelf2Y + 18);
  drawLeaf(potX + 14, shelf2Y + 38);

  // Inscription
  auto currentLang = I18n::getInstance().getLanguage();
  const char* quietMsg = "Time for stories (-.-)zzZ";
  if (currentLang == Language::RU) {
    quietMsg = "Время для историй (-.-)zzZ";
  } else if (currentLang == Language::JA) {
    quietMsg = "ほんのじかん (-.-)zzZ";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, quietMsg, true);
  std::string sleepText = std::string("JPink") + " • " + std::string(tr(STR_SLEEPING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 92, sleepText.c_str());

  if (dark) {
    renderer.invertScreen();
  }
}

void SleepActivity::renderBookshelfArtSleepScreen() const {
  renderBookshelfArtScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderGeometricArtScreen(GfxRenderer& renderer, bool dark) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;

  renderer.clearScreen();

  // 1. Concentric sacred geometry circles
  const int zenY = 280;
  drawCircle(renderer, cx, zenY, 130, true);
  drawCircle(renderer, cx, zenY, 126, true);
  drawCircle(renderer, cx, zenY, 80, true);
  fillCircle(renderer, cx, zenY, 40, Color::LightGray);
  drawCircle(renderer, cx, zenY, 40, true);

  // Tangent circles (Flower of Life motif)
  drawCircle(renderer, cx - 40, zenY, 40, true);
  drawCircle(renderer, cx + 40, zenY, 40, true);
  drawCircle(renderer, cx, zenY - 40, 40, true);
  drawCircle(renderer, cx, zenY + 40, 40, true);

  // Radial rays
  for (int angle = 0; angle < 8; ++angle) {
    float rad = angle * (3.14159265f / 4.0f);
    int x1 = cx + static_cast<int>(85 * std::cos(rad));
    int y1 = zenY + static_cast<int>(85 * std::sin(rad));
    int x2 = cx + static_cast<int>(122 * std::cos(rad));
    int y2 = zenY + static_cast<int>(122 * std::sin(rad));
    renderer.drawLine(x1, y1, x2, y2);
  }

  // 2. Zen Cairn (balanced stones)
  auto drawStone = [&](int sy, int sw, int sh) {
    renderer.fillRoundedRect(cx - sw / 2, sy, sw, sh, sh / 2, Color::LightGray);
    renderer.drawRect(cx - sw / 2, sy, sw, sh, true);
  };
  drawStone(445, 34, 14);
  drawStone(460, 56, 16);
  drawStone(477, 82, 19);
  drawStone(497, 114, 22);

  // 3. Sand ripples / Karesansui dry garden raked lines
  const int groundY = 522;
  renderer.drawLine(40, groundY, pageWidth - 40, groundY);
  renderer.drawLine(40, groundY + 1, pageWidth - 40, groundY + 1);

  for (int i = 1; i <= 4; ++i) {
    int gy = groundY + i * 8;
    int gw = 180 - i * 20;
    renderer.drawLine(cx - gw, gy, cx - 65, gy);
    renderer.drawLine(cx + 65, gy, cx + gw, gy);
  }

  // Inscription
  auto currentLang = I18n::getInstance().getLanguage();
  const char* quietMsg = "Harmony & peace (-.-)zzZ";
  if (currentLang == Language::RU) {
    quietMsg = "Гармония и покой (-.-)zzZ";
  } else if (currentLang == Language::JA) {
    quietMsg = "わとへいあん (-.-)zzZ";
  }
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight - 120, quietMsg, true);
  std::string sleepText = std::string("JPink") + " • " + std::string(tr(STR_SLEEPING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 92, sleepText.c_str());

  if (dark) {
    renderer.invertScreen();
  }
}

void SleepActivity::renderGeometricArtSleepScreen() const {
  renderGeometricArtScreen(renderer, SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale = (bitmap.hasGreyscale() || (preserveBackground && overlayGrayCover)) &&
                            (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                       CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
    renderer.displayBuffer(kSleepClean);
    return;
  }

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Cover + Transparent: the B/W cover is the overlay's background; the overlay renderers redraw its gray planes.
  if (overlayCover == &bitmap) {
    if (hasGreyscale && renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported()) {
      overlayGrayCover = &bitmap;
    }
    renderTransparentCustomSleepScreen();
    overlayGrayCover = nullptr;
    return;
  }

  const bool absolute = hasGreyscale && renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, hasGreyscale && !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(kSleepClean);
    return;
  }
  if (absolute) {
    cleanBeforeAbsoluteGray(renderer);
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return;
  } else if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(kSleepClean);
  }

  if (hasGreyscale) {
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (bitmap.rewindToData() != BmpReaderError::Ok) {
        ready = false;
        break;
      }
      if (preserveBackground) {
        if (!beginOverlayGrayPlane(renderer, overlayGrayCover, plane, absolute)) {
          ready = false;
          break;
        }
      } else {
        renderer.clearScreen(absolute ? 0xFF : 0x00);
        renderer.setRenderMode(plane);
      }
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
        ready = false;
        break;
      }
      if (plane == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (ready)
      renderer.displayGrayBuffer();
    else
      LOG_ERR("SLP", "Incomplete grayscale image; keeping the current display");
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog, overlayGrayCover);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  const BwFrameKeeper bwFrame(renderer, !absolute);
  if (!bwFrame.ready()) {
    renderer.displayBuffer(kSleepClean);
    return true;
  }
  if (absolute) {
    // The cover replaces the previous screen, so clean it like the BMP paths do.
    if (overlayCover) cleanBeforeAbsoluteGray(renderer);
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!beginOverlayGrayPlane(renderer, overlayGrayCover, GfxRenderer::GRAYSCALE_LSB, absolute) ||
      !converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!beginOverlayGrayPlane(renderer, overlayGrayCover, GfxRenderer::GRAYSCALE_MSB, absolute) ||
      !converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  if (overlayCover) {
    // Show the plain cover.
    const Bitmap& cover = *overlayCover;
    overlayCover = nullptr;
    if (cover.rewindToData() == BmpReaderError::Ok) return renderBitmapSleepScreen(cover);
  }
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  bool withOverlay = false;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT):
      renderNoCoverSleepScreen = &SleepActivity::renderTransparentCustomSleepScreen;
      withOverlay = true;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  const bool originalThresholds = useOriginalCoverThresholds(renderer);
  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.jpink");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasReflowableBookExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB, TXT, or Markdown file
    Epub lastEpub(APP_STATE.openEpubPath, "/.jpink");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last book");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped, originalThresholds)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped, originalThresholds);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      if (withOverlay) overlayCover = &bitmap;
      renderBitmapSleepScreen(bitmap);
      overlayCover = nullptr;
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  // Only the moon differs from the displayed frame, so a differential FAST
  // update adds it without the flashing clean pass (which sweeps the panel
  // through the inverse — a full white flash on a night-mode page).
  if (gpio.deviceIsX3()) {
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(kSleepClean);
}
