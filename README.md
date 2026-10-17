# Claude Display für Wemos D1 Mini

Eigenständige Firmware für einen ESP8266/Wemos D1 Mini. Das Gerät zeigt Raumklima und die aktuellen Claude-Nutzungslimits auf einem runden GC9A01-Display an. Konfiguration und Status sind dauerhaft über eine lokale Webseite erreichbar.

## Hardware

- Wemos D1 Mini mit ESP8266
- rundes GC9A01A-SPI-Display mit 240 × 240 Pixeln
- optional: TTP223-Touchsensor für manuelle Aktualisierung und WLAN-Reset
- optional: BME280 für Temperatur, Luftdruck und Luftfeuchtigkeit oder
- optional: BMP280 für Temperatur und Luftdruck ohne Luftfeuchtigkeit
- geeignetes USB-Netzteil

## Funktionen

- Einrichtung des WLANs über eigenen Access Point und Captive Portal
- nach der Einrichtung ausschließlich normaler WLAN-Clientbetrieb
- dauerhafte lokale Webseite unter `http://claude-display.local/`
- geschützte Konfigurationsseite für Claude-Zugang und Abrufintervall
- direkter Abruf der Claude-Sitzungs- und Wochenlimits
- automatische Ermittlung der Claude-Organisations-ID
- optionaler TrueConf-Zähler für ungelesene Nachrichten, direkt per HTTPS/WSS ohne Vermittlungs-PC
- Anzeige von Temperatur, Luftdruck und – mit BME280 – Luftfeuchtigkeit
- Status-API im JSON-Format
- passwortgeschütztes Firmwareupdate per Datei-Upload im Browser
- automatische WLAN-Wiederverbindung
- Unterstützung von BMP280 und BME280 an den Adressen `0x76` und `0x77`
- Betrieb ohne Klimasensor oder Touchsensor; das Display ist zwingend erforderlich
- kompakte Anzeige ohne vollständigen Bildpuffer für den begrenzten ESP8266-RAM

### Display

- frei wählbarer Display-Name unter `Einstellungen` → `Display-Einstellungen speichern`; bleibt auch ohne Claude-Zugang und Klimasensor sichtbar
- IP-Adresse auf dem Display optional, standardmäßig eingeschaltet; über die Checkbox in den Display-Einstellungen ausschaltbar
- schwarzer Hintergrund
- weiße Schrift für Name, Klima und Netzwerk; Limit-Zahlen gehen mit steigendem Verbrauch stufenlos von Grün (0 % verbraucht) zu demselben kräftigen Rot wie der Ring (100 % verbraucht) über
- äußerer, 15 Pixel dicker Ring: Fünf-Stunden-Sitzungslimit
- innerer, 7 Pixel dicker Ring: Wochenlimit
- grüner Ringanteil entspricht den noch freien Prozenten
- übriger Ringanteil entspricht den verbrauchten Prozenten; seine Farbe geht mit steigendem Gesamtverbrauch stufenlos von Grau (`#808080` bei 0 %) zu kräftigem Rot (`#FF0000` bei 100 %) über
- unbekannte Limits erscheinen als vollständig dunkelgrauer Ring
- im normalen Betrieb nur geänderte Textzeilen und Ringsegmente aktualisieren; kleine Textpuffer vermeiden sichtbares Löschen vor dem Neuzeichnen
- Mitte: Temperatur, freie Prozente und Reset-Countdowns für Sitzung (`S`) und Woche (`W`), zum Beispiel `S 75% 0 03:25`
- Countdownformat: Tage, Stunden und Minuten (`D HH:mm`); ohne gültige Uhrzeit oder Reset-Angabe `-- --:--`, nach Ablauf `0 00:00`
- kein zusätzlicher Text „frei“; optional `TC 5` für fünf ungelesene TrueConf-Nachrichten
- alle Textflächen liegen mit mindestens zwei Pixeln Abstand innerhalb der inneren Ringkante; lange Texte werden verkleinert beziehungsweise auf ihre Textfläche begrenzt

### Bedienung des optionalen TTP223

- falls angeschlossen, kurz berühren: Claude-Limits sofort aktualisieren
- falls angeschlossen, mindestens fünf Sekunden berühren: WLAN-Zugangsdaten löschen und ESP8266 neu starten
- nach dem Neustart öffnet sich automatisch das Captive Portal zur Auswahl eines neuen WLANs
- Claude-Zugang und übrige Gerätekonfiguration bleiben dabei erhalten
- ohne Klimasensor zeigt das Gerät für Temperatur, Luftfeuchtigkeit und Luftdruck `--` an.
- ohne TTP223 entfallen die Touch-Aktionen; WLAN und Gerät lassen sich weiterhin über die Webseite bedienen.

## Wiring Diagram

```text
                          Wemos D1 Mini / ESP8266
                     ┌────────────────────────────┐
                     │                            │
  GC9A01 CLK/SCL ────┤ D5 / GPIO14                │
  GC9A01 MOSI/SDA ───┤ D7 / GPIO13                │
  GC9A01 DC ─────────┤ D4 / GPIO2                 │
  GC9A01 RST ────────┤ D0 / GPIO16                │
                     │                            │
 TTP223 OUT ────────┤ D6 / GPIO12 (optional)     │
                     │                            │
 BMP/BME280 SCL ────┤ D1 / GPIO5 (optional)      │
 BMP/BME280 SDA ────┤ D2 / GPIO4 (optional)      │
                     │                            │
  alle VCC ──────────┤ 3V3                        │
  alle GND ──────────┤ GND                        │
                     └────────────────────────────┘

  GC9A01 CS  ───────────── GND
  GC9A01 BL/LED ────────── 3V3 (*)
  GC9A01 SDO/MISO ──────── nicht anschließen

  (*) Nur wenn das verwendete Displaymodul den direkten Anschluss seiner
      Hintergrundbeleuchtung an 3,3 V erlaubt.
```

### Pin-Tabelle

| Modul | Modul-Pin | Wemos D1 Mini | ESP8266-GPIO | Bemerkung |
|---|---|---|---:|---|
| GC9A01 | VCC | 3V3 | – | keine 5-V-Logik verwenden |
| GC9A01 | GND | GND | – | gemeinsame Masse |
| GC9A01 | CLK/SCL | D5 | 14 | SPI-Takt |
| GC9A01 | MOSI/SDA | D7 | 13 | SPI-Daten zum Display |
| GC9A01 | DC | D4 | 2 | Daten/Befehl; Boot-Pin nicht zusätzlich belasten |
| GC9A01 | RST/RES | D0 | 16 | Display-Reset |
| GC9A01 | CS | GND | – | Display dauerhaft ausgewählt |
| GC9A01 | BL/LED | 3V3 | – | abhängig vom konkreten Displaymodul |
| GC9A01 | SDO/MISO | offen | – | wird nicht benötigt |
| BMP/BME280 (optional) | VCC/VIN | 3V3 | – | Sensorversorgung |
| BMP/BME280 (optional) | GND | GND | – | gemeinsame Masse |
| BMP/BME280 (optional) | SCL | D1 | 5 | I²C-Takt |
| BMP/BME280 (optional) | SDA | D2 | 4 | I²C-Daten |
| TTP223 (optional) | VCC | 3V3 | – | Sensorversorgung |
| TTP223 (optional) | GND | GND | – | gemeinsame Masse |
| TTP223 (optional) | OUT/SIG | D6 | 12 | Touchsignal, aktiv HIGH |

Alle Komponenten müssen eine gemeinsame Masse besitzen. Die Signale des ESP8266 arbeiten mit 3,3 V. Vor dem Anschluss der Display-Hintergrundbeleuchtung bitte die Beschriftung beziehungsweise Dokumentation des konkreten GC9A01-Moduls prüfen.

## Einrichtung

1. Firmware mit PlatformIO kompilieren und hochladen.
2. Mit dem WLAN `ClaudeDisplay-Setup` verbinden. Das initiale Passwort lautet `claude-display`.
3. Im automatisch geöffneten Portal ausschließlich das Ziel-WLAN auswählen und speichern.
4. Danach im Heimnetz `http://claude-display.local/` öffnen. Falls mDNS nicht funktioniert, die im Router angezeigte IP verwenden.
5. `Einstellungen` öffnen. Initiale Zugangsdaten: Benutzer `admin`, Passwort `claude-display`.
6. Den `sessionKey` aus dem Browser, optional weitere Cookies und gegebenenfalls die Organisations-ID eintragen. Ohne Organisations-ID versucht das Gerät die erste verfügbare Organisation automatisch zu ermitteln.
7. `Claude-Einstellungen und Session-Key speichern` drücken. Anschließend unter Einstellungen mit `Gespeicherten Session-Key testen` die Verbindung prüfen. Der Test zeigt das Ergebnis an und aktualisiert bei Erfolg die Claude-Limits.
8. Im Abschnitt `Web-Passwort ändern` das aktuelle Passwort und zweimal das neue Passwort (mindestens 8 Zeichen) eingeben und `Passwort ändern` drücken. Danach mit Benutzer `admin` und dem neuen Passwort anmelden.

Falls ein TTP223 angeschlossen ist, startet eine kurze Berührung eine sofortige Claude-Aktualisierung. Eine Berührung von mindestens fünf Sekunden löscht die gespeicherten WLAN-Zugangsdaten und startet das Gerät neu. Da danach keine WLAN-Konfiguration vorhanden ist, öffnet sich automatisch das Captive Portal zur Auswahl eines neuen WLANs. Claude-Konfiguration und Anzeigeeinstellungen bleiben erhalten.

## Webseite

Die aktuelle Firmware zeigt auf jeder Webseite die Version `2026-10-17.13`. Fehlen die neuen Buttons, zuerst die neu kompilierte `.pio/build/d1_mini/firmware.bin` unter `/update` installieren und danach `/settings` neu laden. Änderungen am Quellcode allein aktualisieren das Gerät nicht.

Version `2026-10-17.11` behebt vorzeitig abgeschnittene Einstellungsseiten bei leeren Konfigurationswerten. Leere Servernamen, Organisations-IDs oder andere optionale Textfelder dürfen keinen leeren HTTP-Chunk auslösen, da dieser die Antwort beendet. Der TrueConf-Abschnitt zeigt deshalb auch vor der ersten Konfiguration alle Felder und beide Buttons vollständig an.

Beim Neustart zeigt das Display vor dem WLAN-Verbindungsaufbau jeweils eine halbe Sekunde Rot, Grün, Blau und Weiß. Dieser Selbsttest läuft unabhängig von WLAN, Sensoren und Claude. Bleiben auch diese Flächen unsichtbar, liegt das Problem vor der normalen Statusanzeige. Der SPI-Takt ist zur Fehlersuche auf 10 MHz reduziert. Der serielle Monitor mit 115200 Baud zeigt Firmwareversion, erwartete Display-Verdrahtung und den Start des Selbsttests; diese Meldungen bestätigen den Programmablauf, aber keine erfolgreiche Kommunikation mit dem Display.

Der Display-Name wird separat gespeichert und auch während des WLAN-Verbindungsaufbaus und im Setup angezeigt. Ein vollständig schwarzes Display trotz neuer Firmware deutet auf ein Problem mit Versorgung, Hintergrundbeleuchtung, Verkabelung oder dem verwendeten Displaymodul hin. Insbesondere `BL/LED`, gemeinsame Masse sowie CLK, MOSI, DC und RST entsprechend der Pin-Tabelle prüfen.

### Claude-Verbindung prüfen

Standard ist ein Firefox-140-Profil für Windows: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:140.0) Gecko/20100101 Firefox/140.0`. Der bisherige Chrome-130-Standard wird beim Laden automatisch ersetzt; individuelle User-Agents bleiben erhalten. Mit `Firefox unter Windows einsetzen` kann die Vorgabe im Webformular eingesetzt und anschließend gespeichert werden. Beide Claude-Anfragen senden außerdem `Accept-Language` und die Fetch-Metadaten `Sec-Fetch-Dest: empty`, `Sec-Fetch-Mode: cors`, `Sec-Fetch-Site: same-origin`.

Diese Header ändern weder den TLS-Client des ESP8266 noch führen sie Browser-JavaScript aus. Eine Freigabe durch Cloudflare ist damit nicht garantiert. HTTP/1.0 und unkomprimierte Antworten bleiben für die speichersparende JSON-Verarbeitung erforderlich. Bei übernommenen Browser-Cookies den tatsächlichen User-Agent dieses Browsers verwenden. Das Format des Firefox-User-Agents beschreibt [Mozilla](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/User-Agent/Firefox).

Beim Vergleich mit `claudeusage.ps1` dieselbe Organisations-ID, dieselben zusätzlichen Cookies und denselben User-Agent verwenden; der Session-Key allein bildet nicht zwingend die vollständige Konfiguration ab. Die Firmware entfernt wie das Skript äußere Leerzeichen und das optionale Präfix `sessionKey=`. Geheimnisse werden nicht in Fehlermeldungen ausgegeben.

Seit Version `2026-10-17.6` extrahiert die Firmware wie das PS1-Skript die erste UUID aus dem Organisationsfeld, etwa aus `Name [UUID]` oder einer URL. Das gilt auch für bereits gespeicherte Werte. Ungültige nichtleere Organisationsfelder werden beim Speichern abgewiesen und vor einem Abruf geprüft. Bei einem 404 zeigt die Fehlermeldung die tatsächlich verwendete UUID zum Vergleich mit dem PS1 an. Ein 404 allein beweist weder einen ungültigen Session-Key noch eine nicht existierende Organisation; der Endpunkt kann auch für die Sitzung unzugänglich sein.

Negative Fehlernummern stammen vom ESP8266-HTTPClient, nicht von Claude. `-4` bedeutet in der verwendeten Bibliothek „not connected“, bevor die HTTP-Antwort gelesen werden konnte. Seit Version `2026-10-17.4` verwenden Organisationssuche und Limit-Abruf einen TLS-Empfangspuffer von 16 KiB statt unbestätigt verkleinerter Puffer. HTTP/1.0 und `Accept-Encoding: identity` vermeiden Chunk-Framing beziehungsweise Kompression beim direkten JSON-Lesen aus dem Stream. Fehler zeigen den betroffenen Abruf, den HTTPClient-Fehlertext und bei Transportfehlern zusätzlich verfügbare TLS-Details und freien Heap. Erkannte Cloudflare-Challenges werden gesondert gemeldet.

Hintergrund: [ESP8266-TLS-Puffer und MFLN](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/bearssl-client-secure-class.html#mfln-or-maximum-fragment-length-negotiation-saving-ram), [ArduinoJson mit HTTPClient](https://arduinojson.org/v7/how-to/use-arduinojson-with-httpclient/).

### TrueConf-Nachrichtenzähler einrichten

Version `2026-10-17.13` behebt eine Neustartschleife beim TrueConf-WSS-Aufbau: Die verwendete WebSockets-Version 2.7.3 initialisiert bei lokalen Client-Instanzen die optionalen Client-Zertifikat- und Private-Key-Zeiger nicht vollständig. Die Firmware setzt beide vor dem ersten Verbindungsversuch ausdrücklich auf `nullptr`. Das betrifft die Client-Authentifizierung, die Server-Zertifikatsprüfung per Fingerprint bleibt erhalten. Der zugehörige Absturz zeigte `Exception (28)` in `BearSSL::PrivateKey::isRSA()` aus `WebSocketsClient::loop()`.

Wenn die bisherige Firmware deshalb ständig neu startet, die neue Firmware per USB mit `pio run -e d1_mini --target upload` installieren. Kein Flash-Erase und kein Dateisystem-Upload sind nötig; die gespeicherte Konfiguration bleibt erhalten. Nach dem Start wartet der erste automatische TrueConf-Abruf mindestens 60 Sekunden ab Webserver-Start. Die Einstellungen können in dieser Zeit über HTTP geöffnet und TrueConf bei Bedarf deaktiviert werden; ein manuell gestarteter Test oder erneutes Speichern darf sofort abrufen. Die anschließenden synchronen Netzwerkabrufe können weiterhin vorübergehend die Bedienung verzögern.

Der Zähler ist standardmäßig ausgeschaltet. Er verwendet die TrueConf-Bridge-Chat-API, nicht die administrative REST-Anrufverwaltung. Die Einrichtung erfolgt vollständig unter `/settings` im Abschnitt `TrueConf-Nachrichten`:

1. `Zähler aktivieren` auswählen und den Server als DNS-Namen oder IPv4-Adresse ohne `https://` oder Pfad eintragen. Standardport für HTTPS und WSS ist `443`.
2. Den Benutzernamen des Kontos eintragen, dessen ungelesene Nachrichten gezählt werden sollen, und dessen Passwort eingeben. Konten des lokalen TrueConf-Servers einschließlich unterstützter LDAP-Konten sind möglich. Ein separates Bot-Konto zeigt seine eigenen Nachrichten, nicht die eines anderen Benutzers.
3. Den SHA1-Fingerprint des Serverzertifikats aus den Zertifikatdetails übernehmen (40 Hex-Zeichen, Doppelpunkte und Leerzeichen erlaubt). Nach einem Zertifikatwechsel muss dieser Wert aktualisiert werden. Die ausdrücklich als unsicher gekennzeichnete Checkbox erlaubt alternativ Verbindungen ohne Zertifikatsprüfung.
4. Falls die Anmeldung über eine OAuth-Anwendung erfolgen soll, deren App-ID eintragen. Die Anwendung benötigt gemäß TrueConf `version:read` und die entsprechende Benutzeranmeldung. Die Firmware führt dann den dokumentierten dreistufigen Austausch vom REST-Token zum Bridge-JWT durch. Ohne App-ID verwendet sie die direkte Bridge-Passwortanmeldung. Reine interaktive MFA-/SSO-Anmeldungen kann der ESP nicht selbst bedienen.
5. Alternativ einen vorhandenen **Bridge-JWT** eintragen. Dieser hat Vorrang vor Benutzer/Passwort und ist kein normaler REST-API-Bearer-Token. Ein abgelaufener manuell eingetragener JWT muss ersetzt oder über `Gespeicherten JWT löschen` entfernt werden, um wieder Benutzer/Passwort zu verwenden.
6. Abrufintervall zwischen `30` und `3600` Sekunden einstellen (Standard `60`), `TrueConf-Einstellungen speichern` und danach `Gespeicherte TrueConf-Verbindung testen` drücken.

Alle Einstellungen einschließlich Passwort und manuell eingetragenem JWT werden in LittleFS gespeichert und beim Neustart geladen. Leere Geheimnisfelder behalten den vorhandenen Wert; separate Checkboxen löschen Passwort beziehungsweise JWT. Die Firmware schreibt die Konfiguration zuerst vollständig in eine temporäre Datei und ersetzt anschließend die bisherige Datei. Geheimnisse erscheinen weder im HTML als Feldwerte noch in der Status-API.

Für den direkten Zugriff muss TrueConf Web Manager erreichbar sein und `/bridge/api/` sowie `/websocket/chat_bot/` bereitstellen. Nach der Anmeldung fordert die Firmware über `getChats` jeweils drei Chats an und summiert deren `unreadMessages` über alle Seiten. Bei Erfolg erscheinen `TC <Anzahl>` auf dem Display sowie Anzahl und Status auf der Webseite und unter `/api/status`. Bei Fehlern steht auf dem Display `TC --`, kein irreführender Nullwert. Es werden keine Nachrichten gesendet, Chat-Verläufe angefordert oder Anrufdaten verarbeitet. Die Chat-Liste kann allerdings die letzte Nachricht enthalten; sie wird vom JSON-Filter verworfen und nicht angezeigt oder gespeichert.

Die Anmeldung verwendet `receiveUnread: false` und `receiveSystemMessageEnvelopes: false`. Die Firmware sendet keine Bestätigungen für Chat-Ereignisse, weil diese laut TrueConf Nachrichten als gelesen markieren können. Das Zusammenspiel mit dem Lesestatus des normalen Clients muss beim ersten Test auf eurem Server geprüft werden. WebSocket-Ping/Pong ist davon unabhängig. Die Verbindung wird nach dem Zählen geschlossen; TrueConf- und Claude-TLS laufen nacheinander, um RAM zu sparen. Eine Antwort über 8 KiB, ein unvollständiger Abruf oder ein Zeitlimit ergibt einen Fehler statt einer Teilsumme. Während synchroner Netzwerkabrufe können Webseite und Display kurz pausieren.

Quellen: [TrueConf-Anmeldung und WSS](https://trueconf.com/docs/chatbot-connector/en/connect-and-auth/), [Chat-Zähler, Seitenabruf und Gelesenbestätigungen](https://trueconf.com/docs/chatbot-connector/en/chats/).

### Reset-Countdowns und Uhrzeit

Die Firmware synchronisiert ihre Uhr nach der WLAN-Verbindung über NTP. Unter `Display-Einstellungen` kann ein im eigenen Netzwerk erreichbarer Zeitserver eingestellt und gespeichert werden; Standard ist `pool.ntp.org`. Die Zeitzone der Reset-Angabe wird berücksichtigt, der Countdown ist eine Dauer. Sekundenreste werden auf die nächste Minute aufgerundet. Ohne NTP-Synchronisierung wird kein scheinbar gültiger Countdown berechnet. Die Status-API enthält dafür `clock_synced`.

- `/`: Klima-, Limit- und Gerätestatus
- `/settings`: geschützte Claude- und Gerätekonfiguration
- `/api/status`: Status als JSON, ohne Geheimnisse
- `/refresh`: manuelle Aktualisierung über die Statusseite
- `/update`: passwortgeschützter Upload einer neuen `firmware.bin`

Die Statusseite zeigt:

- Temperatur
- Luftfeuchtigkeit oder `--` beim BMP280
- Luftdruck
- freie Claude-Prozente und Reset-Zeitpunkte
- Verbindungsstatus
- lokale IP-Adresse
- WLAN-Signalstärke
- freien Heap-Speicher

Das Captive Portal läuft nur solange keine funktionierende WLAN-Konfiguration vorhanden ist. Nach erfolgreicher Verbindung wird ausdrücklich auf reinen Clientbetrieb (`WIFI_STA`) umgeschaltet. Der normale Webserver bleibt anschließend dauerhaft im Heimnetz aktiv.

### Firmware über die Webseite aktualisieren

1. Das Projekt mit `pio run` kompilieren.
2. `http://claude-display.local/update` öffnen und mit dem Admin-Zugang anmelden.
3. `.pio/build/d1_mini/firmware.bin` auswählen.
4. `Firmware installieren` drücken und das Gerät bis zum Neustart nicht ausschalten.

Die Webseite zeigt den Uploadfortschritt. Dateiendung und verfügbarer OTA-Speicher werden geprüft. Bei einem erfolgreichen Update startet der ESP8266 automatisch neu. LittleFS und damit WLAN-, Claude- und Gerätekonfiguration werden bei einem normalen Firmwareupdate nicht überschrieben. Keine `.elf`, keine LittleFS-Datei und keine Firmware für ein anderes Board hochladen.

## Wichtige Sicherheitseinschränkung

Der verwendete claude.ai-Endpunkt ist inoffiziell. `sessionKey` ist ein Anmeldecookie und wird in LittleFS gespeichert. Der ESP8266 bietet dafür keinen wirklich sicheren Hardwarespeicher. Außerdem verwendet die Firmware wegen des fehlenden gepflegten CA-Speichers und des knappen RAM derzeit `setInsecure()` für die TLS-Verbindung. Das verschlüsselt die Übertragung, prüft aber die Identität des Servers nicht zuverlässig und ist daher gegen aktive Man-in-the-Middle-Angriffe nicht sicher.

Das Gerät sollte nur in einem vertrauenswürdigen lokalen Netzwerk betrieben werden. Der Cookie darf niemals in Logs, Screenshots oder öffentliche Fehlerberichte übernommen werden. Cloudflare-Challenges kann der ESP8266 nicht lösen; in diesem Fall kann ein aktueller `cf_clearance`-Cookie helfen, ist aber nicht garantiert.

## Bauen

Voraussetzung ist eine PlatformIO-Installation. Im Projektverzeichnis:

```sh
pio run
pio run --target upload
pio device monitor
```

Die fertige Binärdatei liegt anschließend unter:

```text
.pio/build/d1_mini/firmware.bin
```
