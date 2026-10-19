#include "weather.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

WeatherResult fetchWeather(const String &latitude, const String &longitude) {
  WeatherResult result;
  if (!validWeatherCoordinate(latitude.c_str(), 90) ||
      !validWeatherCoordinate(longitude.c_str(), 180)) {
    result.state = "Standort ungueltig";
    return result;
  }
  BearSSL::WiFiClientSecure client;
  // Same TLS policy as the existing Claude API client (see README).
  client.setInsecure();
  client.setTimeout(5000);
  HTTPClient http;
  http.setTimeout(5000);
  http.useHTTP10(true);
  const String url = "https://api.open-meteo.com/v1/forecast?latitude=" + latitude +
      "&longitude=" + longitude +
      "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&forecast_days=1";
  if (!http.begin(client, url)) {
    result.state = "Wetter-Verbindungsfehler";
    return result;
  }
  http.addHeader("Accept-Encoding", "identity");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    result.state = "Wetter HTTP " + String(status);
    http.end();
    return result;
  }
  JsonDocument filter, doc;
  filter["current"] = true;
  const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error || !validWeatherCurrent(doc["current"])) {
    result.state = "Wetterdaten ungueltig";
    return result;
  }
  result.temperature = doc["current"]["temperature_2m"];
  result.humidity = doc["current"]["relative_humidity_2m"];
  result.wind = doc["current"]["wind_speed_10m"];
  result.code = doc["current"]["weather_code"];
  result.valid = true;
  result.state = "Aktuell";
  return result;
}
