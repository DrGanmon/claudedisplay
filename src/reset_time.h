#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

inline int64_t parseResetTimestamp(const char *text) {
  const size_t length = strlen(text);
  if (length < 20) return -1;
  for (size_t i = 0; i < 19; ++i) {
    if (i == 4 || i == 7) { if (text[i] != '-') return -1; }
    else if (i == 10) { if (text[i] != 'T' && text[i] != 't') return -1; }
    else if (i == 13 || i == 16) { if (text[i] != ':') return -1; }
    else if (text[i] < '0' || text[i] > '9') return -1;
  }
  int year, month, day, hour, minute, second;
  if (sscanf(text, "%4d-%2d-%2d%*c%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) return -1;
  if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59) return -1;
  const int monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (day > monthDays[month - 1] + (month == 2 && leap ? 1 : 0)) return -1;
  size_t pos = 19;
  if (text[pos] == '.') {
    const size_t first = ++pos;
    while (pos < length && text[pos] >= '0' && text[pos] <= '9') ++pos;
    if (pos == first) return -1;
  }
  int offset = 0;
  if (pos < length && (text[pos] == 'Z' || text[pos] == 'z') && pos + 1 == length) {
    // UTC
  } else if (pos + 6 == length && (text[pos] == '+' || text[pos] == '-') && text[pos + 3] == ':') {
    for (size_t i = pos + 1; i < pos + 6; ++i) if (i != pos + 3 && (text[i] < '0' || text[i] > '9')) return -1;
    const int hours = (text[pos + 1] - '0') * 10 + text[pos + 2] - '0';
    const int minutes = (text[pos + 4] - '0') * 10 + text[pos + 5] - '0';
    if (hours > 23 || minutes > 59) return -1;
    offset = (hours * 60 + minutes) * 60 * (text[pos] == '+' ? 1 : -1);
  } else return -1;
  int64_t days = 0;
  for (int y = 1970; y < year; ++y) days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  for (int m = 1; m < month; ++m) days += monthDays[m - 1] + (m == 2 && leap ? 1 : 0);
  return (days + day - 1) * 86400 + hour * 3600 + minute * 60 + second - offset;
}

inline void formatResetRemaining(int64_t reset, int64_t now, char *out, size_t size) {
  if (reset < 0 || now < 1577836800) { snprintf(out, size, "-- --:--"); return; }
  const int64_t minutes = reset > now ? (reset - now + 59) / 60 : 0;
  snprintf(out, size, "%lld %02d:%02d", static_cast<long long>(minutes / 1440),
           static_cast<int>((minutes / 60) % 24), static_cast<int>(minutes % 60));
}

inline void formatCozyResetRemaining(int64_t reset, int64_t now, char *out, size_t size) {
  if (reset < 0 || now < 1577836800) { snprintf(out, size, "--"); return; }
  const int64_t minutes = reset > now ? (reset - now + 59) / 60 : 0;
  if (minutes >= 1440)
    snprintf(out, size, "%lld T %d h", static_cast<long long>(minutes / 1440), static_cast<int>((minutes / 60) % 24));
  else if (minutes >= 60)
    snprintf(out, size, "%d h %d min", static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
  else snprintf(out, size, "%d min", static_cast<int>(minutes));
}
