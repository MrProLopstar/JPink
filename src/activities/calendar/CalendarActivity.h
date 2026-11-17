#pragma once

#include "activities/Activity.h"
#include <GfxRenderer.h>

struct Rect;
#include <MappedInputManager.h>

#include <string>
#include <vector>

struct CalendarEvent {
  std::string summary;
  std::string description;
  std::string location;
  int year = 0;
  int month = 0;       // 1-12
  int day = 0;         // 1-31
  int startHour = -1;  // 0-23, or -1 for all-day
  int startMin = -1;
  int endHour = -1;
  int endMin = -1;
  bool isAllDay = true;
};

class CalendarActivity : public Activity {
 public:
  enum class ViewMode {
    MONTH_GRID,
    DAY_AGENDA
  };

  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~CalendarActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ViewMode mode = ViewMode::MONTH_GRID;
  bool showEvents = true;  // Option to show events or clean calendar grid only
  bool needsCleanRefresh = true;

  // Calendar dates
  int viewedYear = 2026;
  int viewedMonth = 11;    // 1-12
  int selectedDay = 15;    // 1-31
  int todayYear = 2026;
  int todayMonth = 11;
  int todayDay = 15;

  // Events & Google Calendar
  std::vector<CalendarEvent> events;
  std::string googleSyncUrl;
  std::string statusNotification;
  unsigned long statusNotificationUntilMs = 0;
  bool isSyncing = false;
  int agendaScroll = 0;

  // Loading & Parsing
  void initCurrentDate();
  void loadEvents();
  void loadGoogleSyncConfig();
  void parseIcsFile(const std::string& path);
  void createSampleEventsIfEmpty();
  void syncGoogleCalendar();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);

  // Queries
  std::vector<const CalendarEvent*> getEventsForDate(int y, int m, int d) const;
  bool hasEventsOnDate(int y, int m, int d) const;
  int countEventsForMonth(int y, int m) const;

  // Date math
  static bool isLeapYear(int y);
  static int getDaysInMonth(int y, int m);
  static int getFirstDayOfWeek(int y, int m); // 0=Mon, 6=Sun

  // Drawing
  void renderMonthGrid(const Rect& content);
  void renderDayAgenda(const Rect& content);
  void drawStatusBanner(int x, int y, int w);

  // Navigation
  void prevMonth();
  void nextMonth();
  void prevDay();
  void nextDay();
  void jumpToToday();
};
