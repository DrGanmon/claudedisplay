#pragma once
#include <ArduinoJson.h>
#include <stdint.h>

inline void trueConfAuthRequest(JsonDocument &request, const char *token) {
  request["type"] = 1;
  request["id"] = 1;
  request["method"] = "auth";
  request["payload"]["token"] = token;
  request["payload"]["tokenType"] = "JWT";
  request["payload"]["receiveUnread"] = false;
  request["payload"]["receiveSystemMessageEnvelopes"] = false;
}

inline bool trueConfAccumulatePage(const JsonDocument &doc, uint32_t &total, size_t &count) {
  if (!doc["payload"].is<JsonArrayConst>()) return false;
  uint32_t sum = total;
  const JsonArrayConst chats = doc["payload"].as<JsonArrayConst>();
  for (JsonObjectConst chat : chats) {
    if (!chat["unreadMessages"].is<uint32_t>()) return false;
    const uint32_t unread = chat["unreadMessages"].as<uint32_t>();
    if (UINT32_MAX - sum < unread) return false;
    sum += unread;
  }
  count = chats.size();
  total = sum;
  return true;
}
