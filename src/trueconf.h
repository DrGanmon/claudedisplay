#pragma once
#include <Arduino.h>

struct TrueConfConfig {
  bool enabled = false;
  String host;
  uint16_t port = 443;
  String username;
  String password;
  String token;  // Optional: vorhandener Bridge-JWT, kein REST-API-Token.
  String oauthClientId;
  String fingerprint;
  bool insecure = false;
  uint32_t intervalSec = 60;
};

struct TrueConfResult {
  bool valid = false;
  uint32_t unread = 0;
  String state = "Deaktiviert";
};

String validateTrueConfConfig(const TrueConfConfig &config);
TrueConfResult fetchTrueConfUnread(const TrueConfConfig &config);
