#pragma once
#include <stdint.h>

namespace BearSSL { class X509List; class PrivateKey; }

template <typename Socket>
void beginTrueConfSocket(Socket &socket, const char *host, uint16_t port, const uint8_t *fingerprint) {
  socket.beginSSL(host, port, "/websocket/chat_bot/", fingerprint, "json.v1");
  // WebSockets 2.7.3 initialisiert diese beiden Zeiger nicht im Konstruktor
  // oder beginSSL(). Stack-Instanzen können sonst zufällige Schlüssel nutzen.
  // Explizite Typen wählen die BearSSL- statt der const-char*-Überladung.
  socket.setSSLClientCertKey(static_cast<BearSSL::X509List *>(nullptr),
                             static_cast<BearSSL::PrivateKey *>(nullptr));
  socket.setReconnectInterval(1);
}
