#include <cassert>
#include <cstring>
#include "../src/weather_protocol.h"
int main() {
  assert(validWeatherCoordinate("0", 90));
  assert(validWeatherCoordinate("-90", 90));
  assert(validWeatherCoordinate("180.0", 180));
  for (const char *bad : {"", "-", ".", "NaN", "inf", "1&x=1", "1,2", "1e2", " 1", "90.01"})
    assert(!validWeatherCoordinate(bad, 90));
  JsonDocument doc;
  assert(!deserializeJson(doc, R"({"temperature_2m":12.5,"relative_humidity_2m":70,"wind_speed_10m":5,"weather_code":3})"));
  assert(validWeatherCurrent(doc.as<JsonVariantConst>()));
  doc["temperature_2m"] = nullptr;
  assert(!validWeatherCurrent(doc.as<JsonVariantConst>()));
  doc["temperature_2m"] = 12;
  doc["relative_humidity_2m"] = 101;
  assert(!validWeatherCurrent(doc.as<JsonVariantConst>()));
  doc["relative_humidity_2m"] = 70;
  doc["wind_speed_10m"] = -1;
  assert(!validWeatherCurrent(doc.as<JsonVariantConst>()));
  assert(std::strcmp(weatherDescription(95), "Gewitter") == 0);
}
