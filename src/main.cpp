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
#include "wifi_recovery.h"
#include "weather.h"
#include "weather_animation.h"
#include "touch_input.h"
#include "tls_memory.h"
#include <memory>
#include <time.h>

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
constexpr char FIRMWARE_VERSION[] = "2026-10-19.6";
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
LimitBucket opusLimit;
LimitBucket sonnetLimit;

TFT_eSPI display;
WeatherAnimation weatherAnimation(display);
ESP8266WebServer web(80);
std::unique_ptr<WiFiManager> wifiPortal;
WifiRecovery wifiRecovery;
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
bool weatherPage = false;
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

uint16_t limitColor(float used) {
  const float fraction = constrain(used, 0.0f, 100.0f) / 100.0f;
  return display.color565(static_cast<uint8_t>(255.0f * fraction + 0.5f),
                          static_cast<uint8_t>(255.0f * (1.0f - fraction) + 0.5f), 0);
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
  web.sendContent(F("<!doctype html><html lang='de'><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<style>body{font-family:system-ui;max-width:760px;margin:auto;padding:20px;background:#111827;color:#f9fafb}"
                    "a{color:#60a5fa}nav{display:flex;gap:18px;margin-bottom:20px}.card{background:#1f2937;padding:18px;border-radius:14px;margin:12px 0}"
                    "label{display:block;margin-top:13px;color:#d1d5db}input{box-sizing:border-box;width:100%;padding:10px;margin-top:5px;background:#111827;color:white;border:1px solid #4b5563;border-radius:7px}"
                    "button{padding:11px 16px;margin-top:16px;background:#2563eb;color:white;border:0;border-radius:7px}.ok{color:#4ade80}.warn{color:#facc15}</style></head><body>"));
  web.sendContent(F("<nav><a href='/'>Status</a><a href='/settings'>Einstellungen</a><a href='/update'>Firmware-Update</a></nav><h1>"));
  web.sendContent(title);
  web.sendContent(F("</h1>"));
  web.sendContent(F("<p>Firmware: "));
  web.sendContent(FIRMWARE_VERSION);
  web.sendContent(F("</p>"));
  web.sendContent(F("<p>Aktuelle Uhrzeit (Europe/Berlin): <time id='deviceClock' data-unix='"));
  const time_t now = time(nullptr);
  web.sendContent(String(now >= 1577836800 ? static_cast<uint32_t>(now) : 0));
  web.sendContent(F("'>Uhrzeit noch nicht synchronisiert</time></p>"
                    "<script>(()=>{const el=document.getElementById('deviceClock');"
                    "let epoch=Number(el.dataset.unix),base=performance.now();"
                    "const fmt=new Intl.DateTimeFormat('de-DE',{timeZone:'Europe/Berlin',"
                    "day:'2-digit',month:'2-digit',year:'numeric',hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});"
                    "function tick(){if(!epoch){el.textContent='Uhrzeit noch nicht synchronisiert';return;}"
                    "const d=new Date(epoch*1000+performance.now()-base);el.textContent=fmt.format(d);el.dateTime=d.toISOString();}"
                    "async function sync(){try{const r=await fetch('/api/time',{cache:'no-store'});if(!r.ok)return;"
                    "const t=await r.json();epoch=t.clock_synced?t.unix_time:0;base=performance.now();tick();}catch(e){}}"
                    "tick();setInterval(tick,1000);setInterval(sync,10000);})();</script>"));
}

void endPage() {
  web.sendContent(F("</body></html>"));
  web.sendContent("");
}

bool requireSettingsAuth() {
  if (web.authenticate("admin", config.webPassword.c_str())) return true;
  web.requestAuthentication(BASIC_AUTH, "Claude Display");
  return false;
}

void handleStatus() {
  beginPage(F("Claude Display"));
  web.sendContent(F("<div class='card'><h2>Klima</h2><p>Temperatur: "));
  web.sendContent(isnan(temperatureC) ? "--" : String(temperatureC, 1));
  web.sendContent(F(" &deg;C<br>Luftfeuchtigkeit: "));
  web.sendContent(isnan(humidityPct) ? "--" : String(humidityPct, 1));
  web.sendContent(F(" %<br>Luftdruck: "));
  web.sendContent(isnan(pressureHpa) ? "--" : String(pressureHpa, 1));
  web.sendContent(F(" hPa</p></div><div class='card'><h2>Claude Limits</h2><p>Sitzung frei: <b>"));
  web.sendContent(percentText(sessionLimit));
  web.sendContent(F(" %</b><br>Woche frei: <b>"));
  web.sendContent(percentText(weeklyLimit));
  web.sendContent(F(" %</b>"));
  if (sessionLimit.resetsAt.length()) {
    web.sendContent(F("<br>Sitzungs-Reset: "));
    sendHtmlEscaped(sessionLimit.resetsAt);
  }
  if (weeklyLimit.resetsAt.length()) {
    web.sendContent(F("<br>Wochen-Reset: "));
    sendHtmlEscaped(weeklyLimit.resetsAt);
  }
  web.sendContent(F("</p><p>Status: "));
  sendHtmlEscaped(claudeState);
  web.sendContent(F("<br>Letzte Aktualisierung: "));
  sendHtmlEscaped(lastClaudeUpdate);
  web.sendContent(F("</p><form method='post' action='/refresh'><button>Jetzt aktualisieren</button></form></div>"));
  web.sendContent(F("<div class='card'><h2>Gerät</h2><p>IP: "));
  web.sendContent(WiFi.localIP().toString());
  web.sendContent(F("<br>WLAN-Signal: "));
  web.sendContent(String(WiFi.RSSI()));
  web.sendContent(F(" dBm<br>Freier Heap: "));
  web.sendContent(String(ESP.getFreeHeap()));
  web.sendContent(F(" Bytes</p></div><div class='card'><h2>TrueConf</h2><p>Ungelesene Nachrichten: "));
  web.sendContent(trueConfResult.valid ? String(trueConfResult.unread) : String(F("--")));
  web.sendContent(F("<br>Status: "));
  sendHtmlEscaped(trueConfResult.state);
  web.sendContent(F("</p></div>"));
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
                    "<button type='submit' onclick=\"this.textContent='Teste ...';this.form.submit();this.disabled=true\">Gespeicherten Session-Key testen</button></form></div>"
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
  if (weatherPage) { forceTrueConfRefresh = true; return; }
  trueConfResult = fetchTrueConfUnread(config.trueConf);
  lastTrueConfMs = millis();
  forceTrueConfRefresh = false;
  displayDirty = true;
}

void handleTestTrueConf() {
  if (!requireSettingsAuth()) return;
  if (weatherPage) {
    forceTrueConfRefresh = true;
    settingsResult(F("TrueConf-Abfragen sind auf der Wetterseite pausiert. Der Abruf folgt nach dem Seitenwechsel."));
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
  if (weatherPage) {
    forceClaudeRefresh = true;
    settingsResult(F("Claude-Abfragen sind auf der Wetterseite pausiert. Der Abruf folgt nach dem Seitenwechsel."));
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
                    "const x=new XMLHttpRequest();x.open('POST','/update');"
                    "x.upload.onprogress=e=>{if(e.lengthComputable){const n=Math.round(e.loaded/e.total*100);p.value=n;t.textContent='Upload: '+n+' %';}};"
                    "x.onload=()=>{document.open();document.write(x.responseText);document.close();};"
                    "x.onerror=()=>{t.textContent='Upload fehlgeschlagen.';};x.send(new FormData(f));});</script>"));
  endPage();
}

void handleUpdateUpload() {
  HTTPUpload &upload = web.upload();
  if (upload.status == UPLOAD_FILE_START) {
    updateAuthenticated = web.authenticate("admin", config.webPassword.c_str());
    updateSuccessful = false;
    updateError = "";
    if (!updateAuthenticated) return;
    if (!upload.filename.endsWith(".bin")) {
      updateError = "Nur eine firmware.bin ist erlaubt.";
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
    updateError = "Upload wurde abgebrochen.";
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
    doc["claude_state"] = claudeState;
    doc["clock_synced"] = time(nullptr) >= 1577836800;
    doc["trueconf"]["enabled"] = config.trueConf.enabled;
    doc["trueconf"]["state"] = trueConfResult.state;
    if (trueConfResult.valid) doc["trueconf"]["unread"] = trueConfResult.unread;
    doc["weather"]["location"] = config.weatherName;
    doc["weather"]["state"] = weather.state;
    doc["weather"]["valid"] = weather.valid;
    if (weather.valid) {
      doc["weather"]["temperature_c"] = weather.temperature;
      doc["weather"]["humidity_pct"] = weather.humidity;
      doc["weather"]["wind_kmh"] = weather.wind;
    }
    doc["touch"]["raw_high"] = digitalRead(Pins::TOUCH) == HIGH;
    doc["touch"]["ready"] = touchInput.isArmed();
    doc["touch"]["pressed"] = touchInput.isPressed();
    doc["display_page"] = weatherPage ? "weather" : "claude";
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
  String cookie = "sessionKey=" + config.sessionKey;
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
  weatherAnimation.release();
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxFreeBlockSize();
  Serial.printf("Claude TLS: Heap=%lu, groesster Block=%lu\n",
                static_cast<unsigned long>(freeHeap), static_cast<unsigned long>(largestBlock));
  if (hasClaudeTlsMemory(freeHeap, largestBlock)) return true;
  claudeState = F("Claude-Abruf ausgesetzt: zu wenig TLS-Speicher. Heap ");
  claudeState += String(freeHeap) + F(" Bytes, groesster Block ") + String(largestBlock) + F(" Bytes");
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
  if (weatherPage) { forceClaudeRefresh = true; return; }
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
  for (const char *key : {"five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"}) {
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
  readBucket(root, "seven_day_opus", opusLimit);
  readBucket(root, "seven_day_sonnet", sonnetLimit);
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
  int angle = 0;
  uint16_t color = TFT_DARKGREY;
  bool valid = false;
};

uint16_t consumedRingColor(float used) {
  const float fraction = constrain(used, 0.0f, 100.0f) / 100.0f;
  // Gleichmäßiger Übergang: Grau (#808080) -> kräftiges Rot (#FF0000).
  const uint8_t red = static_cast<uint8_t>(128.0f + 127.0f * fraction + 0.5f);
  const uint8_t other = static_cast<uint8_t>(128.0f * (1.0f - fraction) + 0.5f);
  return display.color565(red, other, other);
}

void drawLimitRing(int radius, int thickness, const LimitBucket &bucket,
                   RingDrawState &previous, bool fullRedraw) {
  const int inner = radius - thickness;
  const int angle = bucket.valid
      ? static_cast<int>(360.0f * constrain(100.0f - bucket.used, 0.0f, 100.0f) / 100.0f) : 0;
  const uint16_t color = bucket.valid ? consumedRingColor(bucket.used) : TFT_DARKGREY;
  const bool reset = fullRedraw || bucket.valid != previous.valid;
  if (reset) {
    if (angle < 360) display.drawArc(120, 120, radius, inner, angle, 360, color, TFT_BLACK, true);
    if (angle > 0) display.drawArc(120, 120, radius, inner, 0, angle, TFT_GREEN, TFT_BLACK, true);
  } else {
    if (color != previous.color) {
      // Der ganze verbrauchte Anteil ändert seine Farbe mit dem Verbrauch.
      if (angle < 360) display.drawArc(120, 120, radius, inner, angle, 360, color, TFT_BLACK, true);
    } else if (angle < previous.angle) {
      display.drawArc(120, 120, radius, inner, angle, previous.angle, color, TFT_BLACK, true);
    }
    if (angle > previous.angle) {
      display.drawArc(120, 120, radius, inner, previous.angle, angle, TFT_GREEN, TFT_BLACK, true);
    } else if (angle > 0 && (angle < previous.angle || color != previous.color)) {
      display.drawArc(120, 120, radius, inner, max(0, angle - 1), angle, TFT_GREEN, TFT_BLACK, true);
    }
  }
  previous.angle = angle;
  previous.color = color;
  previous.valid = bucket.valid;
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

void drawChangedText(const String &text, uint16_t color, const DisplayLayout::TextArea &area,
                     TextDrawState &previous, bool fullRedraw) {
  if (!fullRedraw && text == previous.text && color == previous.color) return;
  const int y = area.y, width = area.width, height = area.height;
  const uint8_t font = area.font;
  // Nur eine kleine Textzeile puffern; kein 240x240-Vollbild im ESP8266-RAM.
  TFT_eSprite row(&display);
  row.setColorDepth(16);
  if (row.createSprite(width, height)) {
    row.fillSprite(TFT_BLACK);
    row.setTextDatum(MC_DATUM);
    row.setTextColor(color, TFT_BLACK);
    row.setTextFont(font);
    if (row.textWidth(text) > width) row.setTextFont(1);
    String fitted = text;
    while (row.textWidth(fitted) > width && fitted.length()) fitted.remove(fitted.length() - 1);
    row.drawString(fitted, width / 2, height / 2);
    row.pushSprite(120 - width / 2, y - height / 2);
    row.deleteSprite();
  } else {
    // Bei knappem Heap weiterhin eine lesbare Anzeige ohne Vollbildlöschen.
    display.setTextDatum(MC_DATUM);
    display.setTextColor(color, TFT_BLACK);
    display.setTextFont(font);
    if (display.textWidth(text) > width) display.setTextFont(1);
    String fitted = text;
    while (display.textWidth(fitted) > width && fitted.length()) fitted.remove(fitted.length() - 1);
    display.setTextPadding(width);
    display.drawString(fitted, 120, y);
    display.setTextPadding(0);
  }
  previous.text = text;
  previous.color = color;
}

void drawWeatherDisplay() {
  static TextDrawState title, location, temp, condition, humidity, wind, status, source;
  const bool full = displayNeedsFullRedraw;
  if (full) display.fillScreen(TFT_BLACK);
  weatherAnimation.draw(weather.valid ? weather.code : -1, millis(), full);
  drawChangedText("Wetter", TFT_CYAN, DisplayLayout::WEATHER_TITLE, title, full);
  drawChangedText(config.weatherName, TFT_WHITE, DisplayLayout::WEATHER_LOCATION, location, full);
  drawChangedText(weather.valid ? String(weather.temperature, 1) + " C" : "--.- C",
                  TFT_WHITE, DisplayLayout::WEATHER_TEMPERATURE, temp, full);
  drawChangedText(weather.valid ? weatherDescription(weather.code) : "Keine Wetterdaten",
                  TFT_WHITE, DisplayLayout::WEATHER_CONDITION, condition, full);
  drawChangedText(weather.valid ? "Feuchte " + String(weather.humidity, 0) + " %" : "",
                  TFT_WHITE, DisplayLayout::WEATHER_HUMIDITY, humidity, full);
  drawChangedText(weather.valid ? "Wind " + String(weather.wind, 0) + " km/h" : "",
                  TFT_WHITE, DisplayLayout::WEATHER_WIND, wind, full);
  const String state = !config.weatherName.length() ? "Standort in Einstellungen" :
      (WiFi.status() != WL_CONNECTED ? "WLAN getrennt" : weather.state);
  drawChangedText(state, TFT_WHITE, DisplayLayout::WEATHER_STATUS, status, full);
  drawChangedText("Open-Meteo", TFT_DARKGREY, DisplayLayout::WEATHER_SOURCE, source, full);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void drawDisplay() {
  if (weatherPage) { drawWeatherDisplay(); return; }
  static RingDrawState sessionRing, weeklyRing;
  static TextDrawState name, temperature, clock, session, weekly, network, trueConf;
  const bool fullRedraw = displayNeedsFullRedraw;
  if (fullRedraw) display.fillScreen(TFT_BLACK);

  drawLimitRing(119, 15, sessionLimit, sessionRing, fullRedraw);
  drawLimitRing(98, 7, weeklyLimit, weeklyRing, fullRedraw);
  drawChangedText(config.displayName, TFT_WHITE, DisplayLayout::NAME, name, fullRedraw);
  const String temperatureText = isnan(temperatureC) ? "--.- C" : String(temperatureC, 1) + " C";
  drawChangedText(temperatureText, TFT_WHITE, DisplayLayout::TEMPERATURE, temperature, fullRedraw);
  char sessionReset[24], weeklyReset[24];
  const int64_t now = static_cast<int64_t>(time(nullptr));
  char clockText[9] = "--:--:--";
  if (now >= 1577836800) {
    const time_t timestamp = static_cast<time_t>(now);
    struct tm localTime;
    if (localtime_r(&timestamp, &localTime)) strftime(clockText, sizeof(clockText), "%H:%M:%S", &localTime);
  }
  drawChangedText(clockText, TFT_WHITE, DisplayLayout::CLOCK, clock, fullRedraw);
  formatResetRemaining(sessionLimit.valid ? sessionLimit.resetEpoch : -1, now, sessionReset, sizeof(sessionReset));
  formatResetRemaining(weeklyLimit.valid ? weeklyLimit.resetEpoch : -1, now, weeklyReset, sizeof(weeklyReset));
  drawChangedText("S " + percentText(sessionLimit) + "% " + sessionReset,
                  sessionLimit.valid ? limitColor(sessionLimit.used) : TFT_WHITE,
                  DisplayLayout::SESSION, session, fullRedraw);
  drawChangedText("W " + percentText(weeklyLimit) + "% " + weeklyReset,
                  weeklyLimit.valid ? limitColor(weeklyLimit.used) : TFT_WHITE,
                  DisplayLayout::WEEKLY, weekly, fullRedraw);
  const String trueConfText = !config.trueConf.enabled ? String() : "TC " +
      (trueConfResult.valid ? String(trueConfResult.unread) : String(F("--")));
  drawChangedText(trueConfText, TFT_WHITE, DisplayLayout::TRUECONF, trueConf, fullRedraw);
  // Schmale Zeile vollständig innerhalb des inneren Rings.
  const String networkText = WiFi.status() == WL_CONNECTED
      ? (config.showIp ? "IP " + WiFi.localIP().toString() : String())
      : (wifiPortal ? String(F("AP 192.168.4.1")) : String(F("WLAN")));
  drawChangedText(networkText, TFT_WHITE, DisplayLayout::NETWORK, network, fullRedraw);
  displayNeedsFullRedraw = false;
  displayDirty = false;
}

void handleTouch() {
  static bool wasArmed = false, wasPressed = false;
  const TouchInput::Event event = touchInput.update(digitalRead(Pins::TOUCH) == HIGH, millis());
  if (touchInput.isArmed() && !wasArmed) Serial.println(F("Touch: LOW erkannt, Sensor bereit"));
  if (touchInput.isPressed() != wasPressed)
    Serial.println(touchInput.isPressed() ? F("Touch: gedrueckt (HIGH)") : F("Touch: losgelassen (LOW)"));
  wasArmed = touchInput.isArmed();
  wasPressed = touchInput.isPressed();
  if (event == TouchInput::Event::SHORT_PRESS) {
    weatherPage = !weatherPage;
    if (!weatherPage) weatherAnimation.release();
    displayNeedsFullRedraw = true;
    Serial.println(weatherPage ? "Touch: Wetterseite" : "Touch: Claude-Seite");
    drawDisplay();
  } else if (event == TouchInput::Event::LONG_PRESS) {
    touchResetRequested = true;
  }
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
  wifiPortal.reset(new WiFiManager());
  WiFiManager &manager = *wifiPortal;
  manager.setHostname(HOSTNAME);
  manager.setConfigPortalTimeout(0);
  manager.setConfigPortalBlocking(false);
  manager.setWiFiAutoReconnect(true);
  manager.setConnectTimeout(20);
  manager.setBreakAfterConfig(false);
  manager.setAPCallback([](WiFiManager *wifiManager) {
    display.fillScreen(TFT_BLACK);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextFont(2);
    display.drawString("Setup", 120, 90);
    display.drawString("SSID:", 120, 120);
    display.drawString(wifiManager->getConfigPortalSSID(), 120, 150);
    drawDisplayName();
  });
  // Das Portal verwaltet bewusst nur WLAN. Claude-Daten werden später über /settings gepflegt.
  if (trySavedWifi) manager.autoConnect(SETUP_AP, SETUP_AP_PASSWORD);
  else manager.startConfigPortal(SETUP_AP, SETUP_AP_PASSWORD);
  displayNeedsFullRedraw = true;
}

void handleWifiRecovery() {
  if (wifiPortal) processWifiPortalIfRunning(*wifiPortal);
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  const bool fallbackDue = wifiRecovery.needsPortal(connected, now);
  if (connected) {
    if (wifiPortal) {
      stopWifiPortalIfRunning(*wifiPortal);
      wifiPortal.reset();
      WiFi.mode(WIFI_STA);
      displayNeedsFullRedraw = true;
      forceClaudeRefresh = true;
    }
    if (!normalWebRunning) setupWebServer();
    return;
  }
  if (fallbackDue && !wifiPortal) {
    Serial.println(F("WLAN seit 60 Sekunden getrennt; Setup-AP startet"));
    web.stop();
    MDNS.end();
    normalWebRunning = false;
    connectWifi(false);
  }
  if (wifiPortal && !wifiPortal->getConfigPortalActive()) {
    wifiPortal->startConfigPortal(SETUP_AP, SETUP_AP_PASSWORD);
    displayNeedsFullRedraw = true;
  }
  if (now - lastWifiRetryMs >= Timing::WIFI_RETRY_MS && WiFi.SSID().length()) {
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
  // Sichtbarer Selbsttest vor WLAN und Claude: benötigt keine Netzwerkverbindung.
  Serial.println(F("Display-Selbsttest: Rot, Gruen, Blau, Weiss"));
  for (uint16_t color : {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE}) {
    display.fillScreen(color);
    delay(500);
  }
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  display.drawString("WLAN verbinden ...", 120, 120);
  drawDisplayName();
  Serial.println(F("Display initialisiert; WLAN-Verbindung startet"));

  connectWifi();
  configTime(LOCAL_TIMEZONE, config.ntpServer.c_str());
  detectSensor();
  readSensor();
  handleWifiRecovery();
  forceClaudeRefresh = true;
  forceTrueConfRefresh = false;
  if (config.trueConf.enabled) trueConfResult.state = "Erster Abruf 60 Sekunden nach Webserver-Start";
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
  // Fällige Abrufe und manuelle Anforderungen während der Wetterseite behalten.
  // Beim Zurückwechseln greifen die bisherigen Intervalle automatisch wieder.
  if (!weatherPage && (forceClaudeRefresh || now - lastClaudeMs >= intervalMs)) {
    forceClaudeRefresh = false;
    lastClaudeMs = now;
    fetchClaudeUsage();
  }

  if (!weatherPage && config.trueConf.enabled && (forceTrueConfRefresh ||
      (millis() - webServerReadyMs >= Timing::TRUECONF_STARTUP_GRACE_MS &&
       millis() - lastTrueConfMs >= config.trueConf.intervalSec * 1000UL))) refreshTrueConf();

  if (WiFi.status() == WL_CONNECTED && config.weatherName.length() &&
      (forceWeatherRefresh || millis() - lastWeatherMs >= 900000UL)) {
    forceWeatherRefresh = false;
    lastWeatherMs = millis();
    WeatherResult next = fetchWeather(config.weatherLatitude, config.weatherLongitude);
    if (next.valid) weather = next;
    else weather.state = weather.valid ? "Alte Daten / Abruffehler" : next.state;
  }

  // Auch Countdown und Netzstatus prüfen; unveränderte Textzeilen erzeugen keine SPI-Ausgabe.
  if (millis() - lastDisplayMs >= Timing::DISPLAY_MS) {
    lastDisplayMs = millis();
    drawDisplay();
  }
  if (weatherPage && !updateAuthenticated)
    weatherAnimation.draw(weather.valid ? weather.code : -1, millis());
  yield();
}
