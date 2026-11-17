#include "SleepScreenSettingsActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalStorage.h>

#include "CrossPointState.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

SleepScreenSettingsActivity::SleepScreenSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("SleepScreenSettings", renderer, mappedInput) {}

void SleepScreenSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  options.clear();

  // 1. Built-in styles & modes
  options.push_back({tr(STR_LOGO_CUSTOM_DARK), CrossPointSettings::SLEEP_SCREEN_MODE::DARK, ""});
  options.push_back({tr(STR_LOGO_CUSTOM_LIGHT), CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT, ""});
  options.push_back({tr(STR_ART_JAPANESE), CrossPointSettings::SLEEP_SCREEN_MODE::ART_JAPANESE, ""});
  options.push_back({tr(STR_ART_SPACE), CrossPointSettings::SLEEP_SCREEN_MODE::ART_SPACE, ""});
  options.push_back({tr(STR_ART_MOUNTAIN), CrossPointSettings::SLEEP_SCREEN_MODE::ART_MOUNTAIN, ""});
  options.push_back({tr(STR_ART_BOOKSHELF), CrossPointSettings::SLEEP_SCREEN_MODE::ART_BOOKSHELF, ""});
  options.push_back({tr(STR_ART_GEOMETRIC), CrossPointSettings::SLEEP_SCREEN_MODE::ART_GEOMETRIC, ""});
  options.push_back({tr(STR_COVER), CrossPointSettings::SLEEP_SCREEN_MODE::COVER, ""});
  options.push_back({tr(STR_COVER_CUSTOM), CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM, ""});
  options.push_back({tr(STR_QUICK_RESUME), CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME, ""});
  options.push_back({tr(STR_RANDOM_SLEEP), CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM, ""});
  options.push_back({tr(STR_NONE_OPT), CrossPointSettings::SLEEP_SCREEN_MODE::BLANK, ""});

  // 2. Scan SD card for custom sleep wallpapers
  scanCustomFiles();

  // Find currently active option to set initial selection
  int activeIdx = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    if (isOptionActive(options[i])) {
      activeIdx = static_cast<int>(i);
      break;
    }
  }
  nav.selected = activeIdx;
  moveSelectionTo(activeIdx);
}

bool SleepScreenSettingsActivity::isOptionActive(const SleepScreenOption& opt) const {
  if (SETTINGS.sleepScreen != opt.mode) return false;
  if (opt.mode == CrossPointSettings::CUSTOM) {
    if (opt.filePath.empty()) {
      return SETTINGS.customSleepPath[0] == '\0';
    }
    return opt.filePath == SETTINGS.customSleepPath;
  }
  return true;
}

void SleepScreenSettingsActivity::scanCustomFiles() {
  const char* dirs[] = {"/sleep", "/.sleep", "/wallpapers"};
  for (const char* dirPath : dirs) {
    auto dir = Storage.open(dirPath);
    if (!dir || !dir.isDirectory()) continue;

    char filename[64];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(filename, sizeof(filename));
      if (filename[0] == '\0' || filename[0] == '.') continue;

      if (FsHelpers::hasBmpExtension(std::string_view(filename)) ||
          FsHelpers::hasPngExtension(std::string_view(filename)) ||
          FsHelpers::hasJpgExtension(std::string_view(filename))) {
        std::string fullPath = std::string(dirPath) + "/" + filename;
        bool alreadyAdded = false;
        for (const auto& opt : options) {
          if (opt.filePath == fullPath) {
            alreadyAdded = true;
            break;
          }
        }
        if (!alreadyAdded) {
          options.push_back({std::string(filename), CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM, fullPath});
        }
      }
    }
  }
}

void SleepScreenSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowValues_.assign(options.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(options.size());

  for (size_t i = 0; i < options.size(); i++) {
    fui::ListItem item;
    item.label = options[i].title.c_str();
    item.actionValue = static_cast<int16_t>(i);
    if (isOptionActive(options[i])) {
      rowValues_[i] = "[V]";
      item.value = rowValues_[i].c_str();
    }
    rowItems_.push_back(item);
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  syncListViewport(screen, props);
  screen.list(props);
}

void SleepScreenSettingsActivity::activateIndex(int index) {
  if (index < 0 || index >= static_cast<int>(options.size())) return;
  app.clearTapFlash();
  previewIndex = index;
  isPreviewMode = true;
  requestUpdate();
}

bool SleepScreenSettingsActivity::handleCustomInput() {
  if (!isPreviewMode) return false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    isPreviewMode = false;
    requestUpdate();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelection(previewIndex);
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (previewIndex > 0) {
      previewIndex--;
    } else {
      previewIndex = static_cast<int>(options.size()) - 1;
    }
    requestUpdate();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    previewIndex = (previewIndex + 1) % static_cast<int>(options.size());
    requestUpdate();
    return true;
  }

  return true;
}

void SleepScreenSettingsActivity::applySelection(int index) {
  if (index < 0 || index >= static_cast<int>(options.size())) return;
  const auto& opt = options[index];

  SETTINGS.sleepScreen = opt.mode;
  if (!opt.filePath.empty()) {
    strncpy(SETTINGS.customSleepPath, opt.filePath.c_str(), sizeof(SETTINGS.customSleepPath) - 1);
    SETTINGS.customSleepPath[sizeof(SETTINGS.customSleepPath) - 1] = '\0';
  } else {
    SETTINGS.customSleepPath[0] = '\0';
  }

  SETTINGS.saveToFile();
  GUI.drawPopup(renderer, tr(STR_DONE));
  delay(500);
  finish();
}

void SleepScreenSettingsActivity::renderPreview(int index) {
  if (index < 0 || index >= static_cast<int>(options.size())) return;
  const auto& opt = options[index];
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  switch (opt.mode) {
    case CrossPointSettings::SLEEP_SCREEN_MODE::DARK:
      SleepActivity::renderLogoSleepScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT:
      SleepActivity::renderLogoSleepScreen(renderer, false);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::ART_JAPANESE:
      SleepActivity::renderJapaneseArtScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::ART_SPACE:
      SleepActivity::renderSpaceArtScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::ART_MOUNTAIN:
      SleepActivity::renderMountainArtScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::ART_BOOKSHELF:
      SleepActivity::renderBookshelfArtScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::ART_GEOMETRIC:
      SleepActivity::renderGeometricArtScreen(renderer, true);
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::BLANK:
      renderer.clearScreen();
      break;

    case CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME: {
      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 30, tr(STR_QUICK_RESUME), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_SLEEP_LAST_OPEN_PAGE), true);
      break;
    }

    case CrossPointSettings::SLEEP_SCREEN_MODE::COVER:
    case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM: {
      renderer.clearScreen();
      renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 40, tr(STR_COVER), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SLEEP_READING_COVER), true);
      renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 30, tr(STR_SLEEP_WHEN_READING));
      break;
    }

    case CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM: {
      if (!opt.filePath.empty()) {
        HalFile file;
        if (Storage.openFileForRead("SLP", opt.filePath, file)) {
          Bitmap bmp(file);
          if (bmp.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bmp, (pageWidth - bmp.getWidth()) / 2, (pageHeight - bmp.getHeight()) / 2,
                                bmp.getWidth(), bmp.getHeight());
          } else {
            renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, opt.title.c_str(), true);
          }
        }
      } else {
        renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 30, tr(STR_RANDOM_SLEEP), true, EpdFontFamily::BOLD);
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_SLEEP_RANDOM_SLEEP), true);
      }
      break;
    }

    default:
      SleepActivity::renderLogoSleepScreen(renderer, true);
      break;
  }

  // Floating bottom action banner
  const int barH = 38;
  const int barY = pageHeight - barH;
  renderer.fillRect(0, barY, pageWidth, barH, true);
  renderer.drawCenteredText(UI_10_FONT_ID, barY + 10, tr(STR_PREVIEW_HINT), false, EpdFontFamily::BOLD);
}

void SleepScreenSettingsActivity::render(RenderLock&& lock) {
  if (isPreviewMode) {
    renderPreview(previewIndex);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  UiListActivity::render(std::move(lock));
}
