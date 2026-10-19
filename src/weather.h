#pragma once
#include <Arduino.h>
#include "weather_protocol.h"

struct WeatherResult {
  bool valid = false;
  float temperature = 0, humidity = 0, wind = 0;
  int code = 0;
  String state = "Noch kein Abruf";
};
WeatherResult fetchWeather(const String &latitude, const String &longitude);
