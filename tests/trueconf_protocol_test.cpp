#include "../src/trueconf_protocol.h"
#include <assert.h>
#include <string>

int main() {
  JsonDocument auth;
  trueConfAuthRequest(auth, "test-jwt");
  assert(auth["method"] == "auth");
  assert(auth["payload"]["token"] == "test-jwt");
  assert(auth["payload"]["receiveUnread"].is<bool>());
  assert(!auth["payload"]["receiveUnread"].as<bool>());
  assert(!auth["payload"]["receiveSystemMessageEnvelopes"].as<bool>());
  const char *message = R"({"type":2,"id":2,"payload":[{"unreadMessages":2,"lastMessage":{"content":{"text":"ignored"}}},{"unreadMessages":5}]})";
  JsonDocument filter, doc;
  filter["type"] = true;
  filter["id"] = true;
  filter["payload"]["errorCode"] = true;
  filter["payload"]["userId"] = true;
  assert(!deserializeJson(doc, message, DeserializationOption::Filter(filter)));
  assert(doc["id"] == 2);
  filter.clear();
  filter["payload"][0]["unreadMessages"] = true;
  assert(!deserializeJson(doc, message, DeserializationOption::Filter(filter)));
  assert(doc["payload"][0]["lastMessage"].isNull());
  uint32_t total = 0;
  size_t count = 0;
  assert(trueConfAccumulatePage(doc, total, count) && total == 7 && count == 2);
  assert(!deserializeJson(doc, R"({"payload":[{"unreadMessages":3}]})"));
  assert(trueConfAccumulatePage(doc, total, count) && total == 10 && count == 1);
  assert(!deserializeJson(doc, R"({"payload":[]})"));
  assert(trueConfAccumulatePage(doc, total, count) && total == 10 && count == 0);
  for (const char *bad : {R"({"payload":[{}]})", R"({"payload":[{"unreadMessages":-1}]})",
       R"({"payload":[{"unreadMessages":"3"}]})", R"({"payload":{}})",
       R"({"payload":[{"unreadMessages":4294967295}]})"}) {
    assert(!deserializeJson(doc, bad));
    assert(!trueConfAccumulatePage(doc, total, count));
    assert(total == 10);
  }
  // Die zwei Durchläufe dürfen den ursprünglichen WebSocket-Text nicht verändern.
  const std::string original(message);
  assert(original.find("ignored") != std::string::npos);
}
