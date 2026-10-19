# Claude Display für Wemos D1 Mini

Eigenständige Firmware für einen ESP8266/Wemos D1 Mini. Das Gerät zeigt Raumklima und die aktuellen Claude-Nutzungslimits auf einem runden GC9A01-Display an. Konfiguration und Status sind dauerhaft über eine lokale Webseite erreichbar.

## Hardware

- Wemos D1 Mini mit ESP8266
- rundes GC9A01A-SPI-Display mit 240 × 240 Pixeln
- optional: TTP223-Touchsensor für Seitenwechsel und WLAN-Reset
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
- nach 60 Sekunden WLAN-Ausfall automatisch wieder Setup-AP und Captive Portal öffnen, ohne Zugangsdaten zu löschen
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
- unter der Temperatur: aktuelle Uhrzeit als `HH:mm:ss` in Berliner Ortszeit mit automatischer Sommer-/Winterzeit; vor der NTP-Synchronisierung `--:--:--`
- Countdownformat: Tage, Stunden und Minuten (`D HH:mm`); ohne gültige Uhrzeit oder Reset-Angabe `-- --:--`, nach Ablauf `0 00:00`
- kein zusätzlicher Text „frei“; optional `TC 5` für fünf ungelesene TrueConf-Nachrichten
- alle Textflächen liegen mit mindestens zwei Pixeln Abstand innerhalb der inneren Ringkante; lange Texte werden verkleinert beziehungsweise auf ihre Textfläche begrenzt

### Bedienung des optionalen TTP223

Version `2026-10-19.6` behebt einen Pinmux-Konflikt auf D6/GPIO12: `display.init()` ruft beim ESP8266 `SPI.begin()` auf, das auch den unbenutzten MISO-Pin D6 auf SPI umschaltet. Deshalb wird D6 erst **nach** der Displayinitialisierung mit `INPUT_PULLUP` wieder als GPIO-Eingang eingerichtet. Das Display ist nur über MOSI angeschlossen; Display-MISO darf nicht zusätzlich mit D6 verbunden werden.

Zur Diagnose erscheinen im seriellen Monitor `Touch: LOW erkannt, Sensor bereit`, bei Berührung `Touch: gedrueckt (HIGH)` und beim Loslassen `Touch: losgelassen (LOW)`. Unter `/api/status` zeigen `touch.raw_high`, `touch.ready` und `touch.pressed` den Eingangszustand sowie `display_page` die ausgewählte Seite. Leuchtet nur die Sensor-LED, ohne dass `raw_high` zwischen Ruhe und Berührung wechselt, OUT/SIG → D6, gemeinsame Masse und Versorgung mit 3,3 V prüfen. Für die Rohsignalprüfung `/api/status` während der Berührung neu laden und vor Ablauf von fünf Sekunden wieder loslassen.

Der TTP223 muss im Modus „aktiv HIGH, Tasterbetrieb“ arbeiten. Nach dem Start wartet die Firmware auf einen stabilen LOW-Ruhezustand und erkennt anschließend Berührungen fortlaufend. Ein HIGH während der Einschaltkalibrierung deaktiviert den Eingang nicht mehr dauerhaft. Seitenwechsel werden im seriellen Monitor als `Touch: Wetterseite` beziehungsweise `Touch: Claude-Seite` protokolliert. Während synchroner Netzwerkabrufe kann die Erkennung verzögert sein; sehr kurze Berührungen können dabei übersehen werden.

- falls angeschlossen, kurz berühren: zwischen Claude-Anzeige und Wetterseite wechseln (350 ms Wartezeit zur Doppeltap-Erkennung)
- zweimal kurz berühren und loslassen, innerhalb von 350 ms: Statusseite öffnen
- auf der Statusseite einmal kurz berühren und loslassen: sofort zur zuletzt geöffneten Claude- oder Wetterseite zurückkehren
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

Falls ein TTP223 angeschlossen ist, wechselt eine kurze Berührung beim Loslassen zwischen Claude- und Wetterseite. Eine Berührung von mindestens fünf Sekunden löscht die gespeicherten WLAN-Zugangsdaten und startet das Gerät neu. Da danach keine WLAN-Konfiguration vorhanden ist, öffnet sich automatisch das Captive Portal zur Auswahl eines neuen WLANs. Claude-Konfiguration und Anzeigeeinstellungen bleiben erhalten.

## Webseite

Die aktuelle Firmware zeigt auf jeder Webseite die Version `2026-10-20.7`. Fehlen die neuen Buttons, zuerst die neu kompilierte `.pio/build/d1_mini/firmware.bin` unter `/update` installieren und danach `/settings` neu laden. Änderungen am Quellcode allein aktualisieren das Gerät nicht.

Die normalen Webseiten zeigen außerdem Datum und aktuelle Geräteuhrzeit in `Europe/Berlin` einschließlich automatischer Sommer-/Winterzeit. Die Anzeige läuft im Browser sekündlich weiter und gleicht sich alle zehn Sekunden mit `/api/time` ab. Grundlage ist die NTP-synchronisierte ESP-Uhr, nicht die Uhr des PCs. Vor der Zeitsynchronisierung erscheint `Uhrzeit noch nicht synchronisiert`. Die laufende Anzeige benötigt JavaScript.

Version `2026-10-17.11` behebt vorzeitig abgeschnittene Einstellungsseiten bei leeren Konfigurationswerten. Leere Servernamen, Organisations-IDs oder andere optionale Textfelder dürfen keinen leeren HTTP-Chunk auslösen, da dieser die Antwort beendet. Der TrueConf-Abschnitt zeigt deshalb auch vor der ersten Konfiguration alle Felder und beide Buttons vollständig an.

Beim Neustart erscheint nach der Displayinitialisierung die Cozy-Verbindungsanzeige mit einem sanft pulsierenden Lichtkranz in Honig, Salbei und gedämpftem Blau. Name, WLAN-Status und SSID bleiben ruhig in der Mitte stehen. Der frühere Farb-Selbsttest ist ab `2026-10-19.13` entfernt; für den normalen Betrieb ist er nicht erforderlich. Der SPI-Takt beträgt 10 MHz. Der serielle Monitor mit 115200 Baud zeigt Firmwareversion, erwartete Display-Verdrahtung und den Beginn des WLAN-Aufbaus. Diese Meldungen bestätigen den Programmablauf, aber keine erfolgreiche Kommunikation mit dem Display.

Der Display-Name wird separat gespeichert und auch während des WLAN-Verbindungsaufbaus und im Setup angezeigt. Ein vollständig schwarzes Display trotz neuer Firmware deutet auf ein Problem mit Versorgung, Hintergrundbeleuchtung, Verkabelung oder dem verwendeten Displaymodul hin. Insbesondere `BL/LED`, gemeinsame Masse sowie CLK, MOSI, DC und RST entsprechend der Pin-Tabelle prüfen.

### Claude-Verbindung prüfen

Standard ist ein Firefox-140-Profil für Windows: `Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:140.0) Gecko/20100101 Firefox/140.0`. Der bisherige Chrome-130-Standard wird beim Laden automatisch ersetzt; individuelle User-Agents bleiben erhalten. Mit `Firefox unter Windows einsetzen` kann die Vorgabe im Webformular eingesetzt und anschließend gespeichert werden. Beide Claude-Anfragen senden außerdem `Accept-Language` und die Fetch-Metadaten `Sec-Fetch-Dest: empty`, `Sec-Fetch-Mode: cors`, `Sec-Fetch-Site: same-origin`.

Diese Header ändern weder den TLS-Client des ESP8266 noch führen sie Browser-JavaScript aus. Eine Freigabe durch Cloudflare ist damit nicht garantiert. HTTP/1.0 und unkomprimierte Antworten bleiben für die speichersparende JSON-Verarbeitung erforderlich. Bei übernommenen Browser-Cookies den tatsächlichen User-Agent dieses Browsers verwenden. Das Format des Firefox-User-Agents beschreibt [Mozilla](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/User-Agent/Firefox).

Beim Vergleich mit `claudeusage.ps1` dieselbe Organisations-ID, dieselben zusätzlichen Cookies und denselben User-Agent verwenden; der Session-Key allein bildet nicht zwingend die vollständige Konfiguration ab. Die Firmware entfernt wie das Skript äußere Leerzeichen und das optionale Präfix `sessionKey=`. Geheimnisse werden nicht in Fehlermeldungen ausgegeben.

Seit Version `2026-10-17.6` extrahiert die Firmware wie das PS1-Skript die erste UUID aus dem Organisationsfeld, etwa aus `Name [UUID]` oder einer URL. Das gilt auch für bereits gespeicherte Werte. Ungültige nichtleere Organisationsfelder werden beim Speichern abgewiesen und vor einem Abruf geprüft. Bei einem 404 zeigt die Fehlermeldung die tatsächlich verwendete UUID zum Vergleich mit dem PS1 an. Ein 404 allein beweist weder einen ungültigen Session-Key noch eine nicht existierende Organisation; der Endpunkt kann auch für die Sitzung unzugänglich sein.

Negative Fehlernummern stammen vom ESP8266-HTTPClient, nicht von Claude. `-4` bedeutet in der verwendeten Bibliothek „not connected“, bevor die HTTP-Antwort gelesen werden konnte. Seit Version `2026-10-17.4` verwenden Organisationssuche und Limit-Abruf einen TLS-Empfangspuffer von 16 KiB statt unbestätigt verkleinerter Puffer. HTTP/1.0 und `Accept-Encoding: identity` vermeiden Chunk-Framing beziehungsweise Kompression beim direkten JSON-Lesen aus dem Stream. Fehler zeigen den betroffenen Abruf, den HTTPClient-Fehlertext und bei Transportfehlern zusätzlich verfügbare TLS-Details und freien Heap. Erkannte Cloudflare-Challenges werden gesondert gemeldet.

Hintergrund: [ESP8266-TLS-Puffer und MFLN](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/bearssl-client-secure-class.html#mfln-or-maximum-fragment-length-negotiation-saving-ram), [ArduinoJson mit HTTPClient](https://arduinojson.org/v7/how-to/use-arduinojson-with-httpclient/).

### Claude-Test und TLS-Speicher

Version `2026-10-19.5` entschärft einen beobachteten Absturz beim Claude-Verbindungstest: Der dekodierte Stack aus `2026-10-19.4` zeigt `Exception (29)` mit Nulladresse in `ieee80211_setup_ratetable`, während der Claude-TLS-Handshake läuft. Die lokale Disassemblierung zeigt dort eine ungeprüfte Speicherreservierung des WLAN-SDKs vor `memcpy`. Das spricht für Speichermangel; ein erfolgreicher Hardware-Nachtest steht noch aus.

Der Button für den gespeicherten Session-Key merkt den Abruf jetzt vor und beendet zuerst die Webantwort und deren Clientverbindung. Der eigentliche Test läuft anschließend in der Hauptschleife. Ergebnis und Fehlerdetails stehen auf der Statusseite `/`. Auf der Wetterseite bleibt er bis zum Zurückwechseln vorgemerkt.

Feste Meldungstexte liegen verstärkt im Flash. Seit `2026-10-19.7` verwendet die Firmware die vom ESP8266-Core unterstützte Aufteilung **16 KiB Flash-Cache + 48 KiB IRAM mit gemeinsam genutztem zweitem Heap** (`PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED`). Die installierte BearSSL-Implementierung legt ihre I/O-Puffer bevorzugt in diesem zweiten Heap ab. Damit bleibt der volle 16-KiB-TLS-Empfangspuffer erhalten, während der normale DRAM-Heap für WLAN und andere Daten entlastet wird. Der kleinere Flash-Cache und nicht ausgerichtete IRAM-Zugriffe können die Ausführung verlangsamen; Darstellung und Verbindungen müssen am Gerät nachgeprüft werden.

Vor beiden Claude-Abrufen werden die Heaps getrennt geprüft. Der bisherige DRAM-Pfad erfordert weiterhin mindestens 28.000 Bytes frei und einen zusammenhängenden Block von 18.000 Bytes. Für den IRAM-Pfad sind mindestens 18.000 Bytes IRAM frei mit einem Block von 17.000 Bytes sowie mindestens 18.000 Bytes DRAM frei mit einem Block von 6.000 Bytes erforderlich. Diese konservativen Reserven berücksichtigen den Empfangspuffer einschließlich Protokoll- und Allokator-Overhead sowie weitere TLS-/WLAN-Daten. Bei zu wenig Speicher erscheint ein Fehlerstatus statt eines gestarteten Handshakes. Die zweite Heapgröße hängt auch vom Platzbedarf des Programmcodes ab; die Schutzprüfung verwendet deshalb Laufzeitwerte.

Die gemeldeten Werte `Heap 24264 Bytes, groesster Block 23016 Bytes` scheitern am bisherigen DRAM-Gesamtminimum, nicht an Fragmentierung. Mit ausreichend großem zweitem Heap dürfen dieselben DRAM-Werte nun einen Abruf starten. Die Schutzgrenze wird nicht pauschal abgesenkt.

Der serielle Monitor protokolliert jetzt `Claude TLS: DRAM=..., Block=...; IRAM=..., Block=...` ohne Zugangsdaten. `/api/status` liefert außerdem `max_free_block` und `heap_fragmentation` für den DRAM-Heap. Für die Nachprüfung neue Firmware installieren, Claude-Seite öffnen, den Test vormerken und danach Statusseite und seriellen Monitor prüfen. Ein Flash-Erase ist nicht erforderlich.

Grundlagen: [BearSSL-Speicherbedarf](https://arduino-esp8266.readthedocs.io/en/3.0.0/esp8266wifi/bearssl-client-secure-class.html), [IRAM und zweiter Heap](https://arduino-esp8266.readthedocs.io/en/latest/mmu.html), [vergleichbarer SDK-Absturz in der Ratentabelle](https://github.com/espressif/ESP8266_NONOS_SDK/issues/320).

### Speicher beim Seitenwechsel

Ab `2026-10-20.4` werden Texte in wiederverwendeten, nur vier Pixel hohen 16-Bit-Streifen gezeichnet. Der größte angeforderte Textbildpuffer sinkt damit von 196×28×2 = 10.976 Bytes auf höchstens 210×4×2 = 1.680 Bytes (jeweils ohne kleinen Bibliotheks-/Allokator-Overhead). Schrift, Zentrierung und Farben bleiben gleich. Der letzte Teilstreifen wird auf die tatsächliche Resthöhe beschnitten; Die Zeichenstreifen werden weiterhin auf den gültigen Displaybereich beschnitten. Ohne verfügbaren Sprite-Speicher bleibt der bisherige direkte Zeichenpfad erhalten.

Claude filtert und speichert nur noch die tatsächlich genutzten `five_hour`- und `seven_day`-Limits. Die ungenutzten Opus-/Sonnet-Limitobjekte und ihre Resetstrings entfallen; auch der temporäre JSON-Baum wird dadurch kleiner. Der volle TLS-Empfangspuffer bleibt unverändert.

Ab `2026-10-20.2` teilen sich Claude-, Wetter-, Status- und WLAN-Verbindungsanzeige einen Textcache mit zehn Zeilenplätzen. Er speichert nur die zuletzt gezeichneten Texte und Farben der aktuell dargestellten Anzeige. Beim Wechsel wird jeder alte Zeilenzustand zerstört und neu angelegt, sodass auch reservierter Stringspeicher freigegeben wird. Der neue Bildschirm wird vollständig gezeichnet; unveränderte Zeilen derselben Anzeige behalten weiterhin ihren Cache. Auch der automatische Übergang zwischen WLAN-Animation und normaler Anzeige wechselt den Cache.

Die letzten Mess- und API-Werte, Einstellungen sowie kleine Navigations- und Ringanimationszustände bleiben erhalten. Sie ermöglichen die Rückkehr ohne erneuten Abruf. Es gibt keinen vollständigen Bildschirmpuffer. Textzeilen-Sprites werden nach dem Zeichnen gelöscht; der Wetteranimationspuffer wird beim Verlassen der Wetterseite freigegeben. Der Programmcode liegt weiterhin im Flash beziehungsweise im zeitkritischen IRAM und wird nicht seitenweise geladen.

### Gemütliche Claude-Anzeige

Ab `2026-10-19.9` passen die Claude- und Wetterseite farblich zusammen: Der äußere Sessionring ist honigfarben, der innere Wochenring salbeifarben. Verbrauchte Anteile bleiben ab `2026-10-19.11` bis einschließlich 50 % Verbrauch grau. Von 50 bis 75 % geht die Spur stufenlos in Cremegelb über, von 75 bis 100 % weiter in Cremerot. Erst bei 100 % ist Cremerot vollständig erreicht. Bei neuen Werten folgt die Spurfarbe der sanften Ringbewegung. Ohne gültige Limitdaten bleibt die Spur grau. Ab 75 % Verbrauch gehen die verbleibenden Anteile sanft in Apricot über. Name und die größere Uhr (`HH:MM`) erscheinen cremefarben, Sensortemperatur und Reset-Countdowns zurückhaltender. Session und Woche erhalten jeweils eine eigene kleine Countdownzeile.

Neue Limitwerte bewegen die Ringe schrittweise zum Ziel (höchstens 25 Aktualisierungen/s); bei späteren Seitenwechseln erscheinen die gespeicherten Werte sofort. Beim ersten Anzeigen gültiger Werte nach dem Start füllt sich jeder Ring dagegen über 5 Sekunden mit sanftem Anlauf und Ausklang bis zum tatsächlichen Restkontingent. Ein Seitenwechsel pausiert diese erste Animation; beim Zurückkehren läuft sie weiter. Die Prozent- und Countdowntexte zeigen bereits die tatsächlichen Werte. TrueConf- und Wetter-Hintergrundabfragen warten während dieser ersten Füllanimation. Der frühere Rot-Grün-Blau-Weiß-Selbsttest samt zwei Sekunden Wartezeit entfällt ab `2026-10-19.13`; nach der Displayinitialisierung erscheint direkt die Verbindungsanzeige. Im Ruhezustand werden unveränderte Ringe nicht neu gezeichnet. Es wird kein zusätzlicher Bildpuffer angelegt. Bei deaktiviertem TrueConf zeigt die untere Statuszeile `Verbunden` oder `Claude pruefen`, sonst weiterhin den TC-Zähler. Die optionale IP-Adresse steht darunter ohne Präfix, damit sie vollständig in den Kreis passt. Ausführliche Fehlermeldungen stehen auf der Webseite.

### Direkter Seitenwechsel

Ab `2026-10-20.7` ist die kreisförmige Seitenblende entfernt. Beim Tap beziehungsweise Doppeltap wird die Zielseite einmal direkt gezeichnet, einschließlich der Rückkehr von der Statusseite. Die erste Ringfüllung über fünf Sekunden, kurze Übergänge bei neuen Limitwerten, Wetteranimationen und die pulsierende WLAN-Verbindungsanzeige bleiben erhalten. Der gemeinsame Textcache und die kleinen Textstreifenpuffer bleiben aktiv. Zusätzliche Blendenzustände, Layoutverweise im Textcache, der Ringpixel-Zeilenpuffer und die spezielle Blendenberechnung entfallen.

### Statusseite per Doppeltap

Ab `2026-10-19.12` öffnen zwei kurze Berührungen mit Loslassen innerhalb von 350 ms eine zusätzliche Statusseite. Der erste Tap wechselt die normale Seite erst nach Ablauf dieses Zeitfensters; dadurch bleibt beim Doppeltap die richtige Ausgangsseite erhalten. Ein einfacher Tap auf der Statusseite führt beim Loslassen direkt zur zuletzt angezeigten Claude- oder Wetterseite zurück. Der lange Touch für den WLAN-Reset bleibt erhalten.

Die Statusseite verwendet den runden Cozy-Rahmen mit Creme-, Honig-, Salbei- und Blautönen. Sie zeigt SSID, RSSI in dBm mit einem groben Prozentwert, IP-Adresse, WLAN-Kanal, Uhrsynchronisierung, freien RAM und größten zusammenhängenden Block, Laufzeit sowie Firmwareversion. Lange SSIDs werden an die verfügbare Kreisbreite angepasst. Im Setup-AP erscheinen dessen Name und IP-Adresse. Die Werte werden einmal pro Sekunde geprüft; nur geänderte Textzeilen werden neu gezeichnet. Der Signal-Prozentwert ist eine grobe Umrechnung von -100 bis -50 dBm, keine gemessene Datenrate.

Automatische Claude-, TrueConf- und Wetterabfragen pausieren auf der Statusseite. Vorgemerkte Claude-/TrueConf-Abrufe bleiben erhalten und werden nach Rückkehr zur Claude-Seite wieder freigegeben. Auch während des Doppeltap-Zeitfensters und einer gehaltenen Berührung starten keine automatischen API-Abfragen. Bereits laufende synchrone Netzwerkaufrufe können die Touch-Erkennung weiterhin verzögern. `/api/status` meldet die Seite als `display_page: "status"`.

### Wetterseite einrichten

Die Wetterseite verwendet ab `2026-10-19.8` eine warme Farbpalette mit cremefarbenen Werten, honigfarbenen Akzenten, einem dezenten runden Rahmen und ruhigen Animationen. Eine sanft leuchtende Sonne, schattierte Wolken und gedämpfte Blau-/Salbeitöne gestalten die Anzeige gemütlicher. Rahmen und Akzente werden nur beim vollständigen Seitenaufbau gezeichnet; der vorhandene kleine 8-Bit-Sprite bleibt unverändert groß.

Unter `/settings` im Abschnitt **Wetter-Standort** Ortsname, Breitengrad und Längengrad eintragen (Dezimalpunkt, beispielsweise Berlin: `52.52`, `13.405`) und speichern. Die Koordinaten legen den tatsächlichen Abfrageort fest; der Ortsname ist die Displaybeschriftung. Der Standort bleibt nach einem Neustart erhalten. Alle drei Felder leeren deaktiviert den Abruf.

Ein kurzer Touch auf den TTP223 wechselt zwischen der bisherigen Claude-Anzeige und der Wetterseite. Diese zeigt Außentemperatur, Wetterlage, relative Luftfeuchtigkeit und Windgeschwindigkeit in km/h. Sonne, Wolken, Nebel, Regen, Schnee und Gewitter werden passend zum Wettercode in einem eigenen 64×44-Pixel-Bereich animiert (Ziel: 25 Bilder/s). Das Symbol verwendet einen wiederverwendeten 8-Bit-Sprite; beim Verlassen der Wetterseite wird dessen Speicher freigegeben. Auf der Wetterseite sind Claude- und TrueConf-Abfragen pausiert, einschließlich manuell angeforderter Abrufe. Beim Zurückwechseln werden fällige oder vorgemerkte Abrufe automatisch ausgeführt; gespeicherte Werte bleiben erhalten. Die Wetterabfrage selbst kann die Animation weiterhin kurz unterbrechen. Die lokale Sensortemperatur bleibt auf der Claude-Seite. Fünf Sekunden Berührung lösen weiterhin den WLAN-Reset aus. Manuelle Claude-Aktualisierung bleibt über die Webseite möglich.

Die Firmware ruft die [Open-Meteo Forecast API](https://open-meteo.com/en/docs) ohne API-Schlüssel nach dem Start beziehungsweise Speichern und anschließend alle 15 Minuten ab. Bei Fehlern bleiben die letzten gültigen Werte mit Fehlerhinweis sichtbar; ohne gespeicherte Werte erscheinen Platzhalter. Ohne WLAN erscheint ein Hinweis. Wetterstatus und Werte sind zusätzlich unter `/api/status` verfügbar. Die synchronen HTTPS-Abfragen können die Bedienung kurz verzögern. Wie beim Claude-Abruf wird derzeit kein Serverzertifikat geprüft.

Wetterdaten: [Open-Meteo](https://open-meteo.com/), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Die kostenlose API ist für nichtkommerzielle Nutzung vorgesehen.

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

### Pulsierende WLAN-Verbindungsanzeige

Ab `2026-10-20.1` wird der erste WLAN-Verbindungsaufbau mit `WiFi.begin()` asynchron gestartet. Ein pulsierender Halo läuft mit höchstens 20 Aktualisierungen/s um den Textbereich; seine Farbe wechselt langsam zwischen Honig, Salbei und Blau. Die Animation zeichnet nur den äußeren Ringbereich und verwendet keinen zusätzlichen Bildpuffer. Auch bei späterem WLAN-Ausfall erscheint der Halo. Bei erfolgreicher Verbindung wird wieder die zuvor ausgewählte normale Seite angezeigt. Die Statusseite bleibt per Doppeltap erreichbar.

Ohne gespeicherte SSID startet das Setup-Portal sofort, sonst nach 20 Sekunden erfolglosem Startversuch. Im Portal zeigt die Verbindungsanzeige `ClaudeDisplay-Setup` und `AP 192.168.4.1`. Der normale AP-Rückfall nach einem späteren Verbindungsausfall bleibt bei 60 Sekunden. Während der Verbindungssuche starten keine automatischen Claude-/TrueConf-Anfragen. Das Portal läuft parallel zur Animation; das Speichern neuer WLAN-Daten im WiFiManager kann wegen dessen internem synchronen Verbindungsversuch weiterhin vorübergehend pausieren.

### Automatischer AP-Rückfall bei WLAN-Ausfall

Version `2026-10-17.16` behebt einen Absturz direkt nach erfolgreicher WLAN-Anmeldung: Ein WiFiManager ohne tatsächlich gestartetes Portal besitzt keinen Portal-Webserver. Die Firmware prüft deshalb Portalzustand und Server vor `process()` und `stopConfigPortal()`. Bei einer Neustartschleife der älteren Version die neue Firmware per USB mit `pio run -e d1_mini --target upload` installieren; ein Löschen des Flashs oder Dateisystems ist nicht erforderlich.

Ist das gespeicherte WLAN beim Start nicht erreichbar, öffnet das Gerät nach dem Verbindungsversuch das Setup-WLAN `ClaudeDisplay-Setup`. Fällt eine bestehende WLAN-Verbindung später aus, versucht es weiterhin alle 15 Sekunden eine Wiederverbindung und öffnet nach 60 Sekunden ununterbrochener Trennung ebenfalls das Setup-WLAN. Das Passwort lautet `claude-display`; das Portal ist unter `http://192.168.4.1/` erreichbar. Auf dem Display erscheint `AP 192.168.4.1`.

Das Portal läuft im Hintergrund ohne automatischen Zeitablauf. Die bisherigen WLAN-Zugangsdaten sowie Claude-, TrueConf- und Display-Einstellungen bleiben erhalten. Man kann im Portal ein anderes WLAN auswählen oder warten, bis das bisherige wieder erreichbar ist. Sobald eine Verbindung besteht, beendet die Firmware das Portal, schaltet den AP ab und startet das normale Webinterface erneut. Portal und normaler Webserver verwenden Port 80 nacheinander; die normale Status- und Einstellungsseite ist während des Setup-Portals nicht verfügbar. Sensoren und Display werden während des Portals weiter bedient.

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

### Automatischer GitHub-Build und Downloads

Der Workflow [Firmware bauen und veröffentlichen](.github/workflows/firmware.yml) baut die Umgebung `d1_mini` bei jedem Push, Pull Request und bei manuellem Start über **Actions → Firmware bauen und veröffentlichen → Run workflow**. Nach dem Build laufen die nativen Regressionstests. Erfolgreiche Läufe stellen für 30 Tage ein ZIP-Artefakt `firmware-d1-mini-<Commit-SHA>` mit folgenden Dateien bereit:

- `firmware-d1-mini.bin`: Firmware für das Update über `/update` oder zum Flashen des D1 mini
- `firmware-d1-mini.elf`: Symbole zur Analyse von Absturzadressen; nicht als Firmware hochladen
- `manifest.json`: Firmwareversion, Quellcode-Commit, optionaler Tag, Board und SHA256-Prüfsummen
- `SHA256SUMS`: Prüfsummen für die heruntergeladenen Dateien

Downloads: [GitHub Actions](https://github.com/DrGanmon/claudedisplay/actions/workflows/firmware.yml). Beim Artefakt-Download muss man gegebenenfalls bei GitHub angemeldet sein; ZIP entpacken und nur die `.bin` im Geräte-Webinterface auswählen.

Ein gepushter Versions-Tag mit Präfix `v` erstellt nach erfolgreichem Build und Tests automatisch ein [GitHub Release](https://github.com/DrGanmon/claudedisplay/releases) mit denselben Dateien als einzelne Downloads. Der Tag muss exakt zur Konstante `FIRMWARE_VERSION` in `src/main.cpp` passen. Beispiel für den aktuellen Stand, nachdem alle gewünschten Änderungen committed und gepusht wurden:

```sh
git tag v2026-10-20.7
git push origin v2026-10-20.7
```

Der Workflow verwendet das automatisch von GitHub bereitgestellte `GITHUB_TOKEN`. Ein persönlicher Token ist nicht erforderlich; nur der Release-Job erhält Schreibrechte auf Repository-Inhalte. GitHub Actions muss im Repository erlaubt sein. Normale Branch-Pushes und Pull Requests erzeugen Download-Artefakte, aber kein Release. Der Workflow wird auf GitHub erst aktiv, nachdem die neue Workflowdatei gepusht wurde. Lokal vorbereitet bedeutet noch keinen erfolgten GitHub-Build oder veröffentlichten Release.

Es wird ausschließlich die aktuell konfigurierte D1-mini-Firmware gebaut. Lokale Gerätekonfiguration, LittleFS-Abbilder und Zugangsdaten gehören nicht zum Downloadpaket. Hardwaretests auf dem ESP ersetzt der GitHub-Build nicht.

Grundlagen: [PlatformIO in GitHub Actions](https://docs.platformio.org/en/latest/integration/ci/github-actions.html), [GitHub-Artefakte](https://github.com/actions/upload-artifact), [GitHub-CLI-Releases](https://cli.github.com/manual/gh_release_create).

### Lokal kompilieren

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

Nach einem Build können die CI-Prüfungen lokal mit `python3 scripts/run_checks.py` ausgeführt werden (benötigt `g++`). `python3 scripts/package_firmware.py` erzeugt das Downloadpaket im ignorierten Verzeichnis `dist/`; das Zielverzeichnis muss leer sein. Ein anderes Ziel ist über `--output <Verzeichnis>` möglich.
