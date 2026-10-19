#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <Updater.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include "org_id.h"
#include "reset_time.h"
#include "trueconf.h"
#include "display_layout.h"
#include "active_text_cache.h"
#include "text_strips.h"
#include "reusable_text_buffer.h"
#include "web_ui.h"
#include "wifi_recovery.h"
#include "wifi_pulse.h"
#include "weather.h"
#include "weather_animation.h"
#include "touch_input.h"
#include "display_pages.h"
#include "tls_memory.h"
#include "limit_ring_color.h"
#include "ring_startup.h"
#include <umm_malloc/umm_heap_select.h>
#include <memory>
#include <time.h>

#ifndef MMU_IRAM_HEAP
#error "Claude TLS requires the shared IRAM heap configured in platformio.ini"
#endif

namespace Pins {
constexpr uint8_t I2C_SDA = D2;
constexpr uint8_t I2C_SCL = D1;
constexpr uint8_t TOUCH = D6;
}  // namespace Pins

namespace Timing {
constexpr uint32_t SENSOR_MS = 5000;
constexpr uint32_t DISPLAY_MS = 1000;
constexpr uint32_t CLAUDE_MIN_MS = 60000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t TRUECONF_STARTUP_GRACE_MS = 60000;
}  // namespace Timing

constexpr char CONFIG_FILE[] = "/config.json";
constexpr char HOSTNAME[] = "claude-display";
constexpr char SETUP_AP[] = "ClaudeDisplay-Setup";
constexpr char SETUP_AP_PASSWORD[] = "claude-display";
constexpr char DEFAULT_WEB_PASSWORD[] = "claude-display";
constexpr char CLAUDE_BASE[] = "https://claude.ai/api/organizations";
constexpr char FIRMWARE_VERSION[] = "2026-10-20.9";
constexpr char LOCAL_TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char FIREFOX_WINDOWS_USER_AGENT[] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:140.0) Gecko/20100101 Firefox/140.0";
constexpr char LEGACY_USER_AGENT[] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";

struct Config {
  String displayName = "Claude Display";
  bool showIp = true;
  String sessionKey;
  String orgId;
  String extraCookies;
  String userAgent = FIREFOX_WINDOWS_USER_AGENT;
  String webPassword = DEFAULT_WEB_PASSWORD;
  uint32_t intervalSec = 180;
  TrueConfConfig trueConf;
  String weatherName;
  String weatherLatitude;
  String weatherLongitude;
  String ntpServer = "pool.ntp.org";
};

struct LimitBucket {
  bool valid = false;
  float used = 0.0f;
  String resetsAt;
  int64_t resetEpoch = -1;
};

Config config;
LimitBucket sessionLimit;
LimitBucket weeklyLimit;

TFT_eSPI display;
TFT_eSprite textSprite(&display);
ReusableTextBuffer<TFT_eSprite> textBuffer(textSprite);
String fittedText;

void releaseDisplayScratch() {
  textBuffer.release();
  fittedText = String();
}

WeatherAnimation weatherAnimation(display);
ESP8266WebServer web(80);
std::unique_ptr<WiFiManager> wifiPortal;
WifiRecovery wifiRecovery;
WifiStartup wifiStartup;
bool normalWebRunning = false;
Adafruit_BME280 bme;
Adafruit_BMP280 bmp;

enum class SensorType { NONE, BMP280, BME280 };
SensorType sensorType = SensorType::NONE;

float temperatureC = NAN;
float humidityPct = NAN;
float pressureHpa = NAN;
String claudeState = "Nicht konfiguriert";
String lastClaudeUpdate = "Noch nie";
uint32_t lastSensorMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastClaudeMs = 0;
uint32_t lastWifiRetryMs = 0;
bool forceClaudeRefresh = false;
bool displayDirty = true;
bool displayNeedsFullRedraw = true;
bool updateAuthenticated = false;
bool updateSuccessful = false;
String updateError;
TrueConfResult trueConfResult;
uint32_t lastTrueConfMs = 0;
bool forceTrueConfRefresh = false;
uint32_t webServerReadyMs = 0;

WeatherResult weather;
DisplayPages pages;
bool forceWeatherRefresh = true;
uint32_t lastWeatherMs = 0;

TouchInput touchInput;
bool touchResetRequested = false;

String htmlEscape(const String &input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '\"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += input[i];
    }
  }
  return out;
}

void sendHtmlEscaped(const String &input) {
  // Ein leerer HTTP-Chunk beendet die Antwort. Nur endPage() darf ihn senden.
  if (input.length()) web.sendContent(htmlEscape(input));
}

String percentText(const LimitBucket &bucket, bool remaining = true) {
  if (!bucket.valid) return "--";
  const float value = remaining ? 100.0f - bucket.used : bucket.used;
  return String(constrain(value, 0.0f, 100.0f), 0);
}

void normalizeClaudeConfig() {
  config.sessionKey.trim();
  if (config.sessionKey.startsWith("sessionKey=")) config.sessionKey.remove(0, 11);
  config.sessionKey.trim();
  // Ein kopierter Cookie-Wert kann mit einem Semikolon enden.
  while (config.sessionKey.endsWith(";")) config.sessionKey.remove(config.sessionKey.length() - 1);
  config.extraCookies.trim();
  while (config.extraCookies.endsWith(";")) config.extraCookies.remove(config.extraCookies.length() - 1);
  config.orgId.trim();
  const size_t uuidStart = findOrganizationUuid(config.orgId.c_str(), config.orgId.length());
  if (uuidStart != config.orgId.length()) config.orgId = config.orgId.substring(uuidStart, uuidStart + 36);
  config.userAgent.trim();
  if (!config.userAgent.length()) config.userAgent = FIREFOX_WINDOWS_USER_AGENT;
}

bool loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) return false;
  File file = LittleFS.open(CONFIG_FILE, "r");
  if (!file) return false;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return false;
  config.displayName = doc["displayName"] | "Claude Display";
  config.showIp = doc["showIp"] | true;
  if (!config.displayName.length()) config.displayName = "Claude Display";
  config.sessionKey = doc["sessionKey"] | "";
  config.orgId = doc["orgId"] | "";
  config.extraCookies = doc["extraCookies"] | "";
  config.userAgent = doc["userAgent"] | config.userAgent;
  if (config.userAgent == LEGACY_USER_AGENT) config.userAgent = FIREFOX_WINDOWS_USER_AGENT;
  config.webPassword = doc["webPassword"] | DEFAULT_WEB_PASSWORD;
  config.intervalSec = constrain(static_cast<uint32_t>(doc["intervalSec"] | 180), 60UL, 3600UL);
  config.ntpServer = doc["ntpServer"] | "pool.ntp.org";
  config.trueConf.enabled = doc["trueConf"]["enabled"] | false;
  config.trueConf.host = doc["trueConf"]["host"] | "";
  config.trueConf.port = doc["trueConf"]["port"] | 443;
  config.trueConf.username = doc["trueConf"]["username"] | "";
  config.trueConf.password = doc["trueConf"]["password"] | "";
  config.trueConf.token = doc["trueConf"]["token"] | "";
  config.trueConf.oauthClientId = doc["trueConf"]["oauthClientId"] | "";
  config.trueConf.fingerprint = doc["trueConf"]["fingerprint"] | "";
  config.trueConf.insecure = doc["trueConf"]["insecure"] | false;
  config.trueConf.intervalSec = constrain(static_cast<uint32_t>(doc["trueConf"]["intervalSec"] | 60), 30UL, 3600UL);
  config.weatherName = doc["weather"]["name"] | "";
  config.weatherLatitude = doc["weather"]["latitude"] | "";
  config.weatherLongitude = doc["weather"]["longitude"] | "";
  normalizeClaudeConfig();
  return true;
}

bool saveConfig() {
  JsonDocument doc;
  doc["weather"]["name"] = config.weatherName;
  doc["weather"]["latitude"] = config.weatherLatitude;
  doc["weather"]["longitude"] = config.weatherLongitude;
  doc["displayName"] = config.displayName;
  doc["showIp"] = config.showIp;
  doc["sessionKey"] = config.sessionKey;
  doc["orgId"] = config.orgId;
  doc["extraCookies"] = config.extraCookies;
  doc["userAgent"] = config.userAgent;
  doc["webPassword"] = config.webPassword;
  doc["intervalSec"] = config.intervalSec;
  doc["ntpServer"] = config.ntpServer;
  doc["trueConf"]["enabled"] = config.trueConf.enabled;
  doc["trueConf"]["host"] = config.trueConf.host;
  doc["trueConf"]["port"] = config.trueConf.port;
  doc["trueConf"]["username"] = config.trueConf.username;
  doc["trueConf"]["password"] = config.trueConf.password;
  doc["trueConf"]["token"] = config.trueConf.token;
  doc["trueConf"]["oauthClientId"] = config.trueConf.oauthClientId;
  doc["trueConf"]["fingerprint"] = config.trueConf.fingerprint;
  doc["trueConf"]["insecure"] = config.trueConf.insecure;
  doc["trueConf"]["intervalSec"] = config.trueConf.intervalSec;
  // Erst vollständig in eine temporäre Datei schreiben, dann atomar ersetzen.
  File file = LittleFS.open("/config.json.tmp", "w");
  if (!file) return false;
  bool ok = serializeJson(doc, file) == measureJson(doc);
  file.flush();
  ok = ok && !file.getWriteError();
  file.close();
  if (!ok) { LittleFS.remove("/config.json.tmp"); return false; }
  return LittleFS.rename("/config.json.tmp", CONFIG_FILE);
}

void beginPage(const __FlashStringHelper *title) {
  web.sendHeader("Cache-Control", "no-store");
  web.setContentLength(CONTENT_LENGTH_UNKNOWN);
  web.send(200, "text/html; charset=utf-8", "");
  web.sendContent(F("<!doctype html><html lang='de' data-theme='dark'><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'><meta name='theme-color' content='#171a17'><title>"));
  web.sendContent(title);
  web.sendContent(F(" · Claude Display</title><link rel='stylesheet' href='/assets/cozy.css?v="));
  web.sendContent(FIRMWARE_VERSION);
  web.sendContent(F("'><script defer src='/assets/cozy.js?v="));
  web.sendContent(FIRMWARE_VERSION);
  web.sendContent(F("'></script></head><body><a class='skip' href='#content'>Zum Inhalt</a><div class='shell'>"
                    "<header class='topbar'><a class='brand' href='/'><span class='brand-mark' aria-hidden='true'></span>Claude Display</a>"
                    "<nav aria-label='Hauptnavigation'><a href='/'>Übersicht</a><a href='/settings'>Einstellungen</a><a href='/update'>Firmware</a></nav>"
                    "<button class='theme-toggle' id='themeToggle' type='button' aria-label='Zwischen hellem und dunklem Design wechseln'>Heller</button></header>"
                    "<div class='page-heading'><div><p class='eyebrow'>Ein kleiner Moment Überblick</p><h1>"));
  web.sendContent(title);
  web.sendContent(F("</h1></div><div class='clock'><span class='dot' aria-hidden='true'></span><time id='deviceClock' data-unix='"));
  const time_t now = time(nullptr);
  web.sendContent(String(now >= 1577836800 ? static_cast<uint32_t>(now) : 0));
  web.sendContent(F("'>Uhr wird synchronisiert</time><br>Europe/Berlin</div></div><main id='content'"));
  if (web.uri() != "/") web.sendContent(F(" class='settings-layout'"));
  web.sendContent(F("><noscript><p class='notice'>Live-Werte benötigen JavaScript. Formulare funktionieren auch ohne JavaScript.</p></noscript>"));
}

void endPage() {
  web.sendContent(F("</main><footer class='footer'><span>Ein bisschen Ruhe. Alles im Blick.</span><span>Firmware "));
  web.sendContent(FIRMWARE_VERSION);
  web.sendContent(F(" · <a href='/api/status'>Status-API</a></span></footer></div>"
                    "<div id='toast' class='toast' role='status' aria-live='polite' hidden></div></body></html>"));
  web.sendContent("");
}

void sendLiveValue(const __FlashStringHelper *key, const String &value, const char *format = "raw") {
  web.sendContent(F("<span data-live='"));
  web.sendContent(key);
  web.sendContent(F("' data-format='"));
  web.sendContent(format);
  web.sendContent(F("'>"));
  sendHtmlEscaped(value);
  web.sendContent(F("</span>"));
}

bool requireSettingsAuth() {
  if (web.authenticate("admin", config.webPassword.c_str())) return true;
  web.requestAuthentication(BASIC_AUTH, "Claude Display");
  return false;
}

void handleStatus() {
  beginPage(F("Alles im Blick."));
  web.sendContent(F("<div id='dashboard' class='grid'><section class='card limits'><div class='card-top'>"
                    "<h2>Platz für deine Ideen</h2><span class='tag' id='claudeBadge'>"));
  sendHtmlEscaped(claudeState);
  web.sendContent(F("</span></div><div class='gauges'><div class='limit'><div class='gauge' id='sessionGauge' role='img' aria-label='Sessionkontingent'>"
                    "<div class='gauge-center'><strong>"));
  sendLiveValue(F("five_hour.remaining"), percentText(sessionLimit), "integer");
  web.sendContent(F("<span aria-hidden='true'>%</span></strong><small>noch frei</small></div></div><h3>Diese Session</h3><p class='reset' id='sessionReset' data-reset='"));
  sendHtmlEscaped(sessionLimit.resetsAt);
  web.sendContent(F("'>"));
  sendHtmlEscaped(sessionLimit.resetsAt.length() ? sessionLimit.resetsAt : String(F("Reset noch unbekannt")));
  web.sendContent(F("</p></div><div class='limit'><div class='gauge sage' id='weeklyGauge' role='img' aria-label='Wochenkontingent'>"
                    "<div class='gauge-center'><strong>"));
  sendLiveValue(F("seven_day.remaining"), percentText(weeklyLimit), "integer");
  web.sendContent(F("<span aria-hidden='true'>%</span></strong><small>noch frei</small></div></div><h3>Diese Woche</h3><p class='reset' id='weeklyReset' data-reset='"));
  sendHtmlEscaped(weeklyLimit.resetsAt);
  web.sendContent(F("'>"));
  sendHtmlEscaped(weeklyLimit.resetsAt.length() ? weeklyLimit.resetsAt : String(F("Reset noch unbekannt")));
  web.sendContent(F("</p></div></div><div class='card-bottom'><div><p>"));
  sendLiveValue(F("claude_state"), claudeState);
  web.sendContent(F("</p><p class='live-note'><span class='dot' aria-hidden='true'></span><span id='liveState'>Letzter Gerätestand</span></p></div>"
                    "<form method='post' action='/refresh'><button type='submit'>Werte auffrischen</button></form></div></section>"
                    "<section class='card weather'><div><div class='card-top'><h2>Ein Blick nach draußen</h2><span class='weather-symbol' id='weatherSymbol' aria-hidden='true'>☁</span></div>"
                    "<p class='big-value'>"));
  sendLiveValue(F("weather.temperature_c"), weather.valid ? String(weather.temperature, 1) : String(F("--")), "decimal");
  web.sendContent(F("<small> °C</small></p><p class='place'>"));
  sendLiveValue(F("weather.location"), config.weatherName.length() ? config.weatherName : String(F("Standort noch offen")));
  web.sendContent(F("</p><p class='caption'>"));
  sendLiveValue(F("weather.state"), weather.state);
  web.sendContent(F("</p></div><dl class='facts'><div><dt>Luftfeuchtigkeit</dt><dd>"));
  sendLiveValue(F("weather.humidity_pct"), weather.valid ? String(weather.humidity, 0) : String(F("--")), "integer");
  web.sendContent(F(" %</dd></div><div><dt>Wind</dt><dd>"));
  sendLiveValue(F("weather.wind_kmh"), weather.valid ? String(weather.wind, 1) : String(F("--")), "decimal");
  web.sendContent(F(" km/h</dd></div></dl><p class='caption'>Wetterdaten von <a href='https://open-meteo.com/'>Open-Meteo</a></p></section>"
                    "<section class='card'><h2>Hier bei dir</h2><p class='big-value'>"));
  sendLiveValue(F("temperature_c"), isnan(temperatureC) ? String(F("--")) : String(temperatureC, 1), "decimal");
  web.sendContent(F("<small> °C</small></p><p class='caption'>Temperatur am Gerät</p><dl class='facts'><div><dt>Luftfeuchtigkeit</dt><dd>"));
  sendLiveValue(F("humidity_pct"), isnan(humidityPct) ? String(F("--")) : String(humidityPct, 0), "integer");
  web.sendContent(F(" %</dd></div><div><dt>Luftdruck</dt><dd>"));
  sendLiveValue(F("pressure_hpa"), isnan(pressureHpa) ? String(F("--")) : String(pressureHpa, 1), "decimal");
  web.sendContent(F(" hPa</dd></div></dl></section><section class='card network'><div class='card-top'><h2>Dein Display</h2><span class='tag' id='displayPage'>"));
  web.sendContent(pages.weather() ? "Wetter" : (pages.status() ? "Status" : "Claude"));
  web.sendContent(F("</span></div><dl class='facts'><div><dt>WLAN</dt><dd>"));
  sendLiveValue(F("ssid"), WiFi.SSID());
  web.sendContent(F("</dd></div><div><dt>IP-Adresse</dt><dd>"));
  sendLiveValue(F("ip"), WiFi.localIP().toString());
  web.sendContent(F("</dd></div><div><dt>Signalstärke</dt><dd>"));
  sendLiveValue(F("rssi"), String(WiFi.RSSI()), "integer");
  web.sendContent(F(" dBm</dd></div><div><dt>Freier RAM</dt><dd>"));
  sendLiveValue(F("free_heap"), String(ESP.getFreeHeap() / 1024.0f, 1), "kib");
  web.sendContent(F(" KiB</dd></div><div><dt>Größter freier Block</dt><dd>"));
  sendLiveValue(F("max_free_block"), String(ESP.getMaxFreeBlockSize() / 1024.0f, 1), "kib");
  web.sendContent(F(" KiB</dd></div><div><dt>Fragmentierung</dt><dd>"));
  sendLiveValue(F("heap_fragmentation"), String(ESP.getHeapFragmentation()), "integer");
  web.sendContent(F(" %</dd></div><div><dt>TrueConf · ungelesen</dt><dd>"));
  sendLiveValue(F("trueconf.unread"), trueConfResult.valid ? String(trueConfResult.unread) : String(F("--")), "integer");
  web.sendContent(F("</dd></div><div><dt>TrueConf</dt><dd>"));
  sendLiveValue(F("trueconf.state"), trueConfResult.state);
  web.sendContent(F("</dd></div></dl></section></div>"));
  endPage();
}

void sendTrueConfSettings() {
  web.sendContent(F("<div class='card'><h2>TrueConf-Nachrichten</h2><form method='post' action='/settings/trueconf'>"
                    "<label><input type='checkbox' style='width:auto' name='enabled' "));
  if (config.trueConf.enabled) web.sendContent(F("checked "));
  web.sendContent(F("> Zähler aktivieren</label><label>Server (DNS-Name oder IPv4, ohne https://)<input name='host' maxlength='253' value='"));
  sendHtmlEscaped(config.trueConf.host);
  web.sendContent(F("'></label><label>HTTPS/WSS-Port<input type='number' name='port' min='1' max='65535' required value='"));
  web.sendContent(String(config.trueConf.port));
  web.sendContent(F("'></label><label>TrueConf-Benutzername<input name='username' maxlength='254' autocomplete='username' value='"));
  sendHtmlEscaped(config.trueConf.username);
  web.sendContent(F("'></label><label>TrueConf-Passwort (leer = beibehalten)<input type='password' name='password' autocomplete='new-password'></label>"
                    "<label>Bridge-JWT (optional; hat Vorrang vor Benutzer/Passwort)<input type='password' name='token' autocomplete='new-password' placeholder='"));
  web.sendContent(config.trueConf.token.length() ? "gespeichert; leer = beibehalten" : "nicht gespeichert");
  web.sendContent(F("'></label><label><input type='checkbox' style='width:auto' name='clearToken'> Gespeicherten JWT löschen</label>"
                    "<label><input type='checkbox' style='width:auto' name='clearPassword'> Gespeichertes TrueConf-Passwort löschen</label>"
                    "<label>OAuth-App-ID (optional; für Anmeldung über REST-Token-Austausch, Recht version:read)<input name='oauthClientId' maxlength='254' value='"));
  sendHtmlEscaped(config.trueConf.oauthClientId);
  web.sendContent(F("'></label><label>SHA1-Fingerprint des Serverzertifikats (40 Hex-Zeichen)<input name='fingerprint' maxlength='80' value='"));
  sendHtmlEscaped(config.trueConf.fingerprint);
  web.sendContent(F("'></label><label><input type='checkbox' style='width:auto' name='insecure' "));
  if (config.trueConf.insecure) web.sendContent(F("checked "));
  web.sendContent(F("> Zertifikatsprüfung ausschalten (unsicher)</label>"
                    "<label>Abrufintervall in Sekunden (30–3600)<input type='number' name='intervalSec' min='30' max='3600' required value='"));
  web.sendContent(String(config.trueConf.intervalSec));
  web.sendContent(F("'></label><p>Passwort und JWT werden auf dem Gerät gespeichert. Es werden nur Chat-Zähler abgefragt; keine Gelesenbestätigungen gesendet.</p>"
                    "<button type='submit'>TrueConf-Einstellungen speichern</button></form>"
                    "<form method='post' action='/settings/trueconf/test'><button type='submit'>Gespeicherte TrueConf-Verbindung testen</button></form><p>Status: "));
  sendHtmlEscaped(trueConfResult.state);
  web.sendContent(F("</p></div>"));
}

void sendWeatherSettings() {
  web.sendContent(F("<div class='card'><h2>Wetter-Standort</h2><form method='post' action='/settings/weather'>"
                    "<p>Standort über Koordinaten festlegen. Kurzer Touch wechselt die Displayseite. Alle Felder leeren deaktiviert den Wetterabruf.</p>"
                    "<label>Ortsname<input name='weatherName' maxlength='40' value='"));
  sendHtmlEscaped(config.weatherName);
  web.sendContent(F("'></label><label>Breitengrad (-90 bis 90)<input name='latitude' type='number' step='any' min='-90' max='90' value='"));
  sendHtmlEscaped(config.weatherLatitude);
  web.sendContent(F("'></label><label>Längengrad (-180 bis 180)<input name='longitude' type='number' step='any' min='-180' max='180' value='"));
  sendHtmlEscaped(config.weatherLongitude);
  web.sendContent(F("'></label><button type='submit'>Wetter-Standort speichern</button></form>"
                    "<p>Daten: <a href='https://open-meteo.com/'>Open-Meteo</a> (CC BY 4.0), Aktualisierung alle 15 Minuten.</p><p>Status: "));
  sendHtmlEscaped(weather.state);
  web.sendContent(F("</p></div>"));
}

void handleSettings() {
  if (!requireSettingsAuth()) return;
  beginPage(F("Einstellungen"));
  web.sendContent(F("<div class='card'><h2>Display-Einstellungen</h2><form method='post' action='/settings/name'>"
                    "<label>Name auf dem Display<input name='displayName' maxlength='24' required value='"));
  sendHtmlEscaped(config.displayName);
  web.sendContent(F("'></label><label><input type='checkbox' name='showIp' value='1' style='width:auto' "));
  if (config.showIp) web.sendContent(F("checked "));
  web.sendContent(F("> IP-Adresse auf dem Display anzeigen</label>"
                    "<label>NTP-Zeitserver für Reset-Countdowns<input name='ntpServer' maxlength='253' required value='"));
  sendHtmlEscaped(config.ntpServer);
  web.sendContent(F("'></label>"
                    "<button type='submit'>Display-Einstellungen speichern</button></form></div>"
                    "<div class='card'><h2>Claude-Zugang</h2><form method='post' action='/settings/test'>"
                    "<button type='submit'>Gespeicherten Session-Key testen</button></form>"
                    "<form method='post' action='/settings'>"
                    "<button type='submit'>Claude-Einstellungen und Session-Key speichern</button>"
                    "<p class='warn'>Der sessionKey entspricht deiner Claude-Anmeldung. Das Feld leer lassen, um den gespeicherten Wert beizubehalten.</p>"
                    "<label>Claude sessionKey<input type='password' name='sessionKey' autocomplete='new-password' placeholder='"));
  web.sendContent(config.sessionKey.length() ? "gespeichert" : "nicht gespeichert");
  web.sendContent(F("'></label><label>Organisations-ID<input name='orgId' value='"));
  sendHtmlEscaped(config.orgId);
  web.sendContent(F("'></label><label>Weitere Cookies (optional)<input type='password' name='extraCookies' placeholder='"));
  web.sendContent(config.extraCookies.length() ? "gespeichert" : "z. B. cf_clearance=..." );
  web.sendContent(F("'></label><label>User-Agent<input id='userAgent' name='userAgent' value='"));
  sendHtmlEscaped(config.userAgent);
  web.sendContent(F("'></label><button type='button' data-user-agent='"));
  sendHtmlEscaped(FIREFOX_WINDOWS_USER_AGENT);
  web.sendContent(F("' onclick=\"document.getElementById('userAgent').value=this.dataset.userAgent\">Firefox unter Windows einsetzen</button>"
                    "<p>Danach die Claude-Einstellungen speichern. Bei zusätzlichen Browser-Cookies den exakten User-Agent dieses Browsers eintragen.</p>"
                    "<label>Abrufintervall in Sekunden (60–3600)<input type='number' min='60' max='3600' name='intervalSec' value='"));
  web.sendContent(String(config.intervalSec));
  web.sendContent(F("'></label><button type='submit'>Claude-Einstellungen und Session-Key speichern</button></form>"
                    "<form method='post' action='/settings/test'><p>Vor dem Test einen neuen Key speichern. Getestet wird der gespeicherte Key.</p>"
                    "<button type='submit' >Gespeicherten Session-Key testen</button></form></div>"
                    "<div class='card'><h2>Web-Passwort ändern</h2><form method='post' action='/settings/password'>"
                    "<label>Aktuelles Passwort<input type='password' name='currentPassword' autocomplete='current-password' required></label>"
                    "<label>Neues Passwort (mindestens 8 Zeichen)<input type='password' name='webPassword' autocomplete='new-password' minlength='8' required></label>"
                    "<label>Neues Passwort bestätigen<input type='password' name='confirmPassword' autocomplete='new-password' minlength='8' required></label>"
                    "<button type='submit'>Passwort ändern</button></form></div>"
                    "<div class='card'><p>Benutzername der Einstellungsseite: <b>admin</b></p>"
                    "<p>Falls ein TTP223 angeschlossen ist: mindestens 5 Sekunden berühren, um WLAN neu einzurichten.</p></div>"));
  sendWeatherSettings();
  sendTrueConfSettings();
  endPage();
}

void settingsResult(const String &message) {
  beginPage(F("Einstellungen"));
  web.sendContent(F("<div class='card'><p role='status'>"));
  sendHtmlEscaped(message);
  web.sendContent(F("</p><a href='/settings'>Zurück zu den Einstellungen</a></div>"));
  endPage();
}

void handleSaveWeather() {
  if (!requireSettingsAuth()) return;
  String name = web.arg("weatherName"), lat = web.arg("latitude"), lon = web.arg("longitude");
  name.trim(); lat.trim(); lon.trim();
  const bool disabled = !name.length() && !lat.length() && !lon.length();
  if (!disabled && (!name.length() || name.length() > 40 ||
      !validWeatherCoordinate(lat.c_str(), 90) || !validWeatherCoordinate(lon.c_str(), 180))) {
    settingsResult(F("Bitte Ortsname und gültige Koordinaten mit Dezimalpunkt eingeben.")); return;
  }
  const Config previous = config;
  config.weatherName = name; config.weatherLatitude = lat; config.weatherLongitude = lon;
  if (!saveConfig()) {
    config = previous;
    settingsResult(F("Wetter-Standort konnte nicht gespeichert werden.")); return;
  }
  weather = WeatherResult();
  forceWeatherRefresh = true;
  displayNeedsFullRedraw = true;
  settingsResult(F("Wetter-Standort gespeichert."));
}

void handleSaveDisplayName() {
  if (!requireSettingsAuth()) return;
  String name = web.arg("displayName");
  name.trim();
  if (!name.length() || name.length() > 24) {
    settingsResult(F("Bitte einen Display-Namen mit 1 bis 24 Bytes eingeben (Umlaute benötigen mehrere Bytes)."));
    return;
  }
  const String previous = config.displayName;
  const bool previousShowIp = config.showIp;
  const String previousNtp = config.ntpServer;
  String ntp = web.arg("ntpServer");
  ntp.trim();
  if (!ntp.length() || ntp.length() > 253 || ntp.indexOf('/') >= 0 || ntp.indexOf(' ') >= 0) {
    settingsResult(F("Bitte einen gültigen NTP-Servernamen eingeben."));
    return;
  }
  config.displayName = name;
  config.showIp = web.hasArg("showIp");
  config.ntpServer = ntp;
  if (!saveConfig()) {
    config.displayName = previous;
    config.showIp = previousShowIp;
    config.ntpServer = previousNtp;
    settingsResult(F("Display-Einstellungen konnten nicht gespeichert werden."));
    return;
  }
  displayDirty = true;
  configTime(LOCAL_TIMEZONE, config.ntpServer.c_str());
  settingsResult(F("Display-Einstellungen gespeichert. Die Anzeige wird sofort aktualisiert."));
}

void handleSaveTrueConf() {
  if (!requireSettingsAuth()) return;
  TrueConfConfig next = config.trueConf;
  next.enabled = web.hasArg("enabled");
  next.host = web.arg("host"); next.host.trim();
  const long port = web.arg("port").toInt();
  const long interval = web.arg("intervalSec").toInt();
  if (port < 1 || port > 65535 || interval < 30 || interval > 3600) {
    settingsResult(F("TrueConf-Port oder Intervall außerhalb des gültigen Bereichs.")); return;
  }
  next.port = port;
  next.intervalSec = interval;
  next.username = web.arg("username"); next.username.trim();
  next.oauthClientId = web.arg("oauthClientId"); next.oauthClientId.trim();
  next.fingerprint = web.arg("fingerprint"); next.fingerprint.trim();
  next.insecure = web.hasArg("insecure");
  if (web.hasArg("clearPassword")) next.password = "";
  if (web.arg("password").length()) next.password = web.arg("password");
  if (web.hasArg("clearToken")) next.token = "";
  if (web.arg("token").length()) { next.token = web.arg("token"); next.token.trim(); }
  if (next.host.length() > 253 || next.username.length() > 254 || next.oauthClientId.length() > 254 ||
      next.password.length() > 512 || next.token.length() > 4096 || next.fingerprint.length() > 80) {
    settingsResult(F("TrueConf-Eingabe zu lang.")); return;
  }
  const String error = validateTrueConfConfig(next);
  if (error.length()) { settingsResult(error); return; }
  const TrueConfConfig previous = config.trueConf;
  config.trueConf = next;
  if (!saveConfig()) {
    config.trueConf = previous;
    settingsResult(F("TrueConf-Einstellungen konnten nicht gespeichert werden.")); return;
  }
  trueConfResult = TrueConfResult();
  trueConfResult.state = next.enabled ? "Warte auf Abruf" : "Deaktiviert";
  forceTrueConfRefresh = next.enabled;
  displayDirty = true;
  settingsResult(F("TrueConf-Einstellungen dauerhaft gespeichert."));
}

void refreshTrueConf() {
  if (pages.requestsPaused()) { forceTrueConfRefresh = true; return; }
  releaseDisplayScratch();
  trueConfResult = fetchTrueConfUnread(config.trueConf);
  lastTrueConfMs = millis();
  forceTrueConfRefresh = false;
  displayDirty = true;
}

void handleTestTrueConf() {
  if (!requireSettingsAuth()) return;
  if (pages.requestsPaused()) {
    forceTrueConfRefresh = true;
    settingsResult(F("TrueConf-Abfragen sind auf der Wetter- oder Statusseite pausiert. Der Abruf folgt nach dem Seitenwechsel."));
    return;
  }
  refreshTrueConf();
  settingsResult(trueConfResult.valid ? "TrueConf verbunden. Ungelesene Nachrichten: " + String(trueConfResult.unread)
                                     : "TrueConf-Test: " + trueConfResult.state);
}

void handleChangePassword() {
  if (!requireSettingsAuth()) return;
  if (web.arg("currentPassword") != config.webPassword) {
    settingsResult(F("Das aktuelle Passwort ist falsch."));
    return;
  }
  const String password = web.arg("webPassword");
  if (password.length() < 8 || password != web.arg("confirmPassword")) {
    settingsResult(F("Das neue Passwort muss mindestens 8 Zeichen lang sein und die Bestätigung muss übereinstimmen."));
    return;
  }
  const String previous = config.webPassword;
  config.webPassword = password;
  if (!saveConfig()) {
    config.webPassword = previous;
    settingsResult(F("Passwort konnte nicht gespeichert werden."));
    return;
  }
  settingsResult(F("Passwort geändert. Bei der nächsten Anmeldung Benutzer admin und das neue Passwort verwenden."));
}

void fetchClaudeUsage();

void handleTestSessionKey() {
  if (!requireSettingsAuth()) return;
  if (pages.requestsPaused()) {
    forceClaudeRefresh = true;
    settingsResult(F("Claude-Abfragen sind auf der Wetter- oder Statusseite pausiert. Der Abruf folgt nach dem Seitenwechsel."));
    return;
  }
  forceClaudeRefresh = true;
  settingsResult(F("Claude-Test vorgemerkt. Der Abruf startet nach Abschluss dieser Webanfrage. Ergebnis und Fehlerdetails stehen auf der Statusseite."));
  web.client().stop();
}

void handleSaveSettings() {
  if (!requireSettingsAuth()) return;
  const Config previous = config;
  const String newSessionKey = web.arg("sessionKey");
  const String newExtraCookies = web.arg("extraCookies");
  if (newSessionKey.length()) {
    config.sessionKey = newSessionKey;
    if (config.sessionKey.startsWith("sessionKey=")) config.sessionKey.remove(0, 11);
  }
  if (newExtraCookies.length()) config.extraCookies = newExtraCookies;
  config.orgId = web.arg("orgId");
  config.userAgent = web.arg("userAgent");
  config.intervalSec = constrain(static_cast<uint32_t>(web.arg("intervalSec").toInt()), 60UL, 3600UL);
  normalizeClaudeConfig();
  if (config.orgId.length() && findOrganizationUuid(config.orgId.c_str(), config.orgId.length()) != 0) {
    config = previous;
    settingsResult(F("Nicht gespeichert: Die Organisations-ID muss eine UUID sein. Aus dem PS1 kann auch Name [UUID] übernommen werden. Leer lassen startet die automatische Suche."));
    return;
  }
  const bool ok = saveConfig();
  if (!ok) config = previous;
  settingsResult(ok ? "Claude-Einstellungen und Session-Key gespeichert. Der gespeicherte Key kann jetzt getestet werden."
                    : "Speichern fehlgeschlagen. Bitte erneut versuchen.");
}

void handleUpdatePage() {
  if (!requireSettingsAuth()) return;
  const uint32_t freeSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  beginPage(F("Firmware-Update"));
  web.sendContent(F("<div class='card'><p>Hier ausschließlich die für diesen Wemos D1 Mini erzeugte <code>firmware.bin</code> auswählen.</p>"
                    "<p>Maximale Dateigröße: "));
  web.sendContent(String(freeSketchSpace / 1024));
  web.sendContent(F(" KiB</p><form id='updateForm' method='post' action='/update' enctype='multipart/form-data'>"
                    "<label>Firmware-Datei<input type='file' name='firmware' accept='.bin,application/octet-stream' required></label>"
                    "<button type='submit'>Firmware installieren</button></form>"
                    "<p id='progressText'>Noch kein Upload gestartet.</p>"
                    "<progress id='progress' value='0' max='100' style='width:100%'></progress></div>"
                    "<script>const f=document.getElementById('updateForm'),p=document.getElementById('progress'),t=document.getElementById('progressText');"
                    "f.addEventListener('submit',e=>{e.preventDefault();if(!confirm('Firmware jetzt installieren?'))return;"
                    "window.dispatchEvent(new Event('firmware-upload-start'));const x=new XMLHttpRequest();x.open('POST','/update');"
                    "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;t.textContent='Upload: '+n+' %';}};"
                    "x.onload=()=>{document.open();document.write(x.responseText);document.close();};"
                    "x.onerror=()=>{t.textContent='Upload fehlgeschlagen.';window.dispatchEvent(new Event('firmware-upload-end'));};x.send(new FormData(f));});</script>"));
  endPage();
}

void handleUpdateUpload() {
  HTTPUpload &upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    updateAuthenticated = web.authenticate("admin", config.webPassword.c_str());
    updateSuccessful = false;
    updateError = F("");
    if (!updateAuthenticated) return;
    if (!upload.filename.endsWith(".bin")) {
      updateError = F("Nur eine firmware.bin ist erlaubt.");
      return;
    }
    const uint32_t freeSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(freeSketchSpace, U_FLASH)) {
      updateError = "Update konnte nicht gestartet werden (Fehler " + String(Update.getError()) + ").";
      return;
    }
    displayNeedsFullRedraw = true;
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextFont(2);
    display.drawString("Firmware-Update", 120, 110);
    display.drawString("Nicht ausschalten!", 120, 135);
  } else if (updateAuthenticated && upload.status == UPLOAD_FILE_WRITE && !updateError.length()) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      updateError = "Schreibfehler " + String(Update.getError()) + ".";
    }
  } else if (updateAuthenticated && upload.status == UPLOAD_FILE_END && !updateError.length()) {
    if (Update.end(true)) updateSuccessful = true;
    else updateError = "Abschlussfehler " + String(Update.getError()) + ".";
  } else if (updateAuthenticated && upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    updateError = F("Upload wurde abgebrochen.");
  }
  yield();
}

void handleUpdateFinished() {
  if (!updateAuthenticated) {
    web.requestAuthentication(BASIC_AUTH, "Claude Display");
    return;
  }
  web.sendHeader("Connection", "close");
  if (!updateSuccessful || updateError.length()) {
    if (Update.isRunning()) Update.end();
    web.send(500, "text/html; charset=utf-8",
             "<!doctype html><html lang='de'><body><h1>Update fehlgeschlagen</h1><p>" +
                 htmlEscape(updateError.length() ? updateError : "Unbekannter Fehler") +
                 "</p><p><a href='/update'>Zurück</a></p></body></html>");
    return;
  }
  web.send(200, "text/html; charset=utf-8",
           "<!doctype html><html lang='de'><head><meta http-equiv='refresh' content='15;url=/'></head>"
           "<body><h1>Update erfolgreich</h1><p>Das Gerät startet neu.</p></body></html>");
  delay(250);
  ESP.restart();
}

void setupWebServer() {
  static bool routesRegistered = false;
  if (!routesRegistered) {
  web.on("/assets/cozy.css", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "public, max-age=86400");
    web.send_P(200, PSTR("text/css; charset=utf-8"), WEB_CSS);
  });
  web.on("/assets/cozy.js", HTTP_GET, []() {
    web.sendHeader("Cache-Control", "public, max-age=86400");
    web.send_P(200, PSTR("application/javascript; charset=utf-8"), WEB_JS);
  });
  web.on("/", HTTP_GET, handleStatus);
  web.on("/settings", HTTP_GET, handleSettings);
  web.on("/settings", HTTP_POST, handleSaveSettings);
  web.on("/settings/weather", HTTP_POST, handleSaveWeather);
  web.on("/settings/name", HTTP_POST, handleSaveDisplayName);
  web.on("/settings/trueconf", HTTP_POST, handleSaveTrueConf);
  web.on("/settings/trueconf/test", HTTP_POST, handleTestTrueConf);
  web.on("/api/time", HTTP_GET, []() {
    const time_t now = time(nullptr);
    JsonDocument doc;
    doc["clock_synced"] = now >= 1577836800;
    if (now >= 1577836800) doc["unix_time"] = static_cast<uint32_t>(now);
    else doc["unix_time"] = nullptr;
    doc["time_zone"] = "Europe/Berlin";
    String body;
    serializeJson(doc, body);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", body);
  });
  web.on("/settings/password", HTTP_POST, handleChangePassword);
  web.on("/settings/test", HTTP_POST, handleTestSessionKey);
  web.on("/update", HTTP_GET, handleUpdatePage);
  web.on("/update", HTTP_POST, handleUpdateFinished, handleUpdateUpload);
  web.on("/refresh", HTTP_POST, []() {
    if (!requireSettingsAuth()) return;
    forceClaudeRefresh = true;
    web.sendHeader("Location", "/");
    web.send(303, "text/plain", "Aktualisierung gestartet");
  });
  web.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    if (!isnan(temperatureC)) doc["temperature_c"] = temperatureC;
    if (!isnan(humidityPct)) doc["humidity_pct"] = humidityPct;
    if (!isnan(pressureHpa)) doc["pressure_hpa"] = pressureHpa;
    if (sessionLimit.valid) {
      doc["five_hour"]["used"] = sessionLimit.used;
      doc["five_hour"]["remaining"] = 100.0f - sessionLimit.used;
      doc["five_hour"]["resets_at"] = sessionLimit.resetsAt;
    }
    if (weeklyLimit.valid) {
      doc["seven_day"]["used"] = weeklyLimit.used;
      doc["seven_day"]["remaining"] = 100.0f - weeklyLimit.used;
      doc["seven_day"]["resets_at"] = weeklyLimit.resetsAt;
    }
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["claude_state"] = claudeState;
    doc["clock_synced"] = time(nullptr) >= 1577836800;
    doc["trueconf"]["enabled"] = config.trueConf.enabled;
    doc["trueconf"]["state"] = trueConfResult.state;
    if (trueConfResult.valid) doc["trueconf"]["unread"] = trueConfResult.unread;
    doc["weather"]["location"] = config.weatherName;
    doc["weather"]["state"] = weather.state;
    doc["weather"]["valid"] = weather.valid;
    if (weather.valid) {
      doc["weather"]["code"] = weather.code;
      doc["weather"]["temperature_c"] = weather.temperature;
      doc["weather"]["humidity_pct"] = weather.humidity;
      doc["weather"]["wind_kmh"] = weather.wind;
    }
    doc["touch"]["raw_high"] = digitalRead(Pins::TOUCH) == HIGH;
    doc["touch"]["ready"] = touchInput.isArmed();
    doc["touch"]["pressed"] = touchInput.isPressed();
    doc["display_page"] = pages.status() ? "status" : (pages.weather() ? "weather" : "claude");
    doc["free_heap"] = ESP.getFreeHeap();
    doc["max_free_block"] = ESP.getMaxFreeBlockSize();
    doc["heap_fragmentation"] = ESP.getHeapFragmentation();
    String body;
    serializeJson(doc, body);
    web.send(200, "application/json", body);
  });
  web.onNotFound([]() { web.send(404, "text/plain", "Nicht gefunden"); });
    routesRegistered = true;
  }
  web.begin();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  normalWebRunning = true;
  webServerReadyMs = millis();
}

String cookieHeader() {
  String cookie;
  cookie.reserve(11 + config.sessionKey.length() + (config.extraCookies.length() ? 2 + config.extraCookies.length() : 0));
  cookie = F("sessionKey=");
  cookie += config.sessionKey;
  if (config.extraCookies.length()) {
    cookie += "; ";
    cookie += config.extraCookies;
  }
  return cookie;
}

void applyClaudeHeaders(HTTPClient &http) {
  // getStream() umgeht den HTTPClient-Chunkdecoder. HTTP/1.0 fordert
  // eine ungechunkte Antwort an; komprimierte Antworten nicht anfordern.
  http.useHTTP10(true);
  http.setTimeout(20000);
  const char *responseHeaders[] = {"cf-mitigated", "Content-Encoding", "Transfer-Encoding"};
  http.collectHeaders(responseHeaders, 3);
  http.addHeader("Cookie", cookieHeader());
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("Referer", "https://claude.ai/settings/usage");
  // Metadaten eines gleichursprünglichen Fetch-Aufrufs der Nutzungsseite.
  http.addHeader("Accept-Language", "de-DE,de;q=0.9,en-US;q=0.7,en;q=0.5");
  http.addHeader("Sec-Fetch-Dest", "empty");
  http.addHeader("Sec-Fetch-Mode", "cors");
  http.addHeader("Sec-Fetch-Site", "same-origin");
  http.setUserAgent(config.userAgent);
}

String httpErrorText(int code) {
  if (code < 0) return "Verbindungsfehler " + String(code) + ": " + HTTPClient::errorToString(code);
  switch (code) {
    case 401: return F("Sitzung abgelaufen (401)");
    case 403: return F("Zugriff/Cloudflare (403)");
    case 404: return F("Endpunkt nicht gefunden oder Organisation nicht zugänglich (404)");
    case 429: return F("Zu viele Anfragen (429)");
    default: return "HTTP-Fehler " + String(code);
  }
}

String claudeRequestError(int code, HTTPClient &http, BearSSL::WiFiClientSecure &client) {
  if (code == 403 && http.header("cf-mitigated").equalsIgnoreCase("challenge")) {
    return F("Cloudflare-Challenge (403). Weitere Cookies und User-Agent wie im PS1-Skript prüfen.");
  }
  String message = httpErrorText(code);
  if (code < 0) {
    char detail[160] = {};
    const int tlsError = client.getLastSSLError(detail, sizeof(detail));
    if (tlsError) message += "; TLS " + String(tlsError) + ": " + String(detail);
    message += "; freier Heap " + String(ESP.getFreeHeap()) + " Bytes";
    // Keine Cookies, Header oder Antwortinhalte protokollieren.
  }
  return message;
}

bool claudeResponseEncodingSupported(HTTPClient &http) {
  const String encoding = http.header("Content-Encoding");
  const String transfer = http.header("Transfer-Encoding");
  if ((encoding.length() && !encoding.equalsIgnoreCase("identity")) ||
      (transfer.length() && !transfer.equalsIgnoreCase("identity"))) {
    claudeState = F("Nicht unterstützte Antwortkodierung trotz HTTP/1.0 und Accept-Encoding: identity");
    return false;
  }
  return true;
}

bool prepareClaudeTlsRequest() {
  releaseDisplayScratch();
  weatherAnimation.release();
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxFreeBlockSize();
  uint32_t iramFree = 0, iramLargest = 0;
  {
    HeapSelectIram selectIram;
    iramFree = ESP.getFreeHeap();
    iramLargest = ESP.getMaxFreeBlockSize();
  }
  Serial.printf("Claude TLS: DRAM=%lu, Block=%lu; IRAM=%lu, Block=%lu\n",
                static_cast<unsigned long>(freeHeap), static_cast<unsigned long>(largestBlock),
                static_cast<unsigned long>(iramFree), static_cast<unsigned long>(iramLargest));
  if (hasClaudeTlsMemory(freeHeap, largestBlock, iramFree, iramLargest)) return true;
  claudeState = F("Claude-Abruf ausgesetzt: zu wenig TLS-Speicher. Heap ");
  claudeState += String(freeHeap) + F(" Bytes, groesster Block ") + String(largestBlock) +
      F(" Bytes; IRAM ") + String(iramFree) + F(" Bytes, Block ") + String(iramLargest);
  return false;
}

bool discoverOrganization() {
  BearSSL::WiFiClientSecure client;
  // ESP8266 besitzt keinen gepflegten System-Zertifikatsspeicher. Siehe README.
  client.setInsecure();
  // Ohne ausgehandeltes MFLN muss der volle TLS-Record (16 KiB) hineinpassen.
  client.setBufferSizes(16384, 512);
  HTTPClient http;
  if (!http.begin(client, CLAUDE_BASE)) {
    claudeState = F("Organisationssuche: HTTPS konnte nicht starten");
    return false;
  }
  applyClaudeHeaders(http);
  if (!prepareClaudeTlsRequest()) return false;
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    claudeState = "Organisationssuche: " + claudeRequestError(code, http, client);
    http.end();
    return false;
  }
  if (!claudeResponseEncodingSupported(http)) {
    http.end();
    return false;
  }
  JsonDocument filter;
  filter[0]["uuid"] = true;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  if (!error && doc.is<JsonArray>() && doc.size() > 0) config.orgId = doc[0]["uuid"].as<String>();
  http.end();
  if (error) {
    claudeState = "Organisationssuche: JSON-Fehler: " + String(error.c_str());
    return false;
  }
  if (config.orgId.length()) saveConfig();
  return config.orgId.length() > 0;
}

void readBucket(JsonObjectConst root, const char *key, LimitBucket &bucket) {
  JsonObjectConst value = root[key];
  if (value.isNull() || value["utilization"].isNull()) {
    bucket.valid = false;
    return;
  }
  bucket.used = constrain(value["utilization"].as<float>(), 0.0f, 100.0f);
  bucket.resetsAt = value["resets_at"] | "";
  bucket.resetEpoch = parseResetTimestamp(bucket.resetsAt.c_str());
  bucket.valid = true;
}

void fetchClaudeUsage() {
  if (pages.requestsPaused()) { forceClaudeRefresh = true; return; }
  normalizeClaudeConfig();
  if (WiFi.status() != WL_CONNECTED || !config.sessionKey.length()) {
    claudeState = config.sessionKey.length() ? "WLAN getrennt" : "sessionKey fehlt";
    return;
  }
  claudeState = F("Aktualisiere ...");
  displayDirty = true;
  if (!config.orgId.length() && !discoverOrganization()) {
    if (claudeState == "Aktualisiere ...") claudeState = F("Keine Organisation gefunden");
    return;
  }
  if (config.orgId.length() != 36 || findOrganizationUuid(config.orgId.c_str(), config.orgId.length()) != 0) {
    claudeState = F("Ungültige Organisations-ID. UUID aus den PS1-Einstellungen übernehmen oder Feld für automatische Suche leeren.");
    return;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(16384, 512);
  HTTPClient http;
  const String url = String(CLAUDE_BASE) + "/" + config.orgId + "/usage";
  if (!http.begin(client, url)) {
    claudeState = F("HTTPS konnte nicht starten");
    return;
  }
  applyClaudeHeaders(http);
  if (!prepareClaudeTlsRequest()) return;
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    claudeState = "Limit-Abruf: " + claudeRequestError(code, http, client);
    if (code == 404) claudeState += "; verwendete Org-ID: " + config.orgId + ". Mit der ID im PS1 vergleichen.";
    http.end();
    return;
  }
  if (!claudeResponseEncodingSupported(http)) {
    http.end();
    return;
  }

  JsonDocument filter;
  for (const char *key : {"five_hour", "seven_day"}) {
    filter[key]["utilization"] = true;
    filter[key]["resets_at"] = true;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (error) {
    claudeState = "JSON-Fehler: " + String(error.c_str());
    return;
  }
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  readBucket(root, "five_hour", sessionLimit);
  readBucket(root, "seven_day", weeklyLimit);
  if (!sessionLimit.valid && !weeklyLimit.valid) {
    claudeState = F("Antwort ohne bekannte Limits");
    return;
  }
  claudeState = F("Verbunden");
  lastClaudeUpdate = String(millis() / 60000UL) + " min nach Start";
  displayDirty = true;
}

void detectSensor() {
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
  if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
    sensorType = SensorType::BME280;
  } else if (bmp.begin(0x76) || bmp.begin(0x77)) {
    sensorType = SensorType::BMP280;
  }
}

void readSensor() {
  if (sensorType == SensorType::BME280) {
    temperatureC = bme.readTemperature();
    humidityPct = bme.readHumidity();
    pressureHpa = bme.readPressure() / 100.0f;
  } else if (sensorType == SensorType::BMP280) {
    temperatureC = bmp.readTemperature();
    humidityPct = NAN;
    pressureHpa = bmp.readPressure() / 100.0f;
  }
  displayDirty = true;
}

struct RingDrawState {
  RingStartup startup;
  int angle = 0;
  uint16_t color = TFT_DARKGREY;
  uint16_t track = LimitRingColor::GREY;
  bool valid = false;
};

RingDrawState sessionRing, weeklyRing;

bool claudeStartupActive() {
  return !pages.requestsPaused() &&
      ((sessionLimit.valid && !sessionRing.startup.finished()) ||
       (weeklyLimit.valid && !weeklyRing.startup.finished()));
}

uint16_t cozyLimitColor(float used, uint16_t base) {
  // Only near exhaustion, gently shift honey/sage towards warm apricot.
  const float blend = constrain((used - 75.0f) / 25.0f, 0.0f, 1.0f);
  const uint16_t apricot = 0xFBAE;
  const int r = (base >> 11) & 31, g = (base >> 5) & 63, b = base & 31;
  return static_cast<uint16_t>(
      (static_cast<int>(r + (((apricot >> 11) & 31) - r) * blend) << 11) |
      (static_cast<int>(g + (((apricot >> 5) & 63) - g) * blend) << 5) |
       static_cast<int>(b + ((apricot & 31) - b) * blend));
}

void drawLimitRing(int radius, int thickness, const LimitBucket &bucket,
                   RingDrawState &previous, bool fullRedraw, uint16_t baseColor) {
  const int inner = radius - thickness;
  const int target = bucket.valid
      ? static_cast<int>(360.0f * constrain(100.0f - bucket.used, 0.0f, 100.0f) / 100.0f) : 0;
  const bool reset = fullRedraw || bucket.valid != previous.valid;
  // Small changes settle quickly; larger changes move gently over several frames.
  const bool startup = bucket.valid && !previous.startup.finished();
  if (!bucket.valid) previous.startup.pause();
  const int angle = startup ? previous.startup.angle(target, millis()) :
      (reset ? target : previous.angle + constrain(target - previous.angle, -6, 6));
  const uint16_t color = bucket.valid ? cozyLimitColor(bucket.used, baseColor) : WeatherTheme::MUTED;
  const float displayedUsed = startup || angle == target ? bucket.used : 100.0f - angle / 3.6f;
  const uint16_t track = bucket.valid ? LimitRingColor::consumed(displayedUsed) : LimitRingColor::GREY;
  if (reset) {
    if (angle < 360) display.drawArc(120, 120, radius, inner, angle, 360, track, TFT_BLACK, true);
    if (angle > 0) display.drawArc(120, 120, radius, inner, 0, angle, color, TFT_BLACK, true);
  } else {
    if (track != previous.track) {
      if (angle < 360) display.drawArc(120, 120, radius, inner, angle, 360, track, TFT_BLACK, true);
    } else if (angle < previous.angle) {
      display.drawArc(120, 120, radius, inner, angle, previous.angle, track, TFT_BLACK, true);
    }
    if (color != previous.color && angle > 0)
      display.drawArc(120, 120, radius, inner, 0, angle, color, TFT_BLACK, true);
    else if (angle > previous.angle)
      display.drawArc(120, 120, radius, inner, previous.angle, angle, color, TFT_BLACK, true);
    else if (angle > 0 && (angle < previous.angle || track != previous.track))
      display.drawArc(120, 120, radius, inner, max(0, angle - 1), angle, color, TFT_BLACK, true);
  }
  previous.angle = angle;
  previous.color = color;
  previous.track = track;
  previous.valid = bucket.valid;
}

void drawClaudeRings(bool fullRedraw = false) {
  static uint32_t lastFrame = 0;
  const uint32_t now = millis();
  if (!fullRedraw && now - lastFrame < 40) return;
  lastFrame = now;
  drawLimitRing(119, 15, sessionLimit, sessionRing, fullRedraw, WeatherTheme::HONEY);
  drawLimitRing(98, 7, weeklyLimit, weeklyRing, fullRedraw, WeatherTheme::SAGE);
}

void drawDisplayName(int y = 58) {
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  String name = config.displayName;
  if (display.textWidth(name) > 140) display.setTextFont(1);
  while (display.textWidth(name) > 140 && name.length()) name.remove(name.length() - 1);
  display.drawString(name, 120, y);
}

struct TextDrawState {
  String text;
  uint16_t color = TFT_WHITE;
};

using DisplayTextCache = ActiveTextCache<TextDrawState, 10>;
DisplayTextCache activeTextCache;

void drawChangedText(const char *text, uint16_t color, const DisplayLayout::TextArea &area,
                     TextDrawState &previous, bool fullRedraw) {
  if (!fullRedraw && previous.text == text && color == previous.color) return;
  const int y = area.y, width = area.width, height = area.height;
  if (!display.checkViewport(120 - width / 2, y - height / 2, width, height)) return;
  const uint8_t font = area.font;
  // Vier Pixel hoher 16-Bit-Streifen statt kompletter Textzeile.
  fittedText.reserve(64);
  fittedText = text;
  TFT_eSprite &row = textSprite;
  if (textBuffer.ensure()) {
    row.setTextDatum(MC_DATUM);
    row.setTextColor(color, TFT_BLACK);
    row.setTextFont(font);
    if (row.textWidth(text) > width) row.setTextFont(1);
    while (row.textWidth(fittedText) > width && fittedText.length()) fittedText.remove(fittedText.length() - 1);
    TextStrips::draw(row, display, fittedText, 120 - width / 2, y - height / 2, width, height);
  } else {
    // Bei knappem Heap weiterhin eine lesbare Anzeige ohne Vollbildlöschen.
    display.setTextDatum(MC_DATUM);
    display.setTextColor(color, TFT_BLACK);
    display.setTextFont(font);
    if (display.textWidth(text) > width) display.setTextFont(1);
    while (display.textWidth(fittedText) > width && fittedText.length()) fittedText.remove(fittedText.length() - 1);
    display.setTextPadding(width);
    display.drawString(fittedText, 120, y);
    display.setTextPadding(0);
  }
  if (strlen(text) > 11) previous.text.reserve(40);
  previous.text = text;
  previous.color = color;
}

void drawChangedText(const String &text, uint16_t color, const DisplayLayout::TextArea &area,
                     TextDrawState &previous, bool fullRedraw) {
  drawChangedText(text.c_str(), color, area, previous, fullRedraw);
}

void drawChangedText(const __FlashStringHelper *text, uint16_t color, const DisplayLayout::TextArea &area,
                     TextDrawState &previous, bool fullRedraw) {
  if (!fullRedraw && previous.text == text && previous.color == color) return;
  const String value(text);
  drawChangedText(value.c_str(), color, area, previous, fullRedraw);
}

void drawWeatherDisplay() {
  const bool full = activeTextCache.select(DisplayTextCache::Screen::WEATHER) || displayNeedsFullRedraw;
  TextDrawState &title = activeTextCache[0];
  TextDrawState &location = activeTextCache[1];
  TextDrawState &temp = activeTextCache[2];
  TextDrawState &condition = activeTextCache[3];
  TextDrawState &humidity = activeTextCache[4];
  TextDrawState &wind = activeTextCache[5];
  TextDrawState &status = activeTextCache[6];
  TextDrawState &source = activeTextCache[7];
  if (full) {
    display.fillScreen(TFT_BLACK);
    display.drawCircle(120, 120, 117, WeatherTheme::FRAME);
    display.drawCircle(120, 120, 115, WeatherTheme::FRAME);
    // Small warm accents beside the icon, clear of all text surfaces.
    for (int x : {62, 178}) {
      display.fillCircle(x, 80, 2, WeatherTheme::HONEY);
      display.drawFastHLine(x - 3, 70, 7, WeatherTheme::FRAME);
      display.drawFastVLine(x, 67, 7, WeatherTheme::FRAME);
    }
    display.drawFastHLine(93, 159, 54, WeatherTheme::FRAME);
  }
  if (display.checkViewport(88, 58, 64, 44))
    weatherAnimation.draw(weather.valid ? weather.code : -1, millis(), full);
  drawChangedText(F("Draussen"), WeatherTheme::HONEY, DisplayLayout::WEATHER_TITLE, title, full);
  drawChangedText(config.weatherName, WeatherTheme::CREAM, DisplayLayout::WEATHER_LOCATION, location, full);
  char line[64];
  if (weather.valid) snprintf_P(line, sizeof(line), PSTR("%.1f C"), static_cast<double>(weather.temperature));
  else snprintf_P(line, sizeof(line), PSTR("--.- C"));
  drawChangedText(line, WeatherTheme::CREAM, DisplayLayout::WEATHER_TEMPERATURE, temp, full);
  if (weather.valid) drawChangedText(weatherDescription(weather.code), WeatherTheme::MUTED,
                                    DisplayLayout::WEATHER_CONDITION, condition, full);
  else drawChangedText(F("Keine Wetterdaten"), WeatherTheme::MUTED, DisplayLayout::WEATHER_CONDITION, condition, full);
  if (weather.valid) snprintf_P(line, sizeof(line), PSTR("Feuchte %.0f %%"), static_cast<double>(weather.humidity));
  else line[0] = '\0';
  drawChangedText(line, WeatherTheme::SKY, DisplayLayout::WEATHER_HUMIDITY, humidity, full);
  if (weather.valid) snprintf_P(line, sizeof(line), PSTR("Wind %.0f km/h"), static_cast<double>(weather.wind));
  else line[0] = '\0';
  drawChangedText(line, WeatherTheme::SAGE, DisplayLayout::WEATHER_WIND, wind, full);
  if (!config.weatherName.length())
    drawChangedText(F("Standort in Einstellungen"), WeatherTheme::MUTED, DisplayLayout::WEATHER_STATUS, status, full);
  else drawChangedText(weather.state, WeatherTheme::MUTED, DisplayLayout::WEATHER_STATUS, status, full);
  drawChangedText(F("Open-Meteo"), WeatherTheme::MUTED, DisplayLayout::WEATHER_SOURCE, source, full);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void drawStatusDisplay() {
  const bool full = activeTextCache.select(DisplayTextCache::Screen::STATUS) || displayNeedsFullRedraw;
  TextDrawState &title = activeTextCache[0];
  TextDrawState &label = activeTextCache[1];
  TextDrawState &ssid = activeTextCache[2];
  TextDrawState &signal = activeTextCache[3];
  TextDrawState &ip = activeTextCache[4];
  TextDrawState &channel = activeTextCache[5];
  TextDrawState &heap = activeTextCache[6];
  TextDrawState &uptime = activeTextCache[7];
  TextDrawState &version = activeTextCache[8];
  TextDrawState &hint = activeTextCache[9];
  if (full) {
    display.fillScreen(TFT_BLACK);
    display.drawCircle(120, 120, 117, WeatherTheme::FRAME);
    display.drawCircle(120, 120, 115, WeatherTheme::FRAME);
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  drawChangedText(F("Status"), WeatherTheme::HONEY, DisplayLayout::STATUS_TITLE, title, full);
  drawChangedText(connected ? F("DEIN WLAN") : F("WLAN GETRENNT"), WeatherTheme::MUTED,
                  DisplayLayout::STATUS_LABEL, label, full);
  const String ssidText = wifiPortal ? String(SETUP_AP) : WiFi.SSID();
  drawChangedText(ssidText.length() ? ssidText : String(F("Kein WLAN gespeichert")), WeatherTheme::CREAM,
                  DisplayLayout::STATUS_SSID, ssid, full);
  char line[80];
  const int rssi = connected ? WiFi.RSSI() : -100;
  if (connected) snprintf_P(line, sizeof(line), PSTR("Signal %d dBm / %d %%"), rssi, constrain(2 * (rssi + 100), 0, 100));
  else snprintf_P(line, sizeof(line), PSTR("Signal --"));
  drawChangedText(line, WeatherTheme::SAGE, DisplayLayout::STATUS_SIGNAL, signal, full);
  if (connected || wifiPortal) {
    const IPAddress address = connected ? WiFi.localIP() : WiFi.softAPIP();
    snprintf_P(line, sizeof(line), PSTR("IP %u.%u.%u.%u"),
        static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
        static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3]));
  } else snprintf_P(line, sizeof(line), PSTR("IP --"));
  drawChangedText(line, WeatherTheme::CREAM, DisplayLayout::STATUS_IP, ip, full);
  char channelText[8];
  if (connected) snprintf(channelText, sizeof(channelText), "%d", WiFi.channel());
  else snprintf_P(channelText, sizeof(channelText), PSTR("--"));
  snprintf_P(line, sizeof(line), PSTR("Kanal %s  |  Uhr %s"), channelText,
      time(nullptr) >= 1577836800 ? "synchron" : "wartet");
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::STATUS_CHANNEL, channel, full);
  snprintf_P(line, sizeof(line), PSTR("RAM %luK  |  Block %luK"),
      static_cast<unsigned long>(ESP.getFreeHeap() / 1024), static_cast<unsigned long>(ESP.getMaxFreeBlockSize() / 1024));
  drawChangedText(line, WeatherTheme::SKY, DisplayLayout::STATUS_HEAP, heap, full);
  const uint32_t minutes = millis() / 60000UL;
  snprintf_P(line, sizeof(line), PSTR("Laufzeit %luT %luh %lumin"),
      static_cast<unsigned long>(minutes / 1440), static_cast<unsigned long>((minutes / 60) % 24),
      static_cast<unsigned long>(minutes % 60));
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::STATUS_UPTIME, uptime, full);
  snprintf_P(line, sizeof(line), PSTR("FW %s"), FIRMWARE_VERSION);
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::STATUS_VERSION, version, full);
  drawChangedText(F("Tap zum Zurueck"), WeatherTheme::HONEY, DisplayLayout::STATUS_HINT, hint, full);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void drawWifiConnecting(bool force = false) {
  static uint32_t lastFrame = 0, lastSsidCheck = 0;
  const uint32_t now = millis();
  const bool full = activeTextCache.select(DisplayTextCache::Screen::WIFI) || force || displayNeedsFullRedraw;
  TextDrawState &name = activeTextCache[0];
  TextDrawState &title = activeTextCache[1];
  TextDrawState &state = activeTextCache[2];
  TextDrawState &ssid = activeTextCache[3];
  TextDrawState &hint = activeTextCache[4];
  TextDrawState &ip = activeTextCache[5];
  if (!full && now - lastFrame < WifiPulse::FRAME_MS) return;
  lastFrame = now;
  if (full) display.fillScreen(TFT_BLACK);
  // Only the outer halo moves; all text stays inside radius 88.
  for (int radius = 118; radius >= 94; radius -= 2)
    display.drawArc(120, 120, radius, radius - 2, 0, 360,
                    WifiPulse::color(now, radius), TFT_BLACK, false);
  const bool portal = wifiPortal && wifiPortal->getConfigPortalActive();
  drawChangedText(config.displayName, WeatherTheme::MUTED, DisplayLayout::WIFI_NAME, name, full);
  drawChangedText(F("WLAN"), WeatherTheme::CREAM, DisplayLayout::WIFI_TITLE, title, full);
  drawChangedText(portal ? F("In Ruhe einrichten") : F("Verbinde ..."), WeatherTheme::HONEY,
                  DisplayLayout::WIFI_STATE, state, full);
  if (full || now - lastSsidCheck >= 1000) {
    lastSsidCheck = now;
    drawChangedText(portal ? String(SETUP_AP) : WiFi.SSID(), WeatherTheme::CREAM,
                    DisplayLayout::WIFI_SSID, ssid, full);
  }
  drawChangedText(portal ? F("Setup-WLAN ist bereit") : F("Einen Moment ..."), WeatherTheme::MUTED,
                  DisplayLayout::WIFI_HINT, hint, full);
  drawChangedText(portal ? F("AP 192.168.4.1") : F(""), WeatherTheme::SAGE,
                  DisplayLayout::WIFI_IP, ip, full);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void drawDisplay() {
  if (WiFi.status() != WL_CONNECTED && !pages.status()) { drawWifiConnecting(); return; }
  if (pages.status()) { drawStatusDisplay(); return; }
  if (pages.weather()) { drawWeatherDisplay(); return; }
  const bool fullRedraw = activeTextCache.select(DisplayTextCache::Screen::CLAUDE) || displayNeedsFullRedraw;
  TextDrawState &name = activeTextCache[0];
  TextDrawState &temperature = activeTextCache[1];
  TextDrawState &clock = activeTextCache[2];
  TextDrawState &session = activeTextCache[3];
  TextDrawState &sessionCountdown = activeTextCache[4];
  TextDrawState &weekly = activeTextCache[5];
  TextDrawState &weeklyCountdown = activeTextCache[6];
  TextDrawState &network = activeTextCache[7];
  TextDrawState &trueConf = activeTextCache[8];
  if (fullRedraw) display.fillScreen(TFT_BLACK);

  drawClaudeRings(fullRedraw);
  drawChangedText(config.displayName, WeatherTheme::CREAM, DisplayLayout::NAME, name, fullRedraw);
  char line[64];
  if (isnan(temperatureC)) snprintf_P(line, sizeof(line), PSTR("--.- C"));
  else snprintf_P(line, sizeof(line), PSTR("%.1f C"), static_cast<double>(temperatureC));
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::TEMPERATURE, temperature, fullRedraw);
  char sessionReset[24], weeklyReset[24];
  const int64_t now = static_cast<int64_t>(time(nullptr));
  char clockText[6] = "--:--";
  if (now >= 1577836800) {
    const time_t timestamp = static_cast<time_t>(now);
    struct tm localTime;
    if (localtime_r(&timestamp, &localTime)) strftime(clockText, sizeof(clockText), "%H:%M", &localTime);
  }
  drawChangedText(clockText, WeatherTheme::CREAM, DisplayLayout::CLOCK, clock, fullRedraw);
  formatCozyResetRemaining(sessionLimit.valid ? sessionLimit.resetEpoch : -1, now, sessionReset, sizeof(sessionReset));
  formatCozyResetRemaining(weeklyLimit.valid ? weeklyLimit.resetEpoch : -1, now, weeklyReset, sizeof(weeklyReset));
  if (sessionLimit.valid) snprintf_P(line, sizeof(line), PSTR("Session %.0f %%"),
      static_cast<double>(constrain(100.0f - sessionLimit.used, 0.0f, 100.0f)));
  else snprintf_P(line, sizeof(line), PSTR("Session -- %%"));
  drawChangedText(line, cozyLimitColor(sessionLimit.used, WeatherTheme::HONEY), DisplayLayout::SESSION, session, fullRedraw);
  snprintf_P(line, sizeof(line), PSTR("Reset in %s"), sessionReset);
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::SESSION_RESET, sessionCountdown, fullRedraw);
  if (weeklyLimit.valid) snprintf_P(line, sizeof(line), PSTR("Woche %.0f %%"),
      static_cast<double>(constrain(100.0f - weeklyLimit.used, 0.0f, 100.0f)));
  else snprintf_P(line, sizeof(line), PSTR("Woche -- %%"));
  drawChangedText(line, cozyLimitColor(weeklyLimit.used, WeatherTheme::SAGE), DisplayLayout::WEEKLY, weekly, fullRedraw);
  snprintf_P(line, sizeof(line), PSTR("Reset in %s"), weeklyReset);
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::WEEKLY_RESET, weeklyCountdown, fullRedraw);
  const bool connected = claudeState == F("Verbunden");
  if (config.trueConf.enabled) {
    if (trueConfResult.valid) snprintf_P(line, sizeof(line), PSTR("TC %lu"), static_cast<unsigned long>(trueConfResult.unread));
    else snprintf_P(line, sizeof(line), PSTR("TC --"));
  } else if (connected) snprintf_P(line, sizeof(line), PSTR("Verbunden"));
  else snprintf_P(line, sizeof(line), PSTR("Claude pruefen"));
  drawChangedText(line, connected ? WeatherTheme::SAGE : WeatherTheme::MUTED, DisplayLayout::TRUECONF, trueConf, fullRedraw);
  line[0] = '\0';
  if (config.showIp) {
    const IPAddress address = WiFi.localIP();
    snprintf_P(line, sizeof(line), PSTR("%u.%u.%u.%u"),
        static_cast<unsigned>(address[0]), static_cast<unsigned>(address[1]),
        static_cast<unsigned>(address[2]), static_cast<unsigned>(address[3]));
  }
  drawChangedText(line, WeatherTheme::MUTED, DisplayLayout::NETWORK, network, fullRedraw);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void handleTouch() {
  static bool wasArmed = false, wasPressed = false;
  const bool rawPressed = digitalRead(Pins::TOUCH) == HIGH;
  const TouchInput::Event event = touchInput.update(rawPressed, millis());
  if (touchInput.isArmed() && !wasArmed) Serial.println(F("Touch: LOW erkannt, Sensor bereit"));
  if (touchInput.isPressed() != wasPressed)
    Serial.println(touchInput.isPressed() ? F("Touch: gedrueckt (HIGH)") : F("Touch: losgelassen (LOW)"));
  wasArmed = touchInput.isArmed();
  wasPressed = touchInput.isPressed();
  if (pages.update(event, rawPressed || touchInput.isPressed(), millis())) {
    if (pages.requestsPaused()) {
      sessionRing.startup.pause();
      weeklyRing.startup.pause();
    }
    if (!pages.weather()) weatherAnimation.release();
    displayNeedsFullRedraw = true;
    Serial.println(pages.status() ? F("Touch: Statusseite") :
                   (pages.weather() ? F("Touch: Wetterseite") : F("Touch: Claude-Seite")));
    drawDisplay();
  }
  if (event == TouchInput::Event::LONG_PRESS) touchResetRequested = true;
}

void handleLongTouch() {
  if (!touchResetRequested) return;
  touchResetRequested = false;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  display.drawString("WLAN wird geloescht", 120, 105);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("Neustart ...", 120, 135);
  drawDisplayName();
  WiFiManager manager;
  manager.resetSettings();
  delay(1000);
  ESP.restart();
}

void connectWifi(bool trySavedWifi = true) {
  WiFi.persistent(true);
  WiFi.hostname(HOSTNAME);
  if (trySavedWifi) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(); // Start saved credentials asynchronously; do not wait here.
    lastWifiRetryMs = millis();
    if (WiFi.SSID().length()) {
      wifiStartup.begin(millis());
      return;
    }
  }
  wifiStartup.finish();
  wifiPortal.reset(new WiFiManager());
  WiFiManager &manager = *wifiPortal;
  manager.setHostname(HOSTNAME);
  manager.setConfigPortalTimeout(0);
  manager.setConfigPortalBlocking(false);
  manager.setWiFiAutoReconnect(true);
  manager.setConnectTimeout(20);
  manager.setBreakAfterConfig(false);
  manager.setAPCallback([](WiFiManager *) {
    displayNeedsFullRedraw = true;
  });
  // The portal manages WiFi only and runs alongside the animation loop.
  manager.startConfigPortal(SETUP_AP, SETUP_AP_PASSWORD);
  displayNeedsFullRedraw = true;
}

void handleWifiRecovery() {
  if (wifiPortal) processWifiPortalIfRunning(*wifiPortal);
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  static bool wasConnected = false;
  if (connected != wasConnected) {
    displayNeedsFullRedraw = true;
    if (!connected) {
      weatherAnimation.release();
      sessionRing.startup.pause();
      weeklyRing.startup.pause();
    }
    wasConnected = connected;
  }
  const bool fallbackDue = wifiRecovery.needsPortal(connected, now);
  if (connected) {
    wifiStartup.finish();
    if (wifiPortal) {
      stopWifiPortalIfRunning(*wifiPortal);
      wifiPortal.reset();
      WiFi.mode(WIFI_STA);
      displayNeedsFullRedraw = true;
      forceClaudeRefresh = true;
    }
    if (!normalWebRunning) {
      configTime(LOCAL_TIMEZONE, config.ntpServer.c_str());
      forceClaudeRefresh = true;
      setupWebServer();
    }
    return;
  }
  if (!wifiPortal && (wifiStartup.needsPortal(now) || (!wifiStartup.active() && fallbackDue))) {
    Serial.println(F("WLAN nicht erreichbar; Setup-AP startet"));
    web.stop();
    MDNS.end();
    normalWebRunning = false;
    connectWifi(false);
  }
  if (wifiPortal && !wifiPortal->getConfigPortalActive()) {
    wifiPortal->startConfigPortal(SETUP_AP, SETUP_AP_PASSWORD);
    displayNeedsFullRedraw = true;
  }
  if (!wifiStartup.active() && now - lastWifiRetryMs >= Timing::WIFI_RETRY_MS && WiFi.SSID().length()) {
    lastWifiRetryMs = now;
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("Claude Display Firmware %s\n", FIRMWARE_VERSION);
  Serial.println(F("GC9A01: CLK=D5 MOSI=D7 DC=D4 RST=D0 CS=GND; SPI=10 MHz"));
  LittleFS.begin();
  loadConfig();

  display.init();
  display.setRotation(0);
  // ESP8266 SPI.begin() beansprucht auch ohne Display-MISO GPIO12/D6.
  // Erst NACH display.init() den Pinmux wieder als Touch-GPIO konfigurieren.
  // GC9A01 ist hier nur schreibend angeschlossen (kein MISO).
  pinMode(Pins::TOUCH, INPUT_PULLUP);
  Serial.println(F("Touch: D6/GPIO12 nach Displaystart als Eingang eingerichtet"));
  drawWifiConnecting(true);
  Serial.println(F("Display initialisiert; WLAN-Verbindung startet"));

  connectWifi();
  configTime(LOCAL_TIMEZONE, config.ntpServer.c_str());
  detectSensor();
  readSensor();
  handleWifiRecovery();
  forceClaudeRefresh = true;
  forceTrueConfRefresh = false;
  if (config.trueConf.enabled) trueConfResult.state = F("Erster Abruf 60 Sekunden nach Webserver-Start");
  drawDisplay();
}

void loop() {
  const uint32_t now = millis();
  handleWifiRecovery();
  if (normalWebRunning) {
    web.handleClient();
    if (WiFi.status() == WL_CONNECTED) MDNS.update();
  }
  handleTouch();
  handleLongTouch();

  if (now - lastSensorMs >= Timing::SENSOR_MS) {
    lastSensorMs = now;
    readSensor();
  }

  const uint32_t configuredIntervalMs = config.intervalSec * static_cast<uint32_t>(1000);
  const uint32_t intervalMs = max(Timing::CLAUDE_MIN_MS, configuredIntervalMs);
  // Fällige Abrufe und manuelle Anforderungen auf Wetter-/Statusseite behalten.
  // Beim Zurückwechseln greifen die bisherigen Intervalle automatisch wieder.
  if (WiFi.status() == WL_CONNECTED && !pages.requestsPaused() && !pages.tapPending() && !touchInput.isPressed() && (forceClaudeRefresh || now - lastClaudeMs >= intervalMs)) {
    forceClaudeRefresh = false;
    lastClaudeMs = now;
    fetchClaudeUsage();
  }

  if (WiFi.status() == WL_CONNECTED && !claudeStartupActive() && !pages.requestsPaused() && !pages.tapPending() && !touchInput.isPressed() && config.trueConf.enabled && (forceTrueConfRefresh ||
      (millis() - webServerReadyMs >= Timing::TRUECONF_STARTUP_GRACE_MS &&
       millis() - lastTrueConfMs >= config.trueConf.intervalSec * 1000UL))) refreshTrueConf();

  if (!claudeStartupActive() && !pages.status() && !pages.tapPending() && !touchInput.isPressed() &&
      WiFi.status() == WL_CONNECTED && config.weatherName.length() &&
      (forceWeatherRefresh || millis() - lastWeatherMs >= 900000UL)) {
    forceWeatherRefresh = false;
    lastWeatherMs = millis();
    releaseDisplayScratch();
    WeatherResult next = fetchWeather(config.weatherLatitude, config.weatherLongitude);
    if (next.valid) weather = next;
    else weather.state = weather.valid ? "Alte Daten / Abruffehler" : next.state;
  }

  // Auch Countdown und Netzstatus prüfen; unveränderte Textzeilen erzeugen keine SPI-Ausgabe.
  if (millis() - lastDisplayMs >= Timing::DISPLAY_MS) {
    lastDisplayMs = millis();
    drawDisplay();
  }
  if (!updateAuthenticated) {
    if (WiFi.status() != WL_CONNECTED) {
      if (!pages.status()) drawWifiConnecting();
    }
    else if (pages.weather()) weatherAnimation.draw(weather.valid ? weather.code : -1, millis());
    else if (!pages.status()) drawClaudeRings();
  }
  yield();
}
