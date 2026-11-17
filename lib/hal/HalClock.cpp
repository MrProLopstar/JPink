#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
}

namespace {
// UTC calendar date -> Unix epoch, no timezone involvement (newlib has no
// timegm). Days-from-civil per Howard Hinnant's algorithm.
time_t epochFromUtc(const Rtc::DateTime& dt) {
  int y = dt.year;
  const int m = dt.month;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + dt.day - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const long days = static_cast<long>(era) * 146097L + static_cast<long>(doe) - 719468L;
  return static_cast<time_t>(days) * 86400 + dt.hour * 3600L + dt.minute * 60L + dt.second;
}
}  // namespace

void HalClock::setTimezone(const char* posixTz) {
  setenv("TZ", posixTz && posixTz[0] != '\0' ? posixTz : "UTC0", 1);
  tzset();
  _lastPollMs = 0;  // re-derive local time under the new rule immediately
}

bool HalClock::localTime(struct tm& out) const {
  if (!_available) {
    time_t now = time(nullptr);
    if (now > 100000) {
      localtime_r(&now, &out);
      if (out.tm_year + 1900 < 2026) {
        out.tm_year = 2026 - 1900;
        out.tm_mon = 0;
        out.tm_mday = 1;
        out.tm_hour = 12;
        out.tm_min = 0;
        out.tm_sec = 0;
        out.tm_wday = 4;
      }
      return true;
    }
    out.tm_year = 2026 - 1900;
    out.tm_mon = 0;
    out.tm_mday = 1;
    out.tm_hour = 12;
    out.tm_min = 0;
    out.tm_sec = 0;
    out.tm_wday = 4;
    return true;
  }

  const unsigned long now = millis();
  if (_lastPollMs == 0 || (now - _lastPollMs) >= CLOCK_POLL_MS) {
    Rtc::DateTime dt;
    if (_sdkRtc.now(dt)) {
      _cachedUtc = epochFromUtc(dt);
      _hasCachedTime = true;
    } else if (!_hasCachedTime) {
      out.tm_year = 2026 - 1900;
      out.tm_mon = 0;
      out.tm_mday = 1;
      out.tm_hour = 12;
      out.tm_min = 0;
      out.tm_sec = 0;
      out.tm_wday = 4;
      return true;
    }
    _lastPollMs = now != 0 ? now : 1;  // 0 doubles as the invalidation sentinel
  }
  localtime_r(&_cachedUtc, &out);
  if (out.tm_year + 1900 < 2026) {
    out.tm_year = 2026 - 1900;
    out.tm_mon = 0;
    out.tm_mday = 1;
    out.tm_hour = 12;
    out.tm_min = 0;
    out.tm_sec = 0;
    out.tm_wday = 4;
  }
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  struct tm local;
  if (!localTime(local)) return false;
  hour = static_cast<uint8_t>(local.tm_hour);
  minute = static_cast<uint8_t>(local.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  struct tm local;
  if (!localTime(local)) return false;

  if (use12Hour) {
    const bool pm = local.tm_hour >= 12;
    int hour12 = local.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, local.tm_min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", local.tm_hour, local.tm_min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  // configTzTime overwrites the process TZ with UTC0 for the SNTP exchange;
  // remember the display timezone so it can be restored below.
  const char* tzBefore = getenv("TZ");
  char savedTz[64] = {0};
  if (tzBefore) snprintf(savedTz, sizeof(savedTz), "%s", tzBefore);
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      gmtime_r(&now, &timeinfo);

      bool ok = true;
      if (_available) {
        Rtc::DateTime dt;
        dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
        dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
        dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
        dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
        dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
        dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
        dt.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
        ok = _sdkRtc.set(dt);
        if (ok) {
          _cachedUtc = epochFromUtc(dt);
          _hasCachedTime = true;
          _lastPollMs = 0;
          LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                  dt.second);
        }
      } else {
        _cachedUtc = now;
        _hasCachedTime = true;
        _lastPollMs = 0;
      }
      setTimezone(savedTz);
      return ok;
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  setTimezone(savedTz);
  return false;
}

bool HalClock::setDateTime(int year, int month, int day, int hour, int minute, int second) {
  struct tm localTm;
  memset(&localTm, 0, sizeof(localTm));
  localTm.tm_year = year - 1900;
  localTm.tm_mon = month - 1;
  localTm.tm_mday = day;
  localTm.tm_hour = hour;
  localTm.tm_min = minute;
  localTm.tm_sec = second;
  localTm.tm_isdst = -1;

  time_t epoch = mktime(&localTm);
  if (epoch == (time_t)-1) {
    return false;
  }

  // Update ESP32 system clock so time() / gettimeofday() work immediately
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  // Convert epoch to UTC for the hardware RTC
  struct tm utcTm;
  gmtime_r(&epoch, &utcTm);

  bool ok = true;
  if (_available) {
    Rtc::DateTime dt;
    dt.year = static_cast<uint16_t>(utcTm.tm_year + 1900);
    dt.month = static_cast<uint8_t>(utcTm.tm_mon + 1);
    dt.day = static_cast<uint8_t>(utcTm.tm_mday);
    dt.hour = static_cast<uint8_t>(utcTm.tm_hour);
    dt.minute = static_cast<uint8_t>(utcTm.tm_min);
    dt.second = static_cast<uint8_t>(utcTm.tm_sec);
    dt.weekday = static_cast<uint8_t>(utcTm.tm_wday);
    ok = _sdkRtc.set(dt);
    if (ok) {
      _cachedUtc = epochFromUtc(dt);
      _hasCachedTime = true;
      _lastPollMs = 0;
      LOG_INF("CLK", "RTC manually set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
              dt.second);
    }
  } else {
    _cachedUtc = epoch;
    _hasCachedTime = true;
    _lastPollMs = 0;
  }
  return ok;
}
