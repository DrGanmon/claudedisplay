#pragma once
#include <math.h>
#include <stdlib.h>
#include <ArduinoJson.h>

inline bool validWeatherCoordinate(const char *text, double limit) {
  if (!text || !*text) return false;
  // Only decimal coordinates; reject whitespace, exponents and URL fragments.
  bool digit = false, point = false;
  const char *p = text;
  if (*p == '-' || *p == '+') ++p;
  for (; *p; ++p) {
    if (*p >= '0' && *p <= '9') digit = true;
    else if (*p == '.' && !point) point = true;
    else return false;
  }
  const double value = strtod(text, nullptr);
  return digit && isfinite(value) && value >= -limit && value <= limit;
}

inline bool validWeatherCurrent(JsonVariantConst current) {
  if (!current["temperature_2m"].is<float>() ||
      !current["relative_humidity_2m"].is<float>() ||
      !current["wind_speed_10m"].is<float>() ||
      !current["weather_code"].is<int>()) return false;
  const float temperature = current["temperature_2m"], humidity = current["relative_humidity_2m"],
              wind = current["wind_speed_10m"];
  const int code = current["weather_code"];
  return isfinite(temperature) && isfinite(humidity) && isfinite(wind) &&
      humidity >= 0 && humidity <= 100 && wind >= 0 && code >= 0 && code <= 99;
}

inline const char *weatherDescription(int code) {
  switch (code) {
    case 0: return "Klar";
    case 1: return "Meist klar";
    case 2: return "Teilweise bewoelkt";
    case 3: return "Bedeckt";
    case 45: case 48: return "Nebel";
    case 51: case 53: case 55: return "Nieselregen";
    case 56: case 57: return "Gefrierender Niesel";
    case 61: case 63: case 65: return "Regen";
    case 66: case 67: return "Gefrierender Regen";
    case 71: case 73: case 75: case 77: return "Schnee";
    case 80: case 81: case 82: return "Regenschauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter mit Hagel";
    default: return "Unbekannt";
  }
}
