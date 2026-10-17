#include "../src/trueconf_socket.h"
#include <assert.h>
#include <string>

struct Socket {
  BearSSL::X509List *cert = reinterpret_cast<BearSSL::X509List *>(0x918);
  BearSSL::PrivateKey *key = reinterpret_cast<BearSSL::PrivateKey *>(0x1020);
  bool begun = false;
  const uint8_t *fingerprint = nullptr;
  void beginSSL(const char *host, uint16_t port, const char *path, const uint8_t *fp, const char *protocol) {
    assert(std::string(host) == "trueconf.example");
    assert(port == 443);
    assert(std::string(path) == "/websocket/chat_bot/");
    assert(std::string(protocol) == "json.v1");
    fingerprint = fp;
    begun = true;
  }
  void setSSLClientCertKey(BearSSL::X509List *certificate, BearSSL::PrivateKey *privateKey) {
    assert(begun);
    cert = certificate; key = privateKey;
  }
  void setSSLClientCertKey(const char *, const char *) { assert(false); }
  void setReconnectInterval(unsigned long interval) {
    assert(cert == nullptr && key == nullptr);
    assert(interval == 1);
  }
};

int main() {
  uint8_t fingerprint[20] = {};
  for (bool verify : {false, true}) {
    Socket socket;
    beginTrueConfSocket(socket, "trueconf.example", 443, verify ? fingerprint : nullptr);
    assert(socket.cert == nullptr && socket.key == nullptr);
    assert(socket.fingerprint == (verify ? fingerprint : nullptr));
  }
}
