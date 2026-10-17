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
constexpr uint32_t TOUCH_DEBOUNCE_MS = 35;
constexpr uint32_t TOUCH_RESET_MS = 5000;
}  // namespace Timing

constexpr char CONFIG_FILE[] = "/config.json";
constexpr char HOSTNAME[] = "claude-display";
constexpr char SETUP_AP[] = "ClaudeDisplay-Setup";
constexpr char SETUP_AP_PASSWORD[] = "claude-display";
constexpr char DEFAULT_WEB_PASSWORD[] = "claude-display";
constexpr char CLAUDE_BASE[] = "https://claude.ai/api/organizations";

struct Config {
  String sessionKey;
  String orgId;
  String extraCookies;
  String userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36";
  String webPassword = DEFAULT_WEB_PASSWORD;
  uint32_t intervalSec = 180;
};

struct LimitBucket {
  bool valid = false;
  float used = 0.0f;
  String resetsAt;
};

Config config;
LimitBucket sessionLimit;
LimitBucket weeklyLimit;
LimitBucket opusLimit;
LimitBucket sonnetLimit;

TFT_eSPI display;
ESP8266WebServer web(80);
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
bool updateAuthenticated = false;
bool updateSuccessful = false;
String updateError;

bool touchRaw = false;
bool touchStable = false;
bool touchLongHandled = false;
uint32_t touchChangedMs = 0;
uint32_t touchPressedMs = 0;

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

String percentText(const LimitBucket &bucket, bool remaining = true) {
  if (!bucket.valid) return "--";
  const float value = remaining ? 100.0f - bucket.used : bucket.used;
  return String(constrain(value, 0.0f, 100.0f), 0);
}

uint16_t limitColor(float used) {
  if (used >= 80.0f) return TFT_RED;
  if (used > 60.0f) return TFT_YELLOW;
  return TFT_GREEN;
}

bool loadConfig() {
  if (!LittleFS.exists(CONFIG_FILE)) return false;
  File file = LittleFS.open(CONFIG_FILE, "r");
  if (!file) return false;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return false;
  config.sessionKey = doc["sessionKey"] | "";
  config.orgId = doc["orgId"] | "";
  config.extraCookies = doc["extraCookies"] | "";
  config.userAgent = doc["userAgent"] | config.userAgent;
  config.webPassword = doc["webPassword"] | DEFAULT_WEB_PASSWORD;
  config.intervalSec = constrain(static_cast<uint32_t>(doc["intervalSec"] | 180), 60UL, 3600UL);
  return true;
}

bool saveConfig() {
  JsonDocument doc;
  doc["sessionKey"] = config.sessionKey;
  doc["orgId"] = config.orgId;
  doc["extraCookies"] = config.extraCookies;
  doc["userAgent"] = config.userAgent;
  doc["webPassword"] = config.webPassword;
  doc["intervalSec"] = config.intervalSec;
  File file = LittleFS.open(CONFIG_FILE, "w");
  if (!file) return false;
  const bool ok = serializeJson(doc, file) > 0;
  file.close();
  return ok;
}

void beginPage(const __FlashStringHelper *title) {
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
    web.sendContent(htmlEscape(sessionLimit.resetsAt));
  }
  if (weeklyLimit.resetsAt.length()) {
    web.sendContent(F("<br>Wochen-Reset: "));
    web.sendContent(htmlEscape(weeklyLimit.resetsAt));
  }
  web.sendContent(F("</p><p>Status: "));
  web.sendContent(htmlEscape(claudeState));
  web.sendContent(F("<br>Letzte Aktualisierung: "));
  web.sendContent(htmlEscape(lastClaudeUpdate));
  web.sendContent(F("</p><form method='post' action='/refresh'><button>Jetzt aktualisieren</button></form></div>"));
  web.sendContent(F("<div class='card'><h2>Gerät</h2><p>IP: "));
  web.sendContent(WiFi.localIP().toString());
  web.sendContent(F("<br>WLAN-Signal: "));
  web.sendContent(String(WiFi.RSSI()));
  web.sendContent(F(" dBm<br>Freier Heap: "));
  web.sendContent(String(ESP.getFreeHeap()));
  web.sendContent(F(" Bytes</p></div>"));
  endPage();
}

void handleSettings() {
  if (!requireSettingsAuth()) return;
  beginPage(F("Einstellungen"));
  web.sendContent(F("<div class='card'><form method='post' action='/settings'>"
                    "<p class='warn'>Der sessionKey entspricht deiner Claude-Anmeldung. Das Feld leer lassen, um den gespeicherten Wert beizubehalten.</p>"
                    "<label>Claude sessionKey<input type='password' name='sessionKey' autocomplete='new-password' placeholder='"));
  web.sendContent(config.sessionKey.length() ? "gespeichert" : "nicht gespeichert");
  web.sendContent(F("'></label><label>Organisations-ID<input name='orgId' value='"));
  web.sendContent(htmlEscape(config.orgId));
  web.sendContent(F("'></label><label>Weitere Cookies (optional)<input type='password' name='extraCookies' placeholder='"));
  web.sendContent(config.extraCookies.length() ? "gespeichert" : "z. B. cf_clearance=..." );
  web.sendContent(F("'></label><label>User-Agent<input name='userAgent' value='"));
  web.sendContent(htmlEscape(config.userAgent));
  web.sendContent(F("'></label><label>Abrufintervall in Sekunden (60–3600)<input type='number' min='60' max='3600' name='intervalSec' value='"));
  web.sendContent(String(config.intervalSec));
  web.sendContent(F("'></label><label>Neues Web-Passwort<input type='password' name='webPassword' autocomplete='new-password' placeholder='leer = beibehalten'></label>"
                    "<button>Speichern</button></form></div>"
                    "<div class='card'><p>Benutzername der Einstellungsseite: <b>admin</b></p>"
                    "<p>WLAN neu einrichten: TTP223 mindestens 5 Sekunden berühren.</p></div>"));
  endPage();
}

void handleSaveSettings() {
  if (!requireSettingsAuth()) return;
  const String newSessionKey = web.arg("sessionKey");
  const String newExtraCookies = web.arg("extraCookies");
  const String newPassword = web.arg("webPassword");
  if (newSessionKey.length()) {
    config.sessionKey = newSessionKey;
    if (config.sessionKey.startsWith("sessionKey=")) config.sessionKey.remove(0, 11);
  }
  if (newExtraCookies.length()) config.extraCookies = newExtraCookies;
  if (newPassword.length() >= 8) config.webPassword = newPassword;
  config.orgId = web.arg("orgId");
  config.userAgent = web.arg("userAgent");
  config.intervalSec = constrain(static_cast<uint32_t>(web.arg("intervalSec").toInt()), 60UL, 3600UL);
  const bool ok = saveConfig();
  forceClaudeRefresh = true;
  web.sendHeader("Location", ok ? "/" : "/settings");
  web.send(303, "text/plain", ok ? "Gespeichert" : "Speichern fehlgeschlagen");
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
  web.on("/", HTTP_GET, handleStatus);
  web.on("/settings", HTTP_GET, handleSettings);
  web.on("/settings", HTTP_POST, handleSaveSettings);
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
    doc["free_heap"] = ESP.getFreeHeap();
    String body;
    serializeJson(doc, body);
    web.send(200, "application/json", body);
  });
  web.onNotFound([]() { web.send(404, "text/plain", "Nicht gefunden"); });
  web.begin();
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
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
  http.addHeader("Cookie", cookieHeader());
  http.addHeader("Accept", "application/json");
  http.addHeader("Referer", "https://claude.ai/settings/usage");
  http.setUserAgent(config.userAgent);
}

String httpErrorText(int code) {
  switch (code) {
    case 401: return "Sitzung abgelaufen (401)";
    case 403: return "Zugriff/Cloudflare (403)";
    case 404: return "Organisation nicht gefunden (404)";
    case 429: return "Zu viele Anfragen (429)";
    default: return "HTTP-Fehler " + String(code);
  }
}

bool discoverOrganization() {
  BearSSL::WiFiClientSecure client;
  // ESP8266 besitzt keinen gepflegten System-Zertifikatsspeicher. Siehe README.
  client.setInsecure();
  client.setBufferSizes(512, 512);
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, CLAUDE_BASE)) return false;
  applyClaudeHeaders(http);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    claudeState = httpErrorText(code);
    http.end();
    return false;
  }
  JsonDocument filter;
  filter[0]["uuid"] = true;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  if (!error && doc.is<JsonArray>() && doc.size() > 0) config.orgId = doc[0]["uuid"].as<String>();
  http.end();
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
  bucket.valid = true;
}

void fetchClaudeUsage() {
  if (WiFi.status() != WL_CONNECTED || !config.sessionKey.length()) {
    claudeState = config.sessionKey.length() ? "WLAN getrennt" : "sessionKey fehlt";
    return;
  }
  claudeState = "Aktualisiere ...";
  displayDirty = true;
  if (!config.orgId.length() && !discoverOrganization()) {
    if (claudeState == "Aktualisiere ...") claudeState = "Keine Organisation gefunden";
    return;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);
  HTTPClient http;
  http.setTimeout(15000);
  const String url = String(CLAUDE_BASE) + "/" + config.orgId + "/usage";
  if (!http.begin(client, url)) {
    claudeState = "HTTPS konnte nicht starten";
    return;
  }
  applyClaudeHeaders(http);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    claudeState = httpErrorText(code);
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
    claudeState = "Antwort ohne bekannte Limits";
    return;
  }
  claudeState = "Verbunden";
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

void drawLimitRing(int radius, int thickness, const LimitBucket &bucket) {
  const int inner = radius - thickness;
  display.drawArc(120, 120, radius, inner, 0, 360, TFT_DARKGREY, TFT_BLACK, true);
  if (!bucket.valid) return;
  const float remaining = constrain(100.0f - bucket.used, 0.0f, 100.0f);
  const int endAngle = static_cast<int>(360.0f * remaining / 100.0f);
  if (endAngle > 0) display.drawArc(120, 120, radius, inner, 0, endAngle, limitColor(bucket.used), TFT_BLACK, true);
}

void drawDisplay() {
  display.fillScreen(TFT_BLACK);

  // Außen: Sitzung, deutlich dicker. Innen: Wochenlimit.
  drawLimitRing(119, 15, sessionLimit);
  drawLimitRing(98, 7, weeklyLimit);

  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(4);
  const String temperature = isnan(temperatureC) ? "--.- C" : String(temperatureC, 1) + " C";
  display.drawString(temperature, 120, 82);

  display.setTextFont(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("frei", 120, 108);

  display.setTextFont(4);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("S " + percentText(sessionLimit) + "%", 120, 137);

  display.setTextFont(2);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("W " + percentText(weeklyLimit) + "%", 120, 166);

  if (WiFi.status() != WL_CONNECTED) {
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.drawString("WLAN", 120, 190);
  }
  displayDirty = false;
}

void handleTouch() {
  const bool raw = digitalRead(Pins::TOUCH) == HIGH;
  const uint32_t now = millis();
  if (raw != touchRaw) {
    touchRaw = raw;
    touchChangedMs = now;
  }
  if (now - touchChangedMs < Timing::TOUCH_DEBOUNCE_MS || raw == touchStable) return;
  touchStable = raw;
  if (touchStable) {
    touchPressedMs = now;
    touchLongHandled = false;
  } else if (!touchLongHandled) {
    forceClaudeRefresh = true;
  }
}

void handleLongTouch() {
  if (!touchStable || touchLongHandled || millis() - touchPressedMs < Timing::TOUCH_RESET_MS) return;
  touchLongHandled = true;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  display.drawString("WLAN wird geloescht", 120, 105);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString("Neustart ...", 120, 135);
  WiFiManager manager;
  manager.resetSettings();
  delay(1000);
  ESP.restart();
}

void connectWifi() {
  WiFi.persistent(true);
  WiFi.hostname(HOSTNAME);
  WiFiManager manager;
  manager.setHostname(HOSTNAME);
  manager.setConfigPortalTimeout(300);
  manager.setConnectTimeout(20);
  manager.setBreakAfterConfig(false);
  // Das Portal verwaltet bewusst nur WLAN. Claude-Daten werden später über /settings gepflegt.
  if (!manager.autoConnect(SETUP_AP, SETUP_AP_PASSWORD)) ESP.restart();
  WiFi.mode(WIFI_STA);  // Setup-AP nach erfolgreicher Anmeldung abschalten.
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::TOUCH, INPUT);
  LittleFS.begin();
  loadConfig();

  display.init();
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextFont(2);
  display.drawString("WLAN verbinden ...", 120, 120);

  connectWifi();
  detectSensor();
  readSensor();
  setupWebServer();
  forceClaudeRefresh = true;
  drawDisplay();
}

void loop() {
  const uint32_t now = millis();
  web.handleClient();
  MDNS.update();
  handleTouch();
  handleLongTouch();

  if (now - lastSensorMs >= Timing::SENSOR_MS) {
    lastSensorMs = now;
    readSensor();
  }

  if (WiFi.status() != WL_CONNECTED && now - lastWifiRetryMs >= Timing::WIFI_RETRY_MS) {
    lastWifiRetryMs = now;
    WiFi.reconnect();
  }

  const uint32_t configuredIntervalMs = config.intervalSec * static_cast<uint32_t>(1000);
  const uint32_t intervalMs = max(Timing::CLAUDE_MIN_MS, configuredIntervalMs);
  if (forceClaudeRefresh || now - lastClaudeMs >= intervalMs) {
    forceClaudeRefresh = false;
    lastClaudeMs = now;
    fetchClaudeUsage();
  }

  if (displayDirty && now - lastDisplayMs >= Timing::DISPLAY_MS) {
    lastDisplayMs = now;
    drawDisplay();
  }
  yield();
}
