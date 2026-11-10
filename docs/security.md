# Sicherheit: Bedrohungsmodell

Stand: 10. November 2026, Host 0.6.0. Was der Host im Netz anbietet, wem er was zutraut, und was er bewusst nicht schützt.
Alles hier ist aus dem Code gelesen (Fundstellen in Klammern); gemessen auf der Konsole ist davon nichts. Wie man eine
Schwachstelle meldet, steht in [`SECURITY.md`](../SECURITY.md).

## Ausgangslage

Der Host ist ein Payload auf einer gejailbreakten PS5. Er läuft mit den Rechten, die der ELF-Loader ihm gibt, und jeder
andere Payload auf der Konsole hat dieselben. Daraus folgt die Grenze des Modells:

- **Wer auf der Konsole Code ausführt, ist nicht Gegner, sondern Besitzer.** Der ELF-Loader (TCP 9021) nimmt aus dem
  ganzen LAN jedes ELF an und führt es aus. Wer das LAN erreicht, kann also ohnehin alles auf der Konsole; der Host
  schützt nichts davor und kann es nicht. Was er schützt, ist das, was **über seine eigenen Ports** und über das
  **Internet** erreichbar ist, und die Grenzen zwischen gekoppelten Geräten.
- **Das LAN ist halb vertraut.** Der Host nimmt an, dass im LAN niemand Pakete fälscht oder mitliest, dem der Besitzer
  nicht ohnehin den ELF-Loader überlässt. Deshalb ist der Stream im LAN standardmäßig unverschlüsselt, und die Web-Seite
  antwortet dem LAN (mit Login).
- **Tailscale zählt wie LAN.** Sunshine rechnet `100.64.0.0/10` zum LAN (`src/network.cpp` im Sunshine-Baum,
  `lan_ips_v4`). Jedes Gerät im Tailnet, auch ein geteiltes, wird wie ein Gerät zu Hause behandelt. ps5-tailscale leitet
  die Ports auf der Konsole selbst weiter; Geräte im Tailnet erscheinen dem Host dann als `127.0.0.1`
  (`src/web/web_server.cpp`, `forwarded`).
- **Das Internet ist feindlich.** Mit UPnP (`upnp = 1`) oder einer eigenen Portweiterleitung erreicht jeder im Internet
  die sechs Stream-Ports.

## Ports

Basisport 47989 (in der App „Base port“; alle Sunshine-Ports verschieben sich mit ihm, `net::map_port`).

| Port | Was | Bindet an | Erreichbar | Ohne Anmeldung | Anmeldung |
|---|---|---|---|---|---|
| TCP 47989 | Sunshine HTTP (`/serverinfo`, `/pair`) | alle Adressen | LAN; Internet mit UPnP | `serverinfo`: Name, `uniqueid`, Version, LAN-Adresse der Konsole, Codecs, laufendes Spiel (als App-ID), frei/belegt. `/pair`: Kopplungsanfrage stellen | keine; Kopplung braucht die PIN, die der Besitzer auf der Konsole eingibt |
| TCP 47984 | Sunshine HTTPS (`/serverinfo`, `/applist`, `/appasset`, `/launch`, `/resume`, `/cancel`, `/pair`) | alle Adressen | LAN; Internet mit UPnP | TLS-Handshake, danach 401 für jeden Pfad | Client-Zertifikat eines gekoppelten Geräts |
| TCP 48010 | RTSP | alle Adressen | LAN; Internet mit UPnP | nichts: ohne wartenden Start wird die Verbindung sofort geschlossen | ein Start (`/launch`, `/resume`) über HTTPS; mit aktuellem Moonlight ist RTSP mit dem Schlüssel aus diesem Start verschlüsselt (`rtspenc://`) |
| UDP 47998, 47999, 48000 | Video, Steuerkanal (ENet), Audio | alle Adressen, nur während eines Streams | LAN; Internet mit UPnP | Pakete werden einer Sitzung über Zufallswerte aus dem RTSP-Austausch zugeordnet, sonst verworfen | Steuerkanal mit AES-GCM (aktuelles Moonlight), Video und Audio je nach Verschlüsselungsmodus |
| TCP 47990 | Web-Seite (HTTPS, eigenes Zertifikat) | alle Adressen, nur mit eingerichtetem Login | LAN und Tailscale; Internet-Adressen bekommen 403 bzw. eine geschlossene Verbindung | Login-Formular und Dateien der Seite | Benutzer und Passwort (PBKDF2-SHA256, Sperre nach Fehlversuchen), Cookie `Secure; HttpOnly; SameSite=Strict`, eigener Header für POST |
| TCP 47995 | Steuer-Socket | `127.0.0.1`; mit der Datei `control_lan` alle Adressen | Konsole; mit `control_lan` das LAN | alles (siehe unten) | keine |
| TCP 47996 | Bericht für Handy oder PC | alle Adressen | LAN | nur solange die Diagnose-Seite der App offen ist, `diagnostics.txt` (bereinigt: ohne Adressen, Gerätenamen, IDs und Spieltitel) | keine |
| UDP 5353 | mDNS für Moonlights Suche | alle Adressen, Gruppe 224.0.0.251 | LAN | Antwort nur an die Multicast-Gruppe, nie zurück an den Absender | keine |
| TCP 9021 | ELF-Loader | – | LAN | **nicht Teil des Hosts**: führt jedes ELF aus | keine |

Ausgehend, ohne eigenen offenen Port: UPnP (SSDP-Suche und SOAP an den Router, `src/net/upnp_client.cpp`), in der
Debug-Fassung `remote_log` (UDP-Syslog an die Adresse aus `sunshine.conf`), und der Host selbst an den ELF-Loader
(„Restart host“, `PROBE`).

### Was UPnP weiterleitet

Genau die sechs Stream-Ports, jeweils auf denselben Port der Konsole (`src/net/upnp.hpp`, `stream_mappings`):
TCP 47984, 47989, 48010 und UDP 47998, 47999, 48000. Nie die Web-Seite, den Steuer-Socket oder den Bericht. Die
Weiterleitung gilt eine Stunde und wird alle zwei Minuten erneuert; beim Beenden nimmt der Host sie wieder weg (höchstens
3 s Wartezeit). Ein Router, der nur dauerhafte Weiterleitungen kennt, behält sie, bis der Host sie wegnimmt: Stürzt der
Host ab oder geht der Strom weg, bleiben sie dort, auch wenn UPnP danach ausgeschaltet wird. Eine Weiterleitung von Hand
wirkt genauso wie UPnP.

Was eine Weiterleitung **ändert**: Fremde im Internet erreichen dann `serverinfo`, die Kopplung und die
TLS-/RTSP-/ENet-Parser von Sunshine, OpenSSL und Simple-Web-Server. Ein Fremder kann ohne die PIN, die der Besitzer
eingibt, kein Gerät koppeln und ohne gekoppeltes Zertifikat keinen Stream starten. Er kann aber Kopplungsanfragen
stellen, die als Karte in der App und auf der Web-Seite erscheinen, und den Host mit Anfragen belasten.

Was eine Weiterleitung **nicht** ändert: Web-Seite, Steuer-Socket und Bericht bleiben im LAN.

**Vorsicht bei Routern, die die Absenderadresse umschreiben.** Manche Router (und Reverse-Proxys) leiten Verbindungen
aus dem Internet mit ihrer eigenen LAN-Adresse als Absender weiter. Dann hält der Host Fremde für LAN-Geräte: Für sie
gilt die LAN-Verschlüsselung (Standard aus), und wäre die Web-Seite weitergeleitet, antwortete sie ihnen. Auf dem Host
sieht man das daran, dass Kopplungsanfragen und Streams aus dem Internet die Adresse des Routers zeigen.

## Kopplung

Moonlight zeigt eine vierstellige PIN, der Besitzer tippt sie in der App oder auf der Web-Seite ein (`PIN` am
Steuer-Socket, `nvhttp::pin`). Ein Fremder kann eine Anfrage stellen, aber nicht selbst eine PIN einreichen; es gibt
keinen Weg, die PIN von außen zu raten. Eine falsche PIN beendet die Anfrage. Der Austausch prüft die Reihenfolge der
Schritte und nimmt das Zertifikat erst nach dem letzten Schritt auf (die Sunshine-Lücken CVE-2024-51738 und
CVE-2024-45407 sind in diesem Stand behoben, `nvhttp.cpp` `getservercert` bis `clientpairingsecret`).

Grenzen, die im Protokoll liegen:

- **Die Kopplung läuft über unverschlüsseltes HTTP** und ist nur so stark wie die vierstellige PIN. Wer sich bei einer
  Kopplung über das Internet aktiv zwischen Moonlight und Konsole schaltet, kann die PIN nachträglich durchprobieren.
  Darum: **nur im LAN oder über Tailscale koppeln.**
- Offene Anfragen gelten fünf Minuten, höchstens 32 gleichzeitig (`MAX_PENDING_PAIRING_SESSIONS`). Fremde können damit
  die Liste füllen und eine echte Kopplung stören.
- Die App und die Web-Seite zeigen bei jeder Anfrage die Adresse, von der sie kommt, und den Gerätenamen, den der Client
  frei wählt. Ein Name beweist nichts; eine unbekannte Adresse ist ein Grund, abzulehnen („Deny“).

## Gekoppelte Geräte

Ein gekoppeltes Gerät darf streamen. Der Besitzer kann es einschränken: welche Konsolen-Profile seine Controller nutzen
(`src/control/device_profiles.hpp`), welche Apps es sieht und starten darf (`src/apps/app_access.hpp`, Patch 0023), ob
es „PS5 Screen“ und die Menüs der Konsole erreicht (`src/control/menu_access.hpp`). Diese Grenzen hängen am
Client-Zertifikat, mit dem der Start (`/launch`, `/resume`) kam, und gelten für alle Wege, die ein Gerät danach nimmt:
Liste, Start, Beitritt zu einem laufenden Stream, Controller, Tastatur und Maus. Eingaben, die keinem Stream zugeordnet
sind, darf der Host nur loslassen, nie drücken (Patch 0022).

Was ein gekoppeltes Gerät immer kann: sehen, dass die Konsole läuft und ob gestreamt wird, und seinen eigenen Stream mit
„Quit“ beenden. Mit `close_game_on_quit = 1` schließt „Quit“ auch das Spiel, das der letzte Stream gestartet oder
vorgefunden hat. Ein Gerät, dem der Besitzer nicht traut, sollte deshalb nicht gekoppelt bleiben, wenn diese Einstellung an
ist. „Unpair“ in der App oder auf der Web-Seite beendet seinen laufenden Stream und sperrt es aus.

## Verschlüsselung des Streams

Zwei Einstellungen, je nachdem, ob die Adresse des Clients privat ist (LAN, Tailscale) oder nicht (Internet):
`lan_encryption_mode` (Standard 0, aus) und `wan_encryption_mode` (Standard 1, „Optional“). Werte: 0 nie, 1 wenn der
Client es will, 2 immer, Clients ohne Verschlüsselung werden abgewiesen (`config.cpp`, `rtsp.cpp` `cmd_describe` und
`cmd_announce`, `nvhttp.cpp` `launch`).

| Teil | Immer geschützt (aktuelles Moonlight) | Nur mit Modus 1 und Wunsch des Clients, oder Modus 2 |
|---|---|---|
| HTTPS-Befehle (`/launch` usw.) | TLS mit gegenseitigen Zertifikaten | – |
| RTSP-Aushandlung | AES-GCM mit dem Schlüssel aus `/launch` | – |
| Steuerkanal: Eingaben, Rumble, Lichtleiste | AES-GCM | – |
| Video | – | AES-GCM |
| Audio | – | AES-GCM |

Bei Modus 1 schaltet Moonlight Video nur ein, wenn der Nutzer es dort will (`SdpGenerator.c` in moonlight-common-c); erst
Modus 2 verlangt es vom Client. Unverschlüsseltes Video und Audio kann jeder mitlesen, der den Weg der Pakete sieht
(öffentliches WLAN, Provider). Eingaben sind mit aktuellem Moonlight in jedem Modus geschützt; sehr alte Clients ohne
die neue Steuerkanal-Verschlüsselung senden sie offen. Über Tailscale verschlüsselt WireGuard ohnehin alles.

## Web-Seite

Siehe README, „Web page“. Sie ist aus, solange kein Login eingerichtet ist, und antwortet nur LAN- und
Tailscale-Adressen. Sie schützt das, was sonst nur die App auf der Konsole kann: Kopplung, Geräte, Profile, Apps,
Einstellungen, Neustart des Hosts. Wer sie im LAN erreicht und das Passwort nicht kennt, kommt nicht weiter; wer im LAN
ist, hat aber den ELF-Loader (siehe oben). Das Login schützt also vor allem vor Mitbewohnern und Gästen ohne Wissen um
den Jailbreak, und davor, dass eine fremde Web-Seite im Browser des Besitzers Befehle schickt.

TLS ab Version 1.2 mit den Standard-Ciphers von OpenSSL 3.5 (`server_https.hpp` in Simple-Web-Server). Das Zertifikat ist
selbst signiert (P-256, 825 Tage) und wird beim Start der Seite erneuert, wenn es in weniger als 30 Tagen abläuft oder
eine Adresse der Konsole fehlt (`src/web/web_cert.cpp`); danach warnt der Browser einmal neu.

## Steuer-Socket und `control_lan`

Der Steuer-Socket hat keine Anmeldung. Er hört nur auf `127.0.0.1`: Die App auf der Konsole und andere Payloads
erreichen ihn, das Netz nicht. Die leere Datei `/data/prosperosunshine/control_lan` öffnet ihn dem ganzen LAN, für die
Entwicklung. Dann kann jedes Gerät im LAN unter anderem eigene Geräte koppeln (`PIN`), Geräte entfernen, Profile setzen,
das Login der Web-Seite neu setzen (`WEBLOGIN SET`) und den Host beenden (`QUIT`). `FILE` und `INSTALL` bleiben der
Konsole selbst vorbehalten (`src/control/file_bridge.hpp`). Ein Payload, der Verbindungen auf der Konsole an
`127.0.0.1:47995` weiterleitet (etwa ein Tunnel), macht den Socket für dessen Gegenseite zu einem lokalen, mit `FILE` und
`INSTALL`: Diesen Port nie weiterleiten.

## Dateien

In `/data/prosperosunshine/`:

| Datei | Inhalt | Geheim? | Rechte |
|---|---|---|---|
| `credentials/cakey.pem` | privater Schlüssel des Hosts (Kopplung, HTTPS) | ja | 0600 (Sunshine, `httpcommon.cpp`) |
| `credentials/cacert.pem` | Zertifikat des Hosts | nein | 0644 |
| `sunshine_state.json` | gekoppelte Geräte mit ihren Zertifikaten, `uniqueid` | nein, aber wer sie ändern kann, koppelt Geräte | Standard |
| `web_login` | Benutzer, Salt und PBKDF2-Hash des Passworts | der Hash | Standard |
| `web_remembered` | Hashes der Tokens gemerkter Browser | ja | 0600 |
| `web_key.pem`, `web_cert.pem` | Schlüssel und Zertifikat der Web-Seite | der Schlüssel | Schlüssel 0600 |
| `sunshine.conf` | Einstellungen | nein | 0666, damit die App (anderer Benutzer) sie schreiben kann |

Die Rechte sind auf der Konsole fast bedeutungslos: Jeder Payload läuft mit vollen Rechten, und jede Homebrew-App kann
über den ELF-Loader einen schicken. Sie sind trotzdem so eng wie möglich, falls die Dateien kopiert oder in eine Sicherung
gelegt werden. Diese Dateien gehören nie in einen Fehlerbericht; der Bericht der App lässt sie aus.

## Härtung des Programms

Gemessen am ausgelieferten `prospero_sunshine.elf` (`readelf`, `objdump`):

- **PIE**: ja (das SDK baut nur positionsunabhängig). Wohin der ELF-Loader es legt, und ob zufällig, entscheidet der
  Loader; nicht geprüft.
- **Stack-Schutz**: nein. Das SDK übergibt `-fno-stack-protector` (`bin/prospero-clang`); nur die Opus-Bibliothek bringt
  eigene Prüfungen mit. `__stack_chk_fail` und `__stack_chk_guard` liefert `libkernel_web`, also würde
  `-fstack-protector-strong` funktionieren.
- **Code-Segment**: lesbar, schreibbar und ausführbar (Linker-Skript des SDK, `PHDRS` mit `FLAGS (0x7)`). Kein
  `PT_GNU_RELRO`, kein `PT_GNU_STACK`; ob der Loader den Stack ausführbar macht, ist nicht geprüft.
- **`_FORTIFY_SOURCE`**: im SDK nicht verfügbar (die Header kennen es nicht, die Bibliotheken haben keine `__*_chk`).

## Bewusst nicht geschützt

- Alles, was der ELF-Loader ohnehin erlaubt: Code auf der Konsole, Dateien in `/data`, der Steuer-Socket von der Konsole
  aus.
- Wer im LAN mitlesen oder Pakete fälschen kann, sieht einen unverschlüsselten LAN-Stream (Standard) und kann
  Kopplungen im LAN stören.
- Fremde im Internet können bei offener Weiterleitung sehen, dass hier ein Sunshine-Host läuft, wie er heißt, ob gerade
  gestreamt wird, und Kopplungsanfragen stellen. Sie können den Host mit Anfragen belasten; einen Schutz gegen
  Überlastung über die Bandbreite der Leitung hinaus gibt es nicht.
- Ein gekoppeltes Gerät ist ein Gerät, dem der Besitzer den Stream anvertraut. Die Grenzen pro Gerät verhindern Fehler
  und Neugier, sind aber kein Ersatz dafür, ein fremdes Gerät gar nicht erst zu koppeln.

## Empfehlungen für das Streamen über das Internet

1. **Am liebsten Tailscale** (README, „Without port forwarding: Tailscale“): Nichts ist im Internet offen, und alles ist
   verschlüsselt. Nur Geräte ins Tailnet lassen oder mit ihm teilen, denen man den Stream zutraut.
2. **Geräte zu Hause koppeln**, im LAN oder über Tailscale, nicht über die weitergeleiteten Ports.
3. Mit UPnP oder Weiterleitung: **„Internet encryption“ auf „Required“** (`wan_encryption_mode = 2`). Aktuelles
   Moonlight kann das; sehr alte Clients werden dann abgewiesen.
4. **UPnP nur an, solange es gebraucht wird**, und danach im Router nachsehen, ob keine Weiterleitung zur Konsole
   übrig ist (besonders bei Routern, die nur dauerhafte Weiterleitungen kennen).
5. **Nie** die Web-Seite (47990), den Steuer-Socket (47995), den Bericht (47996) oder den ELF-Loader (9021) weiterleiten,
   keine DMZ für die Konsole einrichten, und bei IPv6 (`address_family = both`) prüfen, dass die Firewall des Routers nur
   die sechs Stream-Ports durchlässt.
6. **Kopplungsanfragen ablehnen**, die man nicht selbst gerade gestellt hat, besonders solche von fremden Adressen.
7. `control_lan` nur für die Entwicklung, in einem Netz, dem man vertraut, und danach wieder löschen.
8. Gekoppelte Geräte regelmäßig durchsehen und unbekannte entfernen; Freunden nur die Profile geben, die sie brauchen,
   und `close_game_on_quit` nur einschalten, wenn alle gekoppelten Geräte vertrauenswürdig sind.
