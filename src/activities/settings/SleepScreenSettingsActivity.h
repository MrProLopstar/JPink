#pragma once
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"

struct SleepScreenOption {
  std::string title;
  CrossPointSettings::SLEEP_SCREEN_MODE mode;
  std::string filePath;
};

class SleepScreenSettingsActivity final : public UiListActivity {
 public:
  explicit SleepScreenSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  std::vector<SleepScreenOption> options;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;

  bool isPreviewMode = false;
  int previewIndex = 0;

  int listCount() const override { return static_cast<int>(options.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  void scanCustomFiles();
  void applySelection(int index);
  void renderPreview(int index);
  bool isOptionActive(const SleepScreenOption& opt) const;
};
