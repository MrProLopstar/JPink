#pragma once

#include "activities/Activity.h"

class ManualClockSetActivity final : public Activity {
 public:
  explicit ManualClockSetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Field {
    FIELD_YEAR = 0,
    FIELD_MONTH,
    FIELD_DAY,
    FIELD_HOUR,
    FIELD_MINUTE,
    FIELD_SAVE,
    FIELD_COUNT
  };

  int year = 2026;
  int month = 1;
  int day = 1;
  int hour = 12;
  int minute = 0;
  int focusedField = 0;
  bool saved = false;

  void adjustField(int delta);
  int getMaxDaysInMonth() const;
};
