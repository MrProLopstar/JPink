#include "ManualClockSetActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

ManualClockSetActivity::ManualClockSetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ManualClockSet", renderer, mappedInput) {}

void ManualClockSetActivity::onEnter() {
  Activity::onEnter();
  struct tm tmLocal = {};
  if (halClock.localTime(tmLocal)) {
    year = tmLocal.tm_year + 1900;
    month = tmLocal.tm_mon + 1;
    day = tmLocal.tm_mday;
    hour = tmLocal.tm_hour;
    minute = tmLocal.tm_min;
  } else {
    year = 2026;
    month = 1;
    day = 1;
    hour = 12;
    minute = 0;
  }
  if (year < 2026) year = 2026;
  focusedField = 0;
  saved = false;
  requestUpdate();
}

int ManualClockSetActivity::getMaxDaysInMonth() const {
  if (month == 2) {
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return leap ? 29 : 28;
  }
  if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
  return 31;
}

void ManualClockSetActivity::adjustField(int delta) {
  switch (focusedField) {
    case FIELD_YEAR:
      year += delta;
      if (year < 2025) year = 2025;
      if (year > 2045) year = 2045;
      break;
    case FIELD_MONTH:
      month += delta;
      if (month < 1) month = 12;
      if (month > 12) month = 1;
      break;
    case FIELD_DAY: {
      int maxD = getMaxDaysInMonth();
      day += delta;
      if (day < 1) day = maxD;
      if (day > maxD) day = 1;
      break;
    }
    case FIELD_HOUR:
      hour = (hour + delta + 24) % 24;
      break;
    case FIELD_MINUTE:
      minute = (minute + delta + 60) % 60;
      break;
    default:
      break;
  }
  int maxD = getMaxDaysInMonth();
  if (day > maxD) day = maxD;
  requestUpdate();
}

void ManualClockSetActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (saved) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    focusedField = (focusedField - 1 + FIELD_COUNT) % FIELD_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    focusedField = (focusedField + 1) % FIELD_COUNT;
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    if (focusedField == FIELD_SAVE) {
      focusedField = FIELD_MINUTE;
      requestUpdate();
    } else {
      adjustField(-1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    if (focusedField != FIELD_SAVE) {
      adjustField(1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (focusedField == FIELD_SAVE) {
      halClock.setDateTime(year, month, day, hour, minute);
      SETTINGS.clockHasBeenSynced = 1;
      SETTINGS.saveToFile();
      saved = true;
      finish();
      return;
    } else {
      focusedField = (focusedField + 1) % FIELD_COUNT;
      requestUpdate();
      return;
    }
  }

  // Touch screen support
  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect content = UITheme::getInstance().getContentArea(renderer);
    const int screenW = renderer.getScreenWidth();
    const int cardW = std::min(screenW - 32, 420);
    const int cardX = (screenW - cardW) / 2;
    const int rx = cardX + 12;
    const int rw = cardW - 24;
    const int rowH = 46;
    const int rowSpacing = 8;
    const int startY = metrics.topPadding + metrics.headerHeight + 16;

    for (int i = 0; i < FIELD_COUNT; ++i) {
      int ry = startY + i * (rowH + rowSpacing);
      if (touchY >= ry && touchY <= ry + rowH && touchX >= rx && touchX <= rx + rw) {
        if (i == FIELD_SAVE) {
          halClock.setDateTime(year, month, day, hour, minute);
          SETTINGS.clockHasBeenSynced = 1;
          SETTINGS.saveToFile();
          saved = true;
          finish();
          return;
        }
        focusedField = i;
        int valBoxW = 140;
        int valBoxX = rx + rw - valBoxW - 8;
        if (touchX >= valBoxX && touchX <= valBoxX + 45) {
          adjustField(-1);
          return;
        } else if (touchX >= valBoxX + valBoxW - 45 && touchX <= valBoxX + valBoxW) {
          adjustField(1);
          return;
        }
        requestUpdate();
        return;
      }
    }
  }
}

void ManualClockSetActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight},
                 tr(STR_CLOCK_SET_TITLE), tr(STR_CLOCK_SET_MANUAL), true);

  const int cardW = std::min(screenW - 32, 420);
  const int cardX = (screenW - cardW) / 2;
  const int rx = cardX + 12;
  const int rw = cardW - 24;
  const int rowH = 46;
  const int rowSpacing = 8;
  const int startY = metrics.topPadding + metrics.headerHeight + 16;

  const char* labels[5] = {
      tr(STR_CLOCK_YEAR),
      tr(STR_CLOCK_MONTH),
      tr(STR_CLOCK_DAY),
      tr(STR_CLOCK_HOUR),
      tr(STR_CLOCK_MINUTE)
  };

  char valBufs[5][32];
  snprintf(valBufs[0], sizeof(valBufs[0]), "%04d", year);
  snprintf(valBufs[1], sizeof(valBufs[1]), "%02d", month);
  snprintf(valBufs[2], sizeof(valBufs[2]), "%02d", day);
  snprintf(valBufs[3], sizeof(valBufs[3]), "%02d", hour);
  snprintf(valBufs[4], sizeof(valBufs[4]), "%02d", minute);

  // Draw 5 value rows
  for (int i = 0; i < 5; ++i) {
    int ry = startY + i * (rowH + rowSpacing);
    bool isFocused = (focusedField == i);

    renderer.fillRoundedRect(rx, ry, rw, rowH, 6, Color::White);
    if (isFocused) {
      renderer.fillRoundedRect(rx + 2, ry + 2, rw - 4, rowH - 4, 4, Color::LightGray);
      renderer.drawRect(rx, ry, rw, rowH, true);
      renderer.drawRect(rx + 1, ry + 1, rw - 2, rowH - 2, true);
    } else {
      renderer.drawRect(rx, ry, rw, rowH, true);
    }

    // Label
    renderer.drawText(UI_10_FONT_ID, rx + 16, ry + 13, labels[i], true, EpdFontFamily::BOLD);

    // Value box with arrows
    int valBoxW = 140;
    int valBoxX = rx + rw - valBoxW - 8;
    renderer.drawText(UI_12_FONT_ID, valBoxX + 8, ry + 11, "<", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, valBoxX + valBoxW - 18, ry + 11, ">", true, EpdFontFamily::BOLD);

    int vw = renderer.getTextWidth(UI_12_FONT_ID, valBufs[i], EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, valBoxX + (valBoxW - vw) / 2, ry + 11, valBufs[i], true, EpdFontFamily::BOLD);
  }

  // Draw Save Button (Field 5)
  int saveY = startY + 5 * (rowH + rowSpacing) + 6;
  int btnH = rowH + 4;
  bool saveFocused = (focusedField == FIELD_SAVE);

  if (saveFocused) {
    renderer.fillRoundedRect(rx, saveY, rw, btnH, 6, Color::Black);
    int tw = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_CLOCK_SAVE), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rx + (rw - tw) / 2, saveY + 13, tr(STR_CLOCK_SAVE), false, EpdFontFamily::BOLD);
  } else {
    renderer.fillRoundedRect(rx, saveY, rw, btnH, 6, Color::White);
    renderer.drawRect(rx, saveY, rw, btnH, true);
    renderer.drawRect(rx + 1, saveY + 1, rw - 2, btnH - 2, true);
    int tw = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_CLOCK_SAVE), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rx + (rw - tw) / 2, saveY + 13, tr(STR_CLOCK_SAVE), true, EpdFontFamily::BOLD);
  }

  // Button Hints
  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_SAVE), "-1", "+1");
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
