#include "trueconf.h"
#include "trueconf_protocol.h"
#include "trueconf_socket.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <WiFiClientSecureBearSSL.h>

namespace {
bool fingerprintBytes(const String &text, uint8_t *bytes) {
  String hex;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == ':' || c == ' ') continue;
    if (!isxdigit(static_cast<unsigned char>(c))) return false;
    hex += c;
  }
  if (hex.length() != 40) return false;
  for (size_t i = 0; i < 20; ++i) bytes[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return true;
}

bool postToken(const TrueConfConfig &config, const char *path, const JsonDocument &request,
               const char *field, String &value, String &error) {
  BearSSL::WiFiClientSecure client;
  uint8_t fingerprint[20];
  if (config.insecure) client.setInsecure();
  else {
    if (!fingerprintBytes(config.fingerprint, fingerprint)) return false;
    client.setFingerprint(fingerprint);
  }
  client.setBufferSizes(16384, 512);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(15000);
  const String url = "https://" + config.host + ":" + String(config.port) + path;
  if (!http.begin(client, url)) { error = F("TrueConf: HTTPS konnte nicht starten"); return false; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  String body;
  serializeJson(request, body);
  const int code = http.POST(body);
  if (code != 200 && code != 201) {
    error = "TrueConf-Anmeldung: " + String(path) + " (" + String(code) + ")";
    if (code < 0) error += " " + HTTPClient::errorToString(code);
    http.end();
    return false;
  }
  JsonDocument filter, response;
  filter[field] = true;
  const auto parseError = deserializeJson(response, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (parseError || !response[field].is<String>()) {
    error = F("TrueConf-Anmeldung: Antwort enthält kein gültiges Token/Code");
    return false;
  }
  value = response[field].as<String>();
  return value.length() > 0;
}

bool getToken(const TrueConfConfig &config, String &token, String &error) {
  if (config.token.length()) { token = config.token; return true; }
  JsonDocument request;
  request["client_id"] = config.oauthClientId.length() ? config.oauthClientId : String("chat_bot");
  request["grant_type"] = "password";
  request["username"] = config.username;
  request["password"] = config.password;
  if (!config.oauthClientId.length()) {
    return postToken(config, "/bridge/api/client/v1/oauth/token", request, "access_token", token, error);
  }
  String restToken, code;
  if (!postToken(config, "/api/v4/oauth2/token", request, "access_token", restToken, error)) return false;
  request.clear();
  request["client_id"] = config.username;
  request["token"] = restToken;
  if (!postToken(config, "/bridge/api/client/v1/auth/web_rest_api_token", request, "authorization_code", code, error)) return false;
  request.clear();
  request["client_id"] = config.username;
  request["grant_type"] = "authorization_code";
  request["code"] = code;
  return postToken(config, "/bridge/api/client/v1/auth/token", request, "access_token", token, error);
}
}  // namespace

String validateTrueConfConfig(const TrueConfConfig &config) {
  if (!config.enabled) return F("");
  if (!config.host.length() || config.host.length() > 253) return F("TrueConf-Servername fehlt oder ist zu lang");
  for (size_t i = 0; i < config.host.length(); ++i) {
    const char c = config.host[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.')
      return F("TrueConf-Host: nur DNS-Name oder IPv4, ohne https://, Port oder Pfad");
  }
  if (!config.port) return F("TrueConf-Port muss zwischen 1 und 65535 liegen");
  if (!config.token.length() && (!config.username.length() || !config.password.length()))
    return F("TrueConf benötigt Benutzername und Passwort oder einen Bridge-JWT");
  uint8_t fingerprint[20];
  if (!config.insecure && !fingerprintBytes(config.fingerprint, fingerprint))
    return F("TrueConf benötigt einen SHA1-Zertifikatfingerprint (40 Hex-Zeichen) oder die aktivierte Option ohne Zertifikatsprüfung");
  return F("");
}

TrueConfResult fetchTrueConfUnread(const TrueConfConfig &config) {
  TrueConfResult result;
  if (!config.enabled) return result;
  result.state = validateTrueConfConfig(config);
  if (result.state.length()) return result;
  if (WiFi.status() != WL_CONNECTED) { result.state = F("TrueConf: WLAN getrennt"); return result; }
  // HTTP-Anmeldung und WSS laufen nacheinander, nie parallel zu Claude-TLS.
  String token;
  if (!getToken(config, token, result.state)) return result;
  WebSocketsClient socket;
  bool finished = false;
  uint32_t page = 1, total = 0, requestId = 1;
  constexpr uint32_t PAGE_SIZE = 3;
  String fragments;
  auto send = [&](JsonDocument &doc) {
    String message;
    serializeJson(doc, message);
    if (!socket.sendTXT(message)) { result.state = F("TrueConf: Senden fehlgeschlagen"); finished = true; }
  };
  auto getPage = [&]() {
    JsonDocument request;
    request["type"] = 1;
    request["id"] = ++requestId;
    request["method"] = "getChats";
    request["payload"]["count"] = PAGE_SIZE;
    request["payload"]["page"] = page;
    send(request);
  };
  auto response = [&](uint8_t *payload, size_t length) {
    JsonDocument filter, doc;
    filter["type"] = true;
    filter["id"] = true;
    filter["errorCode"] = true;
    filter["payload"]["errorCode"] = true;
    filter["payload"]["userId"] = true;
    if (length > 8192) { result.state = F("TrueConf: Antwort größer als 8 KiB"); finished = true; return; }
    if (deserializeJson(doc, static_cast<const uint8_t *>(payload), length, DeserializationOption::Filter(filter))) {
      result.state = F("TrueConf: Ungültige JSON-Antwort"); finished = true; return;
    }
    // Ereignisse absichtlich NICHT bestätigen: ACK kann als Gelesenmarkierung wirken.
    if ((doc["type"] | 0) != 2 || (doc["id"] | 0U) != requestId) return;
    if (!doc["errorCode"].isNull() || !doc["payload"]["errorCode"].isNull()) {
      const int code = doc["errorCode"] | (doc["payload"]["errorCode"] | -1);
      result.state = "TrueConf: API-Fehler " + String(code); finished = true; return;
    }
    if (requestId == 1) {
      if (!doc["payload"]["userId"].is<String>()) {
        result.state = F("TrueConf: Anmeldung nicht bestätigt"); finished = true; return;
      }
      getPage(); return;
    }
    filter.clear();
    filter["payload"][0]["unreadMessages"] = true;
    // Read-only-Eingabe: ArduinoJson darf den WebSocket-Puffer beim ersten
    // Durchlauf nicht verändern, da anschließend die Chat-Zähler gelesen werden.
    doc.clear();
    if (deserializeJson(doc, static_cast<const uint8_t *>(payload), length, DeserializationOption::Filter(filter))) {
      result.state = F("TrueConf: Chat-Antwort ungültig"); finished = true; return;
    }
    size_t count = 0;
    if (!trueConfAccumulatePage(doc, total, count)) {
      result.state = F("TrueConf: Chat-Zähler fehlen oder sind ungültig"); finished = true; return;
    }
    if (count < PAGE_SIZE) {
      result.unread = total; result.valid = true; result.state = F("Verbunden"); finished = true;
    } else if (++page > 1000) {
      result.state = F("TrueConf: Zu viele Chats; kein vollständiger Zähler"); finished = true;
    } else getPage();
  };
  socket.onEvent([&](WStype_t type, uint8_t *payload, size_t length) {
    if (finished) return;
    if (type == WStype_CONNECTED) {
      JsonDocument auth;
      trueConfAuthRequest(auth, token.c_str());
      send(auth);
    } else if (type == WStype_TEXT) response(payload, length);
    else if (type == WStype_FRAGMENT_TEXT_START || type == WStype_FRAGMENT || type == WStype_FRAGMENT_FIN) {
      if (type == WStype_FRAGMENT_TEXT_START) fragments = "";
      if (fragments.length() + length > 8192 || !fragments.concat(reinterpret_cast<char *>(payload), length)) {
        result.state = F("TrueConf: Nachricht zu groß / Speicher knapp"); finished = true; return;
      }
      if (type == WStype_FRAGMENT_FIN) { response(reinterpret_cast<uint8_t *>(&fragments[0]), fragments.length()); fragments = ""; }
    } else if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
      result.state = F("TrueConf: WSS getrennt (Host, Port, Zertifikat oder Antwortgröße prüfen)"); finished = true;
    }
  });
  uint8_t fingerprint[20];
  fingerprintBytes(config.fingerprint, fingerprint);
  beginTrueConfSocket(socket, config.host.c_str(), config.port,
                     config.insecure ? nullptr : fingerprint);
  result.state = F("TrueConf: Zeitüberschreitung");
  const uint32_t started = millis();
  while (!finished && millis() - started < 30000) { socket.loop(); delay(1); }
  finished = true;
  socket.disconnect();
  socket.onEvent(nullptr);
  return result;
}
