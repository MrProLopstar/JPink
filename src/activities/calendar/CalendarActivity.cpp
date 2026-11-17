#include "CalendarActivity.h"

#include <FsHelpers.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include "I18nKeys.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "activities/network/WifiSelectionActivity.h"
#include "HapticFeedback.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

static const char* const MONTH_NAMES_RU[] = {
    "", "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
    "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"
};
static const char* const MONTH_NAMES_EN[] = {
    "", "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const char* const MONTH_NAMES_JA[] = {
    "", "1月", "2月", "3月", "4月", "5月", "6月",
    "7月", "8月", "9月", "10月", "11月", "12月"
};

static const char* const DOW_RU[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
static const char* const DOW_EN[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
static const char* const DOW_JA[] = {"月", "火", "水", "木", "金", "土", "日"};

static const char* const DOW_FULL_RU[] = {
    "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье"
};
static const char* const DOW_FULL_EN[] = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"
};
static const char* const DOW_FULL_JA[] = {
    "月曜日", "火曜日", "水曜日", "木曜日", "金曜日", "土曜日", "日曜日"
};

const char* getMonthName(int month) {
  if (month < 1 || month > 12) return "";
  auto lang = I18n::getInstance().getLanguage();
  if (lang == Language::RU) return MONTH_NAMES_RU[month];
  if (lang == Language::JA) return MONTH_NAMES_JA[month];
  return MONTH_NAMES_EN[month];
}

const char* getDayOfWeekShort(int dow) {
  if (dow < 0 || dow > 6) return "";
  auto lang = I18n::getInstance().getLanguage();
  if (lang == Language::RU) return DOW_RU[dow];
  if (lang == Language::JA) return DOW_JA[dow];
  return DOW_EN[dow];
}

const char* getDayOfWeekFull(int dow) {
  if (dow < 0 || dow > 6) return "";
  auto lang = I18n::getInstance().getLanguage();
  if (lang == Language::RU) return DOW_FULL_RU[dow];
  if (lang == Language::JA) return DOW_FULL_JA[dow];
  return DOW_FULL_EN[dow];
}

std::string unescapeIcsString(const std::string& str) {
  std::string result;
  result.reserve(str.size());
  for (size_t i = 0; i < str.size(); ++i) {
    if (str[i] == '\\' && i + 1 < str.size()) {
      char next = str[i + 1];
      if (next == 'n' || next == 'N') {
        result += ' ';
        ++i;
      } else if (next == ',' || next == ';' || next == '\\') {
        result += next;
        ++i;
      } else {
        result += str[i];
      }
    } else {
      result += str[i];
    }
  }
  return result;
}

}  // namespace

CalendarActivity::CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Calendar", renderer, mappedInput) {}

bool CalendarActivity::isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int CalendarActivity::getDaysInMonth(int y, int m) {
  static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  if (m >= 1 && m <= 12) return days[m];
  return 30;
}

int CalendarActivity::getFirstDayOfWeek(int y, int m) {
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = 1;
  mktime(&t);
  return (t.tm_wday + 6) % 7;  // 0 = Monday ... 6 = Sunday
}

void CalendarActivity::initCurrentDate() {
  todayYear = 2026;
  todayMonth = 1;
  todayDay = 1;

  struct tm tmLocal = {};
  if (halClock.localTime(tmLocal)) {
    todayYear = tmLocal.tm_year + 1900;
    todayMonth = tmLocal.tm_mon + 1;
    todayDay = tmLocal.tm_mday;
  }

  viewedYear = todayYear;
  viewedMonth = todayMonth;
  selectedDay = todayDay;
}

void CalendarActivity::onEnter() {
  Activity::onEnter();
  needsCleanRefresh = true;
  initCurrentDate();
  loadGoogleSyncConfig();
  loadEvents();

  // If WiFi is connected, automatically sync time and calendar if needed
  if (WiFi.status() == WL_CONNECTED) {
    halClock.syncFromNTP();
    initCurrentDate();
  }

  requestUpdate();
}

void CalendarActivity::onExit() {
  Activity::onExit();
}

void CalendarActivity::loadGoogleSyncConfig() {
  googleSyncUrl.clear();
  const char* configPaths[] = {"/calendar/google_sync.json", "/sdcard/calendar/google_sync.json"};
  for (const char* path : configPaths) {
    if (Storage.exists(path)) {
      std::string content = Storage.readFile(path).c_str();
      // Simple extraction of "url": "..."
      size_t urlPos = content.find("\"url\"");
      if (urlPos != std::string::npos) {
        size_t colon = content.find(':', urlPos);
        if (colon != std::string::npos) {
          size_t q1 = content.find('"', colon);
          if (q1 != std::string::npos) {
            size_t q2 = content.find('"', q1 + 1);
            if (q2 != std::string::npos) {
              googleSyncUrl = content.substr(q1 + 1, q2 - q1 - 1);
              LOG_INF("CAL", "Loaded Google Sync URL: %s", googleSyncUrl.c_str());
              return;
            }
          }
        }
      }
    }
  }
}

void CalendarActivity::loadEvents() {
  events.clear();
  const char* icsPaths[] = {
      "/calendar/events.ics", "/sdcard/calendar/events.ics",
      "/calendar/google_calendar.ics", "/sdcard/calendar/google_calendar.ics"
  };

  bool found = false;
  for (const char* path : icsPaths) {
    if (Storage.exists(path)) {
      parseIcsFile(path);
      found = true;
      break;
    }
  }

  if (!found) {
    createSampleEventsIfEmpty();
  }

  // Sort events by date and time
  std::sort(events.begin(), events.end(), [](const CalendarEvent& a, const CalendarEvent& b) {
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    if (a.day != b.day) return a.day < b.day;
    if (a.isAllDay != b.isAllDay) return a.isAllDay;
    if (a.startHour != b.startHour) return a.startHour < b.startHour;
    return a.startMin < b.startMin;
  });
}

void CalendarActivity::createSampleEventsIfEmpty() {
  if (!Storage.exists("/calendar")) {
    Storage.mkdir("/calendar");
  }

  // Write starter google_sync.json template if not present
  if (!Storage.exists("/calendar/google_sync.json")) {
    const char* starterConfig =
        "{\n"
        "  \"url\": \"\",\n"
        "  \"description\": \"Insert your secret Google Calendar iCal (.ics) URL above to enable sync\"\n"
        "}\n";
    Storage.writeFile("/calendar/google_sync.json", starterConfig);
  }
}

void CalendarActivity::parseIcsFile(const std::string& path) {
  std::string content = Storage.readFile(path.c_str()).c_str();
  if (content.empty()) return;

  // Split into unfolded lines
  std::vector<std::string> lines;
  lines.reserve(256);
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    std::string line;
    if (end == std::string::npos) {
      line = content.substr(start);
      start = content.size();
    } else {
      line = content.substr(start, end - start);
      start = end + 1;
    }
    // Remove carriage return
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      if (!lines.empty()) {
        lines.back() += line.substr(1);
      }
    } else if (!line.empty()) {
      lines.push_back(line);
    }
  }

  bool inEvent = false;
  CalendarEvent curEvent;

  for (const auto& line : lines) {
    if (line == "BEGIN:VEVENT") {
      inEvent = true;
      curEvent = CalendarEvent();
      continue;
    }
    if (line == "END:VEVENT") {
      if (inEvent && curEvent.year > 0 && curEvent.month > 0 && curEvent.day > 0) {
        events.push_back(curEvent);
      }
      inEvent = false;
      continue;
    }
    if (!inEvent) continue;

    if (line.rfind("SUMMARY", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        curEvent.summary = unescapeIcsString(line.substr(colon + 1));
      }
    } else if (line.rfind("LOCATION", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        curEvent.location = unescapeIcsString(line.substr(colon + 1));
      }
    } else if (line.rfind("DESCRIPTION", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        curEvent.description = unescapeIcsString(line.substr(colon + 1));
      }
    } else if (line.rfind("DTSTART", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string val = line.substr(colon + 1);
        if (val.size() >= 8) {
          curEvent.year = atoi(val.substr(0, 4).c_str());
          curEvent.month = atoi(val.substr(4, 2).c_str());
          curEvent.day = atoi(val.substr(6, 2).c_str());
          size_t tPos = val.find('T');
          if (tPos != std::string::npos && val.size() >= tPos + 5) {
            curEvent.startHour = atoi(val.substr(tPos + 1, 2).c_str());
            curEvent.startMin = atoi(val.substr(tPos + 3, 2).c_str());
            curEvent.isAllDay = false;
          } else {
            curEvent.isAllDay = true;
          }
        }
      }
    } else if (line.rfind("DTEND", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string val = line.substr(colon + 1);
        size_t tPos = val.find('T');
        if (tPos != std::string::npos && val.size() >= tPos + 5) {
          curEvent.endHour = atoi(val.substr(tPos + 1, 2).c_str());
          curEvent.endMin = atoi(val.substr(tPos + 3, 2).c_str());
        }
      }
    }
  }
}

std::vector<const CalendarEvent*> CalendarActivity::getEventsForDate(int y, int m, int d) const {
  std::vector<const CalendarEvent*> result;
  for (const auto& ev : events) {
    if (ev.year == y && ev.month == m && ev.day == d) {
      result.push_back(&ev);
    }
  }
  return result;
}

bool CalendarActivity::hasEventsOnDate(int y, int m, int d) const {
  for (const auto& ev : events) {
    if (ev.year == y && ev.month == m && ev.day == d) return true;
  }
  return false;
}

int CalendarActivity::countEventsForMonth(int y, int m) const {
  int count = 0;
  for (const auto& ev : events) {
    if (ev.year == y && ev.month == m) count++;
  }
  return count;
}

void CalendarActivity::prevMonth() {
  if (viewedMonth == 1) {
    viewedMonth = 12;
    viewedYear--;
  } else {
    viewedMonth--;
  }
  int maxDay = getDaysInMonth(viewedYear, viewedMonth);
  if (selectedDay > maxDay) selectedDay = maxDay;
  needsCleanRefresh = true;
  requestUpdate();
}

void CalendarActivity::nextMonth() {
  if (viewedMonth == 12) {
    viewedMonth = 1;
    viewedYear++;
  } else {
    viewedMonth++;
  }
  int maxDay = getDaysInMonth(viewedYear, viewedMonth);
  if (selectedDay > maxDay) selectedDay = maxDay;
  needsCleanRefresh = true;
  requestUpdate();
}

void CalendarActivity::prevDay() {
  if (selectedDay > 1) {
    selectedDay--;
  } else {
    prevMonth();
    selectedDay = getDaysInMonth(viewedYear, viewedMonth);
  }
  needsCleanRefresh = true;
  requestUpdate();
}

void CalendarActivity::nextDay() {
  int maxDay = getDaysInMonth(viewedYear, viewedMonth);
  if (selectedDay < maxDay) {
    selectedDay++;
  } else {
    nextMonth();
    selectedDay = 1;
  }
  needsCleanRefresh = true;
  requestUpdate();
}

void CalendarActivity::jumpToToday() {
  viewedYear = todayYear;
  viewedMonth = todayMonth;
  selectedDay = todayDay;
  needsCleanRefresh = true;
  requestUpdate();
}

void CalendarActivity::syncGoogleCalendar() {
  loadGoogleSyncConfig();
  if (googleSyncUrl.empty()) {
    statusNotification = tr(STR_CALENDAR_GOOGLE_SETUP);
    statusNotificationUntilMs = millis() + 4000;
    requestUpdate();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    launchWifiSelection();
    return;
  }

  isSyncing = true;
  statusNotification = tr(STR_CALENDAR_SYNCING);
  requestUpdate();

  HttpDownloader::DownloadError err = HttpDownloader::downloadToFile(googleSyncUrl, "/calendar/events.ics");
  isSyncing = false;
  if (err == HttpDownloader::OK) {
    loadEvents();
    statusNotification = tr(STR_CALENDAR_SYNC_OK);
    statusNotificationUntilMs = millis() + 3000;
  } else {
    statusNotification = tr(STR_CALENDAR_SYNC_FAIL);
    statusNotificationUntilMs = millis() + 4000;
  }
  requestUpdate();
}

void CalendarActivity::launchWifiSelection() {
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void CalendarActivity::onWifiSelectionComplete(bool connected) {
  if (connected) {
    syncGoogleCalendar();
  } else {
    statusNotification = tr(STR_WIFI_NOT_CONNECTED);
    statusNotificationUntilMs = millis() + 2500;
    requestUpdate();
  }
}

void CalendarActivity::loop() {
  if (statusNotificationUntilMs > 0 && millis() > statusNotificationUntilMs) {
    statusNotificationUntilMs = 0;
    statusNotification.clear();
    requestUpdate();
  }

  // Back button
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (mode == ViewMode::DAY_AGENDA) {
      mode = ViewMode::MONTH_GRID;
      needsCleanRefresh = true;
      requestUpdate();
      return;
    }
    activityManager.goHome();
    return;
  }

  // Confirm button (Toggle view or open agenda)
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mode == ViewMode::MONTH_GRID) {
      mode = ViewMode::DAY_AGENDA;
    } else {
      mode = ViewMode::MONTH_GRID;
    }
    needsCleanRefresh = true;
    requestUpdate();
    return;
  }

  // Long-press hold thresholds
  constexpr unsigned long HOLD_THRESHOLD_MS = 450;

  // Month navigation: Page buttons or Long-press Left/Right
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
      mappedInput.wasLongPressed(MappedInputManager::Button::Left, HOLD_THRESHOLD_MS) ||
      mappedInput.wasLongPressed(MappedInputManager::Button::PageBack, HOLD_THRESHOLD_MS)) {
    prevMonth();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
      mappedInput.wasLongPressed(MappedInputManager::Button::Right, HOLD_THRESHOLD_MS) ||
      mappedInput.wasLongPressed(MappedInputManager::Button::PageForward, HOLD_THRESHOLD_MS)) {
    nextMonth();
    return;
  }

  // Year navigation: Long-press Up/Down
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Up, HOLD_THRESHOLD_MS)) {
    viewedYear--;
    int maxDays = getDaysInMonth(viewedYear, viewedMonth);
    if (selectedDay > maxDays) selectedDay = maxDays;
    needsCleanRefresh = true;
    requestUpdate();
    return;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Down, HOLD_THRESHOLD_MS)) {
    viewedYear++;
    int maxDays = getDaysInMonth(viewedYear, viewedMonth);
    if (selectedDay > maxDays) selectedDay = maxDays;
    needsCleanRefresh = true;
    requestUpdate();
    return;
  }

  // Short press navigation
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    prevDay();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    nextDay();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (mode == ViewMode::MONTH_GRID) {
      if (selectedDay > 7) {
        selectedDay -= 7;
      } else {
        prevMonth();
        selectedDay = std::max(1, getDaysInMonth(viewedYear, viewedMonth) - (7 - selectedDay));
      }
      requestUpdate();
    } else {
      prevDay();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (mode == ViewMode::MONTH_GRID) {
      int maxDay = getDaysInMonth(viewedYear, viewedMonth);
      if (selectedDay + 7 <= maxDay) {
        selectedDay += 7;
      } else {
        int rem = selectedDay + 7 - maxDay;
        nextMonth();
        selectedDay = std::min(rem, getDaysInMonth(viewedYear, viewedMonth));
      }
      requestUpdate();
    } else {
      nextDay();
    }
    return;
  }

  // Touch screen interaction
  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect content = UITheme::getInstance().getContentArea(renderer);

    // Top Navigation bar (< [Month] > [Today]) placed below header
    int navY = metrics.topPadding + metrics.headerHeight + 6;
    int navH = 36;
    if (touchY >= navY && touchY <= navY + navH) {
      if (touchX < content.x + 60) {
        haptic_feedback::touchAction();
        prevMonth();
        return;
      }
      if (touchX > content.x + content.width - 60) {
        haptic_feedback::touchAction();
        nextMonth();
        return;
      }
      if (touchX >= content.x + content.width / 2 - 80 && touchX <= content.x + content.width / 2 + 80) {
        haptic_feedback::touchAction();
        jumpToToday();
        return;
      }
    }

    // Grid touch in MONTH_GRID mode
    if (mode == ViewMode::MONTH_GRID) {
      int gridStartY = navY + navH + 34;
      int colW = (content.width - 16) / 7;
      int rowH = 48;
      int firstDow = getFirstDayOfWeek(viewedYear, viewedMonth);
      int maxDay = getDaysInMonth(viewedYear, viewedMonth);

      if (touchY >= gridStartY && touchY <= gridStartY + 6 * rowH) {
        int r = (touchY - gridStartY) / rowH;
        int c = (touchX - (content.x + 8)) / colW;
        if (c >= 0 && c < 7 && r >= 0 && r < 6) {
          int slot = r * 7 + c;
          int d = slot - firstDow + 1;
          if (d >= 1 && d <= maxDay) {
            haptic_feedback::touchAction();
            if (selectedDay == d) {
              mode = ViewMode::DAY_AGENDA;
            } else {
              selectedDay = d;
            }
            requestUpdate();
            return;
          }
        }
      }

      // Tap bottom info card -> Open agenda
      int bottomCardY = gridStartY + 6 * rowH + 8;
      if (touchY >= bottomCardY) {
        haptic_feedback::touchAction();
        mode = ViewMode::DAY_AGENDA;
        requestUpdate();
        return;
      }
    } else {
      // In Day Agenda: Tap back button to grid or sync
      if (touchY > renderer.getScreenHeight() - metrics.buttonHintsHeight - 50) {
        haptic_feedback::touchAction();
        syncGoogleCalendar();
        return;
      }
    }
  }
}

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int screenW = renderer.getScreenWidth();

  char headerSubtitle[64];
  snprintf(headerSubtitle, sizeof(headerSubtitle), "%s %d", getMonthName(viewedMonth), viewedYear);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_CALENDAR),
                 headerSubtitle, true);

  if (mode == ViewMode::MONTH_GRID) {
    renderMonthGrid(content);
  } else {
    renderDayAgenda(content);
  }

  // Draw status banner notification if active
  if (!statusNotification.empty()) {
    drawStatusBanner(content.x + 16, content.y + content.height - 76, content.width - 32);
  }

  // Button Hints
  if (mode == ViewMode::MONTH_GRID) {
    const char* modeLabel = showEvents ? tr(STR_CALENDAR_GRID_ONLY) : tr(STR_CALENDAR_WITH_EVENTS);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), modeLabel, tr(STR_CALENDAR_PREV_MONTH), tr(STR_CALENDAR_NEXT_MONTH));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const auto labels = mappedInput.mapLabels(tr(STR_CALENDAR_GRID_VIEW), tr(STR_CALENDAR_SYNC_GCAL), tr(STR_CALENDAR_PREV_DAY), tr(STR_CALENDAR_NEXT_DAY));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (needsCleanRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    needsCleanRefresh = false;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void CalendarActivity::drawStatusBanner(int x, int y, int w) {
  int h = 32;
  renderer.fillRoundedRect(x, y, w, h, 6, Color::White);
  renderer.drawRect(x, y, w, h, true);
  renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  int tw = renderer.getTextWidth(UI_10_FONT_ID, statusNotification.c_str(), EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x + (w - tw) / 2, y + 8, statusNotification.c_str(), true, EpdFontFamily::BOLD);
}

void CalendarActivity::renderMonthGrid(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // 1. Month Bar: <  [Month Year]  >  placed cleanly below header (raised by 2px)
  int navY = metrics.topPadding + metrics.headerHeight + 4;
  int navH = 34;
  renderer.fillRoundedRect(content.x + 6, navY, content.width - 12, navH, 6, Color::White);
  renderer.drawRect(content.x + 6, navY, content.width - 12, navH, true);

  // Arrows
  renderer.drawText(UI_12_FONT_ID, content.x + 18, navY + 5, "<", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, content.x + content.width - 28, navY + 5, ">", true, EpdFontFamily::BOLD);

  // Month and Year centered
  char titleBuf[64];
  snprintf(titleBuf, sizeof(titleBuf), "%s  %d", getMonthName(viewedMonth), viewedYear);
  int titleW = renderer.getTextWidth(UI_12_FONT_ID, titleBuf, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, content.x + (content.width - titleW) / 2, navY + 5, titleBuf, true,
                    EpdFontFamily::BOLD);

  // 2. Day-of-week header row (Mon-Sun) - raised and spaced cleanly above grid
  int dowY = navY + navH + 3;
  int dowH = 20;
  int gridMarginX = content.x + 8;
  int gridW = content.width - 16;
  int colW = gridW / 7;

  for (int c = 0; c < 7; ++c) {
    int cx = gridMarginX + c * colW;
    const char* dowStr = getDayOfWeekShort(c);
    int dw = renderer.getTextWidth(UI_10_FONT_ID, dowStr, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, cx + (colW - dw) / 2, dowY + 2, dowStr, true, EpdFontFamily::BOLD);
  }

  // Line under DOW
  int lineY = dowY + dowH;
  renderer.drawLine(gridMarginX, lineY, gridMarginX + 7 * colW, lineY);

  // 3. Month Matrix (6 rows x 7 cols)
  int gridStartY = lineY + 6;
  int cellH = 46;
  int firstDow = getFirstDayOfWeek(viewedYear, viewedMonth);
  int maxDays = getDaysInMonth(viewedYear, viewedMonth);
  int prevMaxDays = getDaysInMonth(viewedMonth == 1 ? viewedYear - 1 : viewedYear,
                                   viewedMonth == 1 ? 12 : viewedMonth - 1);

  for (int r = 0; r < 6; ++r) {
    int cy = gridStartY + r * cellH;
    for (int c = 0; c < 7; ++c) {
      int cx = gridMarginX + c * colW;
      int slot = r * 7 + c;
      int dayNum = slot - firstDow + 1;
      bool isCurMonth = (dayNum >= 1 && dayNum <= maxDays);

      int displayDay = dayNum;
      if (dayNum < 1) {
        displayDay = prevMaxDays + dayNum;
      } else if (dayNum > maxDays) {
        displayDay = dayNum - maxDays;
      }

      bool isSelected = isCurMonth && (dayNum == selectedDay);
      bool isToday = isCurMonth && (viewedYear == todayYear && viewedMonth == todayMonth && dayNum == todayDay);
      bool hasEvent = isCurMonth && showEvents && hasEventsOnDate(viewedYear, viewedMonth, dayNum);

      // Cell border
      renderer.drawRect(cx, cy, colW, cellH, true);

      // Selected Day Highlight
      if (isSelected) {
        renderer.fillRoundedRect(cx + 2, cy + 2, colW - 4, cellH - 4, 4, Color::LightGray);
      }

      // Today Indicator: crisp 2px border outline
      if (isToday) {
        int badgeW = 26;
        int badgeH = 22;
        int badgeX = cx + (colW - badgeW) / 2;
        int badgeY = cy + 4;
        renderer.drawRect(badgeX, badgeY, badgeW, badgeH, true);
        renderer.drawRect(badgeX + 1, badgeY + 1, badgeW - 2, badgeH - 2, true);
      }

      // Day Number Text (lowered by 1px)
      char dayStr[8];
      snprintf(dayStr, sizeof(dayStr), "%d", displayDay);
      int nw = renderer.getTextWidth(UI_10_FONT_ID, dayStr, (isToday || isSelected) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      int tx = cx + (colW - nw) / 2;
      int ty = cy + 7;

      if (isToday) {
        renderer.drawText(UI_10_FONT_ID, tx, ty, dayStr, true, EpdFontFamily::BOLD);
      } else if (!isCurMonth) {
        // Crisp regular text for days outside month, without dithering
        renderer.drawText(UI_10_FONT_ID, tx, ty, dayStr, true, EpdFontFamily::REGULAR);
      } else {
        renderer.drawText(UI_10_FONT_ID, tx, ty, dayStr, true, isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }

      // Event Indicator Dots at bottom of cell
      if (hasEvent) {
        int dotY = cy + cellH - 8;
        int dotX = cx + colW / 2;
        renderer.fillRoundedRect(dotX - 2, dotY, 4, 4, 2, isSelected ? Color::Black : Color::Black);
      }
    }
  }

  // 4. Bottom Info Card
  int cardY = gridStartY + 6 * cellH + 8;
  int cardH = content.y + content.height - cardY - 6;
  if (cardH > 30) {
    renderer.fillRoundedRect(gridMarginX, cardY, gridW, cardH, 6, Color::White);
    renderer.drawRect(gridMarginX, cardY, gridW, cardH, true);
    renderer.drawRect(gridMarginX + 1, cardY + 1, gridW - 2, cardH - 2, true);

    // Date Title in card
    struct tm tempTm = {};
    tempTm.tm_year = viewedYear - 1900;
    tempTm.tm_mon = viewedMonth - 1;
    tempTm.tm_mday = selectedDay;
    mktime(&tempTm);
    int selDow = (tempTm.tm_wday + 6) % 7;

    char cardDateTitle[96];
    snprintf(cardDateTitle, sizeof(cardDateTitle), "%d %s %d, %s",
             selectedDay, getMonthName(viewedMonth), viewedYear, getDayOfWeekFull(selDow));
    renderer.drawText(UI_10_FONT_ID, gridMarginX + 12, cardY + 8, cardDateTitle, true, EpdFontFamily::BOLD);

    if (showEvents) {
      auto dayEvents = getEventsForDate(viewedYear, viewedMonth, selectedDay);
      if (dayEvents.empty()) {
        renderer.drawText(UI_10_FONT_ID, gridMarginX + 12, cardY + 30, tr(STR_CALENDAR_NO_EVENTS), true);
        if (!googleSyncUrl.empty()) {
          renderer.drawText(UI_10_FONT_ID, gridMarginX + 12, cardY + 50, tr(STR_CALENDAR_GCAL_CONNECTED), true);
        }
      } else {
        int evY = cardY + 30;
        for (size_t i = 0; i < dayEvents.size() && evY < cardY + cardH - 18; ++i) {
          const auto* ev = dayEvents[i];
          char lineBuf[128];
          if (ev->isAllDay) {
            snprintf(lineBuf, sizeof(lineBuf), "• [%s] %s", tr(STR_CALENDAR_ALL_DAY), ev->summary.c_str());
          } else {
            snprintf(lineBuf, sizeof(lineBuf), "• %02d:%02d  %s", ev->startHour, ev->startMin, ev->summary.c_str());
          }
          renderer.drawText(UI_10_FONT_ID, gridMarginX + 12, evY, lineBuf, true);
          evY += 20;
        }
      }
    } else {
      // Pure calendar info
      char dayOfYearBuf[128];
      snprintf(dayOfYearBuf, sizeof(dayOfYearBuf), tr(STR_CALENDAR_DAY_OF_YEAR), tempTm.tm_yday + 1);
      renderer.drawText(UI_10_FONT_ID, gridMarginX + 12, cardY + 30, dayOfYearBuf, true);
    }
  }
}

void CalendarActivity::renderDayAgenda(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Agenda header bar (< [Date] >) placed cleanly below header
  int navY = metrics.topPadding + metrics.headerHeight + 6;
  int navH = 38;
  renderer.fillRoundedRect(content.x + 8, navY, content.width - 16, navH, 6, Color::White);
  renderer.drawRect(content.x + 8, navY, content.width - 16, navH, true);

  renderer.drawText(UI_12_FONT_ID, content.x + 18, navY + 8, "<", true, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, content.x + content.width - 28, navY + 8, ">", true, EpdFontFamily::BOLD);

  struct tm tempTm = {};
  tempTm.tm_year = viewedYear - 1900;
  tempTm.tm_mon = viewedMonth - 1;
  tempTm.tm_mday = selectedDay;
  mktime(&tempTm);
  int selDow = (tempTm.tm_wday + 6) % 7;

  char dateStr[96];
  snprintf(dateStr, sizeof(dateStr), "%d %s %d (%s)",
           selectedDay, getMonthName(viewedMonth), viewedYear, getDayOfWeekShort(selDow));
  int dtW = renderer.getTextWidth(UI_12_FONT_ID, dateStr, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, content.x + (content.width - dtW) / 2, navY + 8, dateStr, true,
                    EpdFontFamily::BOLD);

  // Events list
  auto dayEvents = getEventsForDate(viewedYear, viewedMonth, selectedDay);
  int cardStartY = navY + navH + 12;
  int cardW = content.width - 16;
  int cardX = content.x + 8;

  if (dayEvents.empty()) {
    int emptyH = 160;
    renderer.fillRoundedRect(cardX, cardStartY, cardW, emptyH, 8, Color::White);
    renderer.drawRect(cardX, cardStartY, cardW, emptyH, true);

    renderer.drawText(UI_12_FONT_ID, cardX + 20, cardStartY + 24, tr(STR_CALENDAR_NO_EVENTS), true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, cardX + 20, cardStartY + 56,
                      tr(STR_CALENDAR_EVENTS_ICS_HINT), true);
    renderer.drawText(UI_10_FONT_ID, cardX + 20, cardStartY + 80,
                      tr(STR_CALENDAR_OR_GCAL), true);

    if (googleSyncUrl.empty()) {
      renderer.drawText(UI_10_FONT_ID, cardX + 20, cardStartY + 116,
                        tr(STR_CALENDAR_GCAL_CONFIG), true, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, cardX + 20, cardStartY + 116,
                        tr(STR_CALENDAR_GCAL_CONNECTED), true, EpdFontFamily::BOLD);
    }
    return;
  }

  int curY = cardStartY;
  for (size_t i = 0; i < dayEvents.size() && curY < content.y + content.height - 80; ++i) {
    const auto* ev = dayEvents[i];
    int eventCardH = 74;

    renderer.fillRoundedRect(cardX, curY, cardW, eventCardH, 6, Color::White);
    renderer.drawRect(cardX, curY, cardW, eventCardH, true);
    renderer.drawRect(cardX + 1, curY + 1, cardW - 2, eventCardH - 2, true);

    // Time pill
    char timeBadge[32];
    if (ev->isAllDay) {
      snprintf(timeBadge, sizeof(timeBadge), "%s", tr(STR_CALENDAR_ALL_DAY));
    } else {
      snprintf(timeBadge, sizeof(timeBadge), "%02d:%02d - %02d:%02d",
               ev->startHour, ev->startMin,
               ev->endHour >= 0 ? ev->endHour : (ev->startHour + 1) % 24,
               ev->endMin >= 0 ? ev->endMin : ev->startMin);
    }
    int tbW = renderer.getTextWidth(UI_10_FONT_ID, timeBadge, EpdFontFamily::BOLD);
    renderer.fillRoundedRect(cardX + 10, curY + 8, tbW + 12, 20, 4, Color::LightGray);
    renderer.drawText(UI_10_FONT_ID, cardX + 16, curY + 10, timeBadge, true, EpdFontFamily::BOLD);

    // Summary Title
    renderer.drawText(UI_10_FONT_ID, cardX + 14 + tbW + 16, curY + 10, ev->summary.c_str(), true,
                      EpdFontFamily::BOLD);

    // Location / Details
    if (!ev->location.empty()) {
      char locBuf[96];
      snprintf(locBuf, sizeof(locBuf), tr(STR_CALENDAR_LOCATION), ev->location.c_str());
      renderer.drawText(UI_10_FONT_ID, cardX + 14, curY + 36, locBuf, true);
    } else if (!ev->description.empty()) {
      renderer.drawText(UI_10_FONT_ID, cardX + 14, curY + 36, ev->description.c_str(), true);
    } else {
      renderer.drawText(UI_10_FONT_ID, cardX + 14, curY + 36, tr(STR_CALENDAR_EVENT), true);
    }

    curY += eventCardH + 10;
  }
}
