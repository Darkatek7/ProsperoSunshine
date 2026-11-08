# Arbeitsstand

Stand: 8. November 2026, nach Release 0.5.0. Hier steht nur, was live geprüft ist, mit dem Messweg. Was ausgeliefert ist:
`CHANGELOG.md`. Plan und Reihenfolge: `ROADMAP.md`. Architektur: `docs/adr/001-sunshine-port.md`.

Die älteren Einträge (bis 4. November 2026) und die ersten Nachweise nach Roadmap stehen unverändert und mit denselben
Überschriften in [`docs/status-archive.md`](docs/status-archive.md). Verweise auf `STATUS.md` mit einer Überschrift, die
hier fehlt (etwa in Code-Kommentaren), meinen diese Datei. „Archiv“ unten heißt ebenfalls diese Datei.

## Kurzfassung

**Der Host streamt.** Moonlight paart sich mit der PS5, startet „PS5 Screen“ oder ein installiertes Spiel und bekommt Bild,
Spielton und Controller. Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei (Archiv, „Nachweise nach
Roadmap“). Jedes Release seit 0.3.2 lief auf der Konsole mit 13.00 je 30 s in H.264 und HEVC 1080p60 und dekodierte
fehlerfrei („Release 0.5.0 auf der Konsole“ und die Einträge davor).

**Firmware:** 13.00 und 10.20 (PS5 Pro) sind geprüft. Von 9.60, 10.40, 11.40, 13.40 und 13.60 gibt es Berichte von
Nutzern und Mitwirkenden, gemessen ist dort hier nichts („Firmware-Sonde auf 9.60“, „Berichte nach 0.3.1“, „Belegter
Steuerport“; Archiv, „Berichte von Testern zu 0.1.7“).

**Clients:** Moonlight für Windows, Linux, iOS, Android und Android TV, Nintendo Switch, PS Vita und Moonlight Web haben
gestreamt, teils im LAN, teils über das Internet. Viele dieser Läufe sind Berichte des Besitzers oder eines Mitwirkenden
mit Hostlog, gemessen ist meist mit dem eigenen Testclient (`tools/moonlight/`).

**Bild:** H.264 und HEVC, HDR als HEVC Main 10 mit HDR10-Metadaten (seit 0.2.4). Auf 13.00 640x360, 960x540, 1280x720,
1080p, 1440p und 4K mit 30 oder 60 fps, bis 150 Mbit/s. Auf 10.20 bis 1080p; dort übernimmt der Encoder keine Bitrate, der
Host setzt einen Quantisierer nach der verlangten Bitrate (Archiv, „Modi unter 1080p (2026-11-02, 13.00)“, „Modi unter 1080p
auf Firmware 10.20“, „HDR“, „Quantisierer auf 10.x folgt der Bitrate“).

**Ton:** Stereo als Opus. 5.1 und 7.1 hinter dem Schalter „Surround“ („Raumklang folgt einem breiteren Stream“; Archiv,
„Für 0.3.1“).

**Eingabe:** ein virtuelles DualSense je Moonlight-Controller, mit Touch und Bewegung (nur am Testclient gemessen),
klassischem Rumble, Lichtleiste und adaptiven Triggern (Moonlight am PC). Tastatur und Maus gehen auf eine virtuelle
Tastatur und Maus der Konsole. Ein Gerät ohne Controller spielt mit Tastatur und Maus als eigenes Pad, mit Profilen je
Spiel: auf 10.20 vom Besitzer in GTA III gespielt, auf 13.00 nur am Testclient, ohne Spiel und Profile („Tastatur-Pad
nach dem Review von PR 20“,
„Release 0.3.3 auf der Konsole“; Archiv, „Tastatur und Maus“, „Tastatur und Maus als Controller“). Für Controller ohne
Touchpad sind View und Menu zusammen der Touchpad-Klick („Touchpad ohne Touchpad und „Restart host“ im Stream“).

**Mehrere Spieler:** Weitere Geräte treten einem laufenden Stream mit eigenem Pad bei. Ein Gerät mit anderem Codec,
Dynamikbereich oder einer Auflösung bis 1080p bekommt einen zweiten Aufnahmekanal (13.00, Testclient). Ein Tester spielte
auf 10.20 mit vier Spielern, auch über das Internet. Nur das Gerät des Hosts hat PS und Create, das Pad eines Gasts erscheint
erst bei seiner ersten Eingabe („Host-Rolle am Gerät“, „Gast-Pad erst bei der ersten Eingabe“; Archiv, „Zweiter Spieler
von einem anderen Gerät“, „Zweiter Aufnahmekanal im Host“).

**Spiele:** Die installierten Titel stehen in Moonlights App-Liste und starten von dort. Mit `close_game_on_quit` schließt
„Beenden“ in Moonlight das Spiel (Archiv, „Spiele in Moonlight“, „Terminate App schließt das Spiel“).

**Bedienung:** Die App auf der Konsole (Status, PIN, Geräte, Einstellungen, Autoload, Diagnose) und seit 0.4.0 eine
Webseite auf Port 47990, die erst mit einem Login angeht („Webseite“, „Webseite nach der Testrunde auf 10.20“). Mit 0.5.0
kamen Konsolenprofile pro Gerät („Profile pro Gerät, PR 33“), versteckte Apps für nicht vertraute Geräte („Apps verstecken,
Issue 36“) und ein Debug-Build in jedem Release („Debug-Build (PR 29) zusammen mit Profilen und Apps auf main“, „Debug-Build
(Issue 28, …)“). Profile und versteckte Apps sind nur mit dem Testclient auf 13.00 gemessen, der Debug-Build auf 13.00 und
10.20.

## Offen und bekannte Grenzen

1. **Menüs je Profil (PR 38):** nach 0.5.0 auf `main` gemergt, in keinem Release. Mit dem Testclient auf 13.00 gemessen
   („Konsolenmenüs, PR 38“), mit echtem Moonlight noch nicht.
2. **Echtes Moonlight fehlt** beim zweiten Aufnahmekanal, bei den Profilen pro Gerät, den versteckten Apps, der Host-Rolle
   mit mehreren Geräten und bei Touch und Bewegung. Dort lief nur der Testclient.
3. **Andere Firmware:** Nur 13.00 und 10.20 sind geprüft (Checkliste in `docs/firmware-porting.md`). 9.60 hat einen
   Datensatz aus der Sonde eines Mitwirkenden und bleibt ungeprüft; die App sieht dort `/data` nicht und geht über den Host
   („Firmware-Sonde auf 9.60“).
4. **10.x:** Der Encoder hält keine Bitrate, `SetVideoBitrate` beendet den Kanal. H.264 kommt in 34 Slices je 1080p-Bild,
   die Hardware-Decoder unter Windows nehmen höchstens 32 und zeigen kein Bild; HEVC geht („Berichte nach 0.3.1“). Modi über
   1080p bietet der Host dort nur mit dem Experimental-Schalter an.
5. **1440p und 4K im Spiel:** Bei manchen Titeln (Mario 64, Teardown) verweigert das System den großen Modus, der Stream
   läuft dann in 1080p. 120 fps gibt es nicht, `OpenVideo` lehnt das Timing-Feld ab (Archiv, „1440p und 4K, H.264“,
   „HDR-Metadaten, Slices, 120 fps, zweiter Kanal“).
6. **Raumklang:** Welcher der 12 Kanäle welcher Lautsprecher ist, ist ungeprüft. Die Konsole im Test gibt Stereo aus, dann
   tragen nur vorne links und rechts Ton. Dafür braucht es eine Konsole mit 5.1- oder 7.1-Ausgabe („Raumklang auf 13.60“).
7. **Ruhemodus:** Auf dem Startbildschirm ohne Spiel und Stream startet der Host seine Dienste danach neu (zwei Tests mit
   0.1.3). Mit offenem Spiel und laufendem Stream kam er nicht zurück, im ersten Test hing die Konsole. Moonlight kann die
   Konsole nicht wecken, Payloads stehen im Ruhemodus still (Archiv, „Stand 30. Oktober 2026“, „Wecken aus Moonlight“).
   Tests nur nach Rückfrage.
8. **Haptik und Trigger:** Nur klassisches Rumble kommt an, die Haptik des DualSense nicht. Adaptive Trigger gehen nur mit
   Moonlight am PC und bleiben nach dem Stream hart bis zum nächsten Verbinden (`docs/re/adaptive-triggers.md`).
9. **HDR:** Avcap2 liefert keine Metadaten, der Host schickt feste HDR10-Werte. Geprüft an zwei echten Aufbauten: Moonlight
   am HDR-Fernseher und Moonlight unter Windows mit HDR-Bildschirm („Berichte eines Mitwirkenden“; Archiv, „HDR“).
10. **Lange Spielsitzungen:** Ein Lauf über mehrere Stunden in einem Spiel mit ausgewertetem Log fehlt.
11. **Seltene Fehlstarts:** 1 in 86 Starts im Docker-Client, 0 in 300 im Windows-Client; die Ursache ist offen (Archiv,
    „Stand 30. Oktober 2026“).
12. **Webseite:** Hier nicht geprüft sind ein Browser am PC gegen die Konsole, PIN, „End stream“, Umbenennen und Entfernen
    über die Seite, eine Adresse außerhalb des LAN und die Seite nach echtem Ruhemodus. Die Bedienung mit Login prüfte ein
    Mitwirkender auf 10.20 und am iPhone („Release 0.4.0 auf der Konsole“).
13. **Echter DualSense an der Konsole:** Er trennt sich, wenn sich das Pad des Streams anmeldet; die Ursache liegt auf der
    Seite der Konsole (Archiv, „Stand 30. Oktober 2026“, Punkt 2 unter „Noch offen“).
14. **Store:** Die Installation aus dem ProsperoStore klappte laut Nutzer einmal (2026-10-31); spätere Versionen sind aus dem
    Store nicht eigens installiert worden.

## Neuere Nachweise

### Browser als Client über moonlight-web-stream (2026-11-08, 13.00, Host 0.5.0)

moonlight-web-stream v2.10.0 in Docker auf einem Linux-Rechner im LAN, als Moonlight-Client per PIN über `PIN` gepairt,
„PS5 Screen“ in headless Chrome geöffnet und `getStats()` der WebRTC-Verbindung alle 5 s gelesen: 30 s H.264 in
1920x1080 mit 59 bis 60 fps im Browser, 1913 Bilder dekodiert, 0 verworfen, 0 Pakete verloren, Opus-Ton dabei. Der Host
sah einen normalen Client (`STREAMS`: H.264, 1080p60, 7308 kbit/s). Nach dem Schließen des Tabs beendete moonlight-web den
Stream nach rund 25 s selbst. Am Host ist dafür nichts nötig. Nicht geprüft: ein TV-Browser, Eingabe, HEVC.

### UPnP, PR 43 (2026-11-08, 13.00)

Testhost mit PR 43 am Router des Besitzers (UPnP dafür kurz eingeschaltet), Mappings vom PC aus per SSDP und
`GetSpecificPortMappingEntry` abgefragt:
- Stand `d2c2778`: 5 von 6 Ports weitergeleitet, UDP 48000 lehnte der Router mit 729 (`ConflictWithOtherMechanisms`) ab.
  QUIT entfernte alle Mappings, der Host war nach rund 1 s beendet. Nach Ruhemodus fand der erste Durchlauf keinen Router
  („no router answered“), der nächste zwei Minuten später leitete wieder weiter.
- Stand `51b0082` (Entfernen mit 3 s Obergrenze, schnelle Wiederholung in der ersten Minute), Router-UPnP wieder aus:
  UPnP aus im Host: 20 s H.264 in 1080p60 fehlerfrei dekodiert, keine UPnP-Zeilen, QUIT rund 1 s. `upnp = 1`: QUIT 3 s
  und 85 s nach dem Start, beide Male `host stopped` in `host_diag.log` nach 0,5 s. Durchläufe etwa alle 17 s, „no router
  answered“ nach 70 s, im ersten Durchlauf nach der ersten Minute.
- Nicht geprüft: Mappings und Ruhemodus mit Stand `51b0082` (vom Mitwirkenden gegen einen nachgebauten Router und auf 10.20
  gemessen), zwei Router hintereinander.

### Konsolenmenüs, PR 38 (2026-11-08, 13.00)

Testhost mit PR 38, zwei Konsolenprofile, ein Testclient auf das zweite Profil beschränkt (`SETPROFILES`), die Menüs nur für
das erste (`SETMENUS`). Abfragen mit dem Zertifikat des Testclients:
- Stand `a4c38d16`: In `/applist` fehlte „PS5 Screen“, `/launch` von App 1 gab 403 „This app is hidden on this device“; ein
  Gerät mit freier Wahl sah es weiter. Mit den Menüs auch für das zweite Profil war es wieder da. `SETMENUS` lehnte ein
  unbekanntes Profil, eine doppelte ID und ein fehlendes Argument ab. Menüs mitten im Stream von „PS5 Screen“ entzogen: Das
  Pad wartete nach weniger als 0,4 s („its input waits“), der Stream lief weiter und endete sauber.
- Stand `959b67a` (das Flag `SceShellCoreUtilAppFocus` für die App mit dem Bildschirm): Es ließ sich auf 13.00 öffnen, die
  Warnung für den Rückfall kam nicht. GTA III vom beschränkten Gerät gestartet, 200 s, `--no-script` mit Steuerkreuz-Ticks:
  Mit dem Spiel vorne ging die Eingabe durch. PS lang auf einem zweiten Controller (über ps5-mcp): Home-
  Bildschirm, GTA lief weiter als großes Programm, das Pad wartete. ✕ auf der Kachel: Spiel wieder vorne, Eingabe ging
  durch. PS kurz (Kontrollzentrum über dem Spiel): Das Spiel blieb vorne, nichts geloggt. Stream fehlerfrei dekodiert.
- Nicht geprüft: echtes Moonlight, Tastatur und Maus eines beschränkten Geräts.

### Issue 39 auf 13.00 (2026-11-08, Testclient, nicht veröffentlicht)

Die Änderungen für 10.x (größere Modi bleiben nach einer Ablehnung am Ausgang auf Probe, der Rückfall auf 1080p bekommt den
Anteil der Bitrate, ein laufender Stream behält seine Größe) auf 13.00 nachgeprüft, wo 1440p und 4K ohne Schalter gehen:
- Stand `f98f318`: H.264 in 4K vom Home-Bildschirm, 3840x2160, fehlerfrei dekodiert, wie vor der Änderung.
- Stand `d1d70f8`: 4K vom Home-Bildschirm, 120 s, GTA III mitten im Stream gestartet und wieder geschlossen. Auf 13.00 nimmt
  das der Aufnahme nichts: kein `capture lost`, kein Wiederöffnen, 7183 Bilder in 3840x2160, fehlerfrei dekodiert. Der Weg,
  der den Stream beendet, statt die Größe zu wechseln, ist hier also nicht erreichbar; auf 10.20 prüft ihn ein Mitwirkender.

### Release 0.5.0 auf der Konsole (2026-11-07, 13.00)

App nach `/data/homebrew/PPSA47989` kopiert (`contentVersion` `01.000.024`) und neu registriert, Host-ELF nach
`/data/prosperosunshine/` und mit `host_restart.elf` neu gestartet: Kernel-Log `version=0.5.0`, Steuerport nur lokal.
Testclient, Home-Bildschirm, je 30 s 1080p60:

| Variante | H.264: Bilder, Lücken | HEVC: Bilder, Lücken |
| --- | --- | --- |
| Normal | 1793, 0 | 1792, 0 |
| Debug (`0.5.0-debug`, Selbsttest `problems=0`) | 1794, 0 | 1792, 0 |

Alle fehlerfrei dekodiert, ohne Tonfehler. Danach wieder die normale Variante über `host_restart.elf`.

### Debug-Build (PR 29) zusammen mit Profilen und Apps auf main (2026-11-07, 13.00)

Host `d19bce53` (PR 29 mit den Review-Änderungen auf main gemergt, Patches 0001 bis 0025), Testclient, Home-Bildschirm:
- Normal: `version=0.4.2`, H.264 und HEVC 1080p je 30 s mit 1792 und 1787 Bildern, keine Lücken, fehlerfrei dekodiert,
  ohne Tonfehler.
- Debug: `version=0.4.2-debug`, Selbsttest in 179 ms mit `ra=resolved by_nid=4`, `ports=free`, `mdns=ok`,
  `capture=opens … step=open only rc=0`, `problems=0`; das Kernel-Log zeigt jetzt „capture open only ok, closed again“
  statt „open failed“. `->`-Zeilen vor jedem Capture-Schritt und jedem Bind.
- Host-, Debug- und App-Build mit beiden Testsuiten grün. Nicht geprüft: die Release-Job-Änderung (nur beim nächsten Tag
  in CI sichtbar).

### Apps verstecken, Issue 36 (2026-11-07, 13.00)

Testhost auf `da426c3` mit den Änderungen für Issue 36 (Patch 0023), zwei gekoppelte Testclients, beide mit freier
Profilwahl, einer in `web_trusted.txt` eingetragen. Abfragen direkt mit dem Zertifikat des jeweiligen Clients:
- `SETAPPS PPSA99000 hidden` (ProsperoStore): `APPS` zeigt es `hidden`. Ohne vertrautes Gerät fehlte es in `/applist` bei
  beiden (36 statt 37 Einträge); mit einem vertrauten Gerät sah nur dieses es wieder.
- `/launch` mit der ID des Stores vom nicht vertrauten Client: HTTP-Status 403 „This app is hidden on this device“, nichts
  gestartet, Server blieb `SUNSHINE_SERVER_FREE`.
- Der vertraute Client startete den Store und streamte 40 s (H.264 1080p60, 2391 Bilder, fehlerfrei dekodiert). Währenddessen
  bekam der andere auf `/resume` 403 und auf `/launch` von PS5 Screen 400 („An app is already running“). Mit dem Ende
  schloss `/cancel` den Store wieder.
- `sunshine.log` nennt die Ablehnungen ohne Titel.
- Webseite: `/api/apps` listet 36 Titel; `/api/apps/hidden` versteckt den Store (danach fehlt er in `/applist` des nicht
  vertrauten Clients) und zeigt ihn wieder; eine ungültige Titel-ID und `"hidden":"yes"` gaben 400. Die Ansicht Apps vom
  Besitzer im Browser angesehen.
- Nicht geprüft: echtes Moonlight.

### Profile pro Gerät, PR 33 (2026-11-07, 13.00)

Testhost `1e8eab9d` (PR 33 auf `4a03c0b`), zwei Konsolenprofile (Besitzer und ein Testprofil), Testclient mit eigener
Kopplung (`stream_test`, ein Pad vom Typ PS ohne Touchpad, `--no-script` mit zwei Steuerkreuz-Ticks, teils Tasten):
- Beim ersten Start behielten die 12 gekoppelten Geräte die freie Wahl (`free`); `USERS`, `PROFILES`, `SETPROFILES` antworten.
- Gerät auf das Testprofil beschränkt, das an der Konsole angemeldet war: Pad nicht hinzugefügt („signed in on the console by
  someone else“). Ebenso mit dem Profil des Besitzers, das vorne angemeldet ist.
- Gerät auf `none`: Hinweis als Zuschauer beim Streamstart, kein Pad, keine Tastatur.
- Freies Gerät: Pad wie bisher als Controller des Hosts, sauber entfernt.
- Testprofil vorher an der Konsole abgemeldet: Der Host meldete es an, das Pad war nach 2 ms im Profil, ohne die Frage der
  Konsole; die Tastatur des Geräts bekam ein eigenes Gerät im Profil (`0x3000000170100`).
- Neu verbunden 10 s nach dem Ende: gleiches Profil ohne neue Anmeldung. 45 s nach dem letzten Pad abgemeldet (`rc 0`),
  keine Fehler beim Zurücksetzen der Rechte. Alle Streams fehlerfrei dekodiert.
- Nicht geprüft: Profilwechsel (nur ein Profil frei) und ein echter Controller, der über die Benutzerauswahl der Konsole
  dazukommt (vom Autor als offene Grenze genannt).

### Release 0.4.2 auf der Konsole (2026-11-07, 13.00)

App nach `/data/homebrew/PPSA47989` kopiert, Host-ELF nach `/data/prosperosunshine/` und mit `host_restart.elf` neu gestartet
(vorne lief ein Spiel, daher nicht über die App): Kernel-Log `version=0.4.2 commit=d440adfe`, Steuerport nur lokal. Das
Neuregistrieren lehnte ShadowMountPlus ab, solange das Spiel läuft (`game_active`), die App zeigt ihre neue Version also erst
nach dem nächsten Neuregistrieren. Testclient, Spiel vorne: H.264 und HEVC 1080p je 30 s mit je 1794 Bildern bei 59,94 bzw.
59,93 fps, fehlerfrei dekodiert, ohne Tonfehler. Der Fix für „PS5 Screen“ nach einem Stream (Moonlight hing bei
„Quitting PS5 Screen...“, im Host-Log kam kein `/cancel` an) vorher als Testhost mit Moonlight am PC vom Besitzer geprüft.

### Touchpad ohne Touchpad und „Restart host“ im Stream (2026-11-07, 13.00)

Moonlight für Windows meldet einen Xbox-Controller mit `type 01`, `capabilities 0043` und `supportedButtonFlags 0000F7FF`
(Debug-Log der Ankunft): kein Touchpad, keine Share-Taste (`0x200000`), auch wenn sie gedrückt wird. View und Menu zusammen
sind seit `8e1467b` der Touchpad-Klick für solche Controller; vom Besitzer mit diesem Xbox-Controller in einem Spiel
bestätigt, View allein, Menu allein und die Xbox-Taste wie zuvor.

„Restart host“ während eines Streams (Bericht aus Issue 30: auf 9.60 ging die Konsole dabei aus, mit dem Haptik-Kanal des
Forks offen) wie die App nachgestellt: Testclient streamt, vorne ein Spiel, `QUIT` an den Steuerport, dann das 0.4.1-ELF an
den Loader. Der Host beendet sich in unter 2 s sauber (Aufnahme und Ton gestoppt und geschlossen, Pad abgemeldet,
`exit_value=0`), die Konsole bleibt an, der neue Host kommt normal hoch. Der Client hatte bis dahin 1018 Bilder fehlerfrei
dekodiert; ein neuer Stream danach 20 s mit 1196 Bildern, fehlerfrei dekodiert, ohne Tonfehler. Auf 13.00 ohne Haptik-Kanal
also nicht nachstellbar; mit Moonlight am PC statt Testclient und auf 9.60 ungeprüft.

### Release 0.4.1 auf der Konsole (2026-11-07, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.022`; nach „Restart host“ meldet das Kernel-Log
`version=0.4.1 commit=cd0d808b`, und der Prozess heißt dort und in der Prozessliste `ProsperoSunshine` statt `payload.elf`
(Issue #32). Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1793 und 1792 Bildern bei 59,94 fps, fehlerfrei
dekodiert, ohne Tonfehler. `http://` auf Port 47990 bekommt 302 auf `https://` (Issue #31; vorher leere Antwort, curl
Fehler 52), ohne Host-Kopfzeile 400. Vor dem Release mit `control_lan` gemessen: Ein POST mit `STATUS` als Inhalt an den
Steuerport führte auf 0.4.0 `STATUS` aus, nach der Änderung schließt der Host die Verbindung ohne Antwort, `STATUS` und
`DEVICES` ohne HTTP antworten wie zuvor. Der Datensatz für 9.60 ändert an 13.00 nichts (Golden-Datei, Stream wie oben); auf
9.60 selbst ist er hier nicht prüfbar, ebenso der Dateizugriff der App über den Host, den nur eine Konsole ohne `/data` in
der Sandbox eines Titels nutzt.

### Debug-Build (Issue 28, 2026-11-06, 10.20, PS5 Pro, Testclient, Home-Bildschirm, nicht veröffentlicht)

Host `commit=47dc059f` in beiden Varianten, nacheinander per ELF-Loader gestartet; der installierte Host blieb unverändert.
Für die Messung war `min_log_level` aus `sunshine.conf` entfernt, damit jede Variante mit ihrer Voreinstellung läuft.

- **Debug, `host_diag.log`:** beginnt mit `version=0.4.0-debug commit=47dc059f … started=2026-11-06T16:47:…`, jede Zeile mit
  den Millisekunden seit dem Start; die Datei des vorigen Laufs liegt als `host_diag.log.1` daneben. Selbsttest in 185 bis
  191 ms: `console=PS5-Pro`, `ra=resolved by_nid=4 by_table=0`, `ports=free`, `mdns=ok`, `capture=opens` (OpenVideo ohne
  Start), `problems=0`. `->`-Zeilen vor jedem Capture-Schritt (Nachschlagen, Register, RUNNING, Initialize, OpenVideo, Start,
  OpenAudio, Stop/Close, Terminate, STOP, Unregister), vor den Binds (47984, 47989, 47990, 47995, 47996, 48010, 5353 und je
  Stream 47998 bis 48000) und vor QUIT.
- **Debug, `sunshine.log`:** Stufe Debug. Bei `/pair` stehen `clientcert` und `salt` als `<hidden>`, das Zertifikat des
  Clients als `<certificate>`, bei `/launch` `rikey` und `rikeyid` als `<hidden>`; im ganzen Log keine IPv4-Adresse (die des
  Clients als `<ip>`) und keine 32-stellige Hex-Folge. `Stream stats:` alle 5 s, einmal `Held back 2 similar lines …`. Das
  Kernel-Log blieb bei Info.
- **Rauschen:** RTSPs Zeile „No pending session for incoming RTSP connection“ kam auf Debug alle 5 s, ausgelöst von der
  Portüberwachung des Hosts. Patch 0024 stuft sie auf Verbose herab; danach kam sie nicht mehr.
- **Normal:** `version=0.4.0`, Zeitstempel, keine `->`- und keine `selfcheck`-Zeilen, `sunshine.log` auf Info.
- **Testclient** (Windows-Build, `--no-script`), H.264 und HEVC 1080p60 je 30 s:

| Variante | H.264: Bilder, fps, fehlend, längste Pause | HEVC: Bilder, fps, fehlend, längste Pause |
| --- | --- | --- |
| Debug | 1791, 60,01, 0, 86 ms | 1793, 59,96, 0, 43 ms |
| Normal | 1792, 59,95, 0, 43 ms | 1795, 59,96, 0, 142 ms |

  Alle Tonpakete ohne Dekodierfehler.

Nicht geprüft: Spiele (der Home-Bildschirm fordert den Encoder kaum, rund 0,5 Mbit/s), 13.00, das Fernlog (`remote_log`) an
der Konsole, und die Debug-App (`PPSA47989-debug.zip`, in CI gebaut) auf der Konsole.

### Release 0.4.0 auf der Konsole (2026-11-06, 13.00)

`main` mit PR 27 (Webseite) und 0.3.4 zusammengeführt, App aus `build/app` kopiert und neu registriert, sie zeigt
`v01.000.021`; nach „Restart host“ meldet das Kernel-Log `version=0.4.0 commit=a2b4231a` und „Web page on port 47990“.
Die Seite liefert `index.html` mit dem eigenen Zertifikat (`CN=ProsperoSunshine web page`, Adressen der Konsole), ohne Login
antworten `/api/overview`, `/api/settings` und `/api/logs` mit 401, ein falsches Login ebenso. Testclient, Home-Bildschirm:
H.264 und HEVC 1080p je 30 s mit 1793 und 1792 Bildern, ohne Lücke, längste Pause 20 ms, fehlerfrei dekodiert, je 5805
Tonpakete ohne Fehler. Die Bedienung der Seite mit Login prüfte ein Mitwirkender auf 10.20 und am iPhone (PR 27).

### Release 0.3.4 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.020`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.4 commit=29a15422`. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1795 und 1793 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5805 Tonpakete ohne Fehler.

### Gast-Pad erst bei der ersten Eingabe (2026-11-06, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Issue #26 (13.60): Jedes Gerät meldete zwei Controller, der zweite drückte nie etwas, und die Frage „Wer benutzt diesen
Controller?“ für ihn blieb stehen. Host `commit=171a5874` mit der Änderung (vor dem Commit), per ELF-Loader gestartet:

- `--pads 2`, nur Pad 0 aktiv: „pad 0 attached as the host's controller“, „pad 1 announced, opens when it is used“; Pad 1
  öffnete nie, am Ende „pad 1 gone, never used“. Danach zeigte der Startbildschirm keinen Dialog.
- `--pads 2 --tick-pad 1`: Pad 1 öffnete beim ersten Druck („attached, the console asks who uses it“), am Ende abgemeldet.
- A mit Controller, B tritt ohne Controller bei und drückt W: „pad 1 announced“, 4 ms später „keyboard and mouse … play as
  virtual pad 1“ und „attached, the console asks who uses it“. Alle Streams ohne Lücke, fehlerfrei dekodiert.

Nicht geprüft: echtes Moonlight mit Steam Input oder iOS, und der Fall, in dem das Phantom-Pad als erstes ankommt und damit
das Pad des Hosts ist (dann bliebe der echte Controller ein Gast seines eigenen Geräts).

**Bestätigt vom Melder** (Issue #26, 13.60, Testbuild `v0.3.3-pads-1`, 2026-11-06): Im `sunshine.log` seines Streams „pad 0
attached as the host's controller“, „pad 1 announced, opens when it is used“, am Ende „pad 1 gone, never used“; er meldet,
dass es jetzt geht. Der erste Versuch davor endete mit „Couldn't bind RTSP server to port [48010], Address already in use“:
der vorige Host lief noch.

### Release 0.3.3 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.019` und installierte beim Start das gebündelte ELF;
nach „Restart host“ meldet das Kernel-Log `version=0.3.3 commit=461bdc8c`. Der neue Hilfetext zu „PS for all players“ passt
in die Zeile unter den Settings. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1793 und 1790 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5800 Tonpakete ohne Fehler; danach Pad 0 abgemeldet, „the host role
is free“.

**Tastatur-Pad auf 13.00** (derselbe Host, per ELF-Loader gestartet, vor dem App-Build): Testclient ohne Controller
(`--pads 0`, neu) mit `--kbm-seq`: Maus rechts und links, ein Radschritt, dann W und S. Maus und Rad öffneten weder ein Pad
noch die Maus der Konsole; W öffnete „PS5 virtual pad 0 attached as the host's controller“ und „The keyboard and mouse of a
client without a controller play as virtual pad 0“, am Streamende abgemeldet. Mit Controller (ohne `--pads 0`) gingen
dieselben Eingaben wie bisher an die virtuelle Tastatur und Maus der Konsole. Beide Streams ohne Lücke, fehlerfrei
dekodiert. Nicht geprüft auf 13.00: ein Spiel, die Profile, ein Gast mit Tastatur.

### Raumklang folgt einem breiteren Stream (PR 16, 2026-11-06, 13.00, Testclient, Home-Bildschirm)

Host `commit=c15327c3` (der PR-Stand), Schalter „Surround“ an, Konsole mit Stereo-Ausgabe. A streamt Stereo (40 s), B tritt
nach 10 s per `/resume` mit 5.1 bei (`--surround 51`, 20 s), beide H.264 1080p60 mit `--no-script`.

- Hostlog: A öffnet Typ 0 („2 channels“). Mit B: „sharing the channel another stream reads“, „6 channels“, dann beim Leser
  von A „12 channels from the console for 2 speakers“, also der Wechsel auf Typ 3. Keine Warnung, kein verworfenes Paket.
- A: 2391 Bilder ohne Lücke, fehlerfrei dekodiert, 7796 Tonpakete ohne Dekodierfehler, RMS je Kanal 0,0073 / 0,0062, auch
  über den Wechsel hinweg.
- B: 1183 Bilder ohne Lücke, fehlerfrei dekodiert, 6 Kanäle, RMS 0,0059 / 0,0054 / 0 / 0 / 0 / 0 (die Konsole gibt nur
  Stereo aus).

Nicht geprüft: eine Konsole mit 5.1-Ausgabe und ein abgelehnter Typ 3 (dann bleibt es bei Stereo bis zur nächsten Sitzung).

### Webseite (2026-11-05, 13.00, Branch `webui`, nicht veröffentlicht)

Host und App aus dem Arbeitsstand des Branches, über die App installiert. Den Login hat für den Test `WEBLOGIN SET` vom PC
gesetzt (dafür lag kurz `control_lan` im Datenordner, danach gelöscht und der Host neu gestartet; der Steuer-Socket war
danach vom PC aus wieder geschlossen). Gemessen mit curl vom PC im LAN:

- Ohne Login war 47990 zu; nach `WEBLOGIN SET` lieferte die Seite 200, die API ohne Cookie 401.
- Anmelden: ohne den Header `X-Prospero` 403, mit ihm 200 und ein Cookie `Secure; HttpOnly; SameSite=Strict`, die
  Content-Security-Policy im Kopf. Fünf falsche Passwörter je 401, der sechste Versuch 429 mit 30 s Wartezeit. Im
  `sunshine.log` nur „login from“ mit der Adresse, kein Passwort; ein Login dauerte rund 0,1 s.
- Übersicht, Geräte, Einstellungen (ohne die Experimental-Schalter) und Logs lieferten die Daten der Konsole.
- „Restart host“ über die API: nach etwa 4 s war der Host wieder da, der Login blieb, die alte Sitzung bekam 401.
- `RELISTEN`: „Starting the services anew“, danach „Web page on port 47990“ und ein neuer Login ging.
- Der Diagnosebericht über die Seite kam in 0,03 s, ohne Adressen und Gerätenamen.
- In der App: der Eintrag „Web page“ zeigte die Adresse und „On for tester“; „Turn off“ schloss 47990 sofort. Später „Set login“ über die
  Bildschirmtastatur (Name, dann Passwort als Punkte mit Zeichenzahl): „The web page is on“, und ein Login vom PC mit diesen
  Daten bekam 200.
- Die Oberfläche selbst lief gegen einen Nachbau der API im Browser am PC, auch in Handybreite; den echten Host konnte der
  Browser des Tests wegen des selbst signierten Zertifikats nicht öffnen.

Der Diagnosebericht, die Sperre über alle Adressen (20 Fehlversuche) und das Lesen nur des Log-Endes kamen mit einem zweiten
aufgespielten Stand; auf ihm liefen Bericht, Logs, Übersicht, Login und „Restart host“ erneut, die Sperre über alle Adressen
nur im Unit-Test, das Log-Ende nur mit einem kurzen Log.

Danach hat der Besitzer die Seite im Browser seines Handys gegen die Konsole geöffnet, sich angemeldet und sie angesehen:
ohne Befund.

Nicht geprüft: ein Browser am PC gegen die Konsole, PIN, „End
stream“, Umbenennen und Entfernen über die Seite, eine Adresse außerhalb des LAN, echter Ruhemodus.

### Webseite nach der Testrunde auf 10.20 (2026-11-05, 13.00, Testbuild `v0.3.2-webui-2`)

Host `bc38f990` aus dem Testbuild, App-Ordner und ELF per FTP aufgespielt, der alte Host beendet und das ELF gesendet.

- Zertifikat der Seite, mit `openssl s_client` vom PC gelesen: CN „ProsperoSunshine web page“, `CA:FALSE`, Key Usage
  Digital Signature, EKU TLS Web Server Authentication, SAN `localhost`, die Adresse der Konsole und 127.0.0.1, gültig
  825 Tage. `web_cert.pem` und `web_key.pem` lagen danach im Datenordner.
- `sunshine_state.json` gesichert und durch 15.283 Null-Bytes ersetzt, Host neu gestartet: „Moved the unreadable ...
  to sunshine_state.json.corrupt“, `/serverinfo` mit einer neuen uniqueid, die neue Datei 112 Bytes. Ein frischer
  Testclient (`pair.py`) paarte sich (PairStatus=1), die Datei wuchs auf 1.378 Bytes. Danach die Sicherung zurückgespielt;
  nach dem nächsten Start war die alte uniqueid wieder da.
- Ablehnen einer Anfrage (dafür lag kurz `control_lan` im Datenordner, danach umbenannt): `PAIRINGS` zeigte die Anfrage,
  `CANCELPIN <id>` „OK cancelled“, ein zweites „ERR no such request“, ohne ID „ERR usage“. Der wartende Client bekam
  HTTP 400 „Pairing request cancelled by operator“; derselbe Client paarte sich gleich danach ohne Wartezeit.
- „Restart host“ hat der Besitzer am Handy auf der Seite gedrückt: im Kernel-Log „restart asked for“, „host stopped“,
  dann vom Helfer „relaunch after the old host was gone (1250 ms): sent the host to the ELF loader“ und der neue Host bis
  „Web page on port 47990“. Danach lief nur noch ein Host-Prozess, der Steuer-Socket war wieder nur lokal.

Nicht geprüft: der Neustart auf 10.20, Safari auf dem iPhone mit dem neuen Zertifikat, „Turn on“/„Forget login“ und
„Deny“ in der App, R2/R3 in der Tastatur der App, „End stream“ auf dem Geräte-Tab, das gedrosselte Zertifikats-Log.

### Testbuild für 13.40 (2026-11-05, 13.00)

Pre-Release `v0.3.1-fw1340-1` für Issue #14: 13.40 und alles bis 13.60 bekommt den Datensatz von 13.60. Gemessen nur auf
13.00, wo sich nichts ändert (die 288 Datensätze in `capture_records_test` gleich): Testclient, Mario-64-Demo, je 30 s H.264
mit 1795 Bildern bei 59,94 fps und HEVC mit 1794 Bildern bei 59,95 fps, fehlerfrei dekodiert, Ton ohne Fehler. Auf 13.40
ungetestet, der Bericht des Testers steht aus.

### Berichte eines Mitwirkenden (2026-11-04, Discord, PS5 Pro mit 10.20)

- **HDR-Farben unter Windows:** Moonlight unter Windows mit HDR-Bildschirm und Hardware-Decoding, HEVC 10 Bit HDR 1920x1080,
  GTA III (Titelmenü). Laut Bericht und Foto vom Bildschirm stimmen die Farben jetzt, mit dem Host ab 0.2.4 (HDR10-Metadaten
  statt Nullen). Moonlights Statistik im Foto: rund 58 fps, keine verlorenen Bilder. Windows-Screenshots desselben Streams
  wirken überstrahlt; das liegt an der Aufnahme von HDR-Inhalten, nicht am Stream. Hier nicht gemessen.
- **Tailscale:** Mit [ps5-tailscale](https://github.com/holdmysocks/ps5-tailscale) ab 0.7.3 (sein Beitrag, UDP 47998–48000 in
  den Standardports) erreichte Moonlight auf iOS den Host über die Tailnet-Adresse der Konsole. Hier nicht geprüft.

### Berichte nach 0.3.1 (2026-11-05)

Hier nicht nachgemessen; die Angaben stammen aus den Berichten.

- **Slices bei H.264 auf 10.20 (Mitwirkender, Discord, PS5 Pro):** Host 0.3.1, Moonlight unter Windows mit H.264 1920x1080@60
  und Hardware-Decoding (D3D11VA). `host_diag.log`: `firmware 10.x: no rate fields, QP 24/28`, `OpenVideo rc=0`,
  `Start rc=0`, `first frame has 34 slices, more than hardware decoders on Windows take`, `capture open ok`. Die Felder
  `cfg+0x40/0x44/0x48` werden also angenommen und wirken nicht; der Rückfall greift nicht, weil nichts abgelehnt wird.
  Moonlights Log zeigt wiederholt `avcodec_send_packet() failed: Invalid data found when processing input` und „Resetting
  decoder due to consistent failure“, auf dem Bildschirm kein Bild.
- **Tailscale mit einer Konsole in einem anderen Haushalt (derselbe Mitwirkende):** aktuelles ps5-tailscale und Host 0.3.1,
  Moonlight streamte über das Tailnet.
- **Firmware 13.40 (GitHub-Issue 14, PS5 Slim Digital):** Testbuild `0.3.1-fw1340-1` (13.40 bekommt den Datensatz von
  13.60): `capture OpenVideo rc=0`, `Start rc=0`, `capture open ok`, Ton über Avcap2; Moonlight auf Android zeigte H.264.
  Eingabe und lange Sitzungen sind dort noch nicht berichtet.

### Raumklang auf 13.60 (2026-11-04, Bericht in PR 16 von BuffMcBigHuge)

Ein Mitwirkender baute parallel zu 0.3.1 denselben Weg (Typ 3 bei 5.1/7.1) und maß auf 13.60 mit echten 5.1-Quellen. Hier
nicht nachgemessen; die Angaben stammen aus seinem PR.

- Konsole auf HDMI, Gerätetyp AV-Verstärker, 5.1, Linear PCM. Quelle: die App „Surround Sound Studio“ (6-Kanal-PCM, ihr
  Audioport im Modus 6) und 007 First Light im Spiel. In Typ 3 trugen nur Kanal 0 und 1 Ton, 2–11 waren exakt 0.
- Mit erzwungenem 5.1-LPCM auf dem HDMI-Kabel (AV-Control: `port:HDMI ... 48k 5.1 ... fmt:LPCM`) blieb die Aufnahme gleich:
  Center und Surrounds lagen mit etwa −3 dB auf links/rechts, LFE war still.
- Sein Fernseher meldet im EDID nur 2-Kanal-LPCM. Seine Deutung: Die Systemmischung der Konsole folgt dem EDID und bleibt bei
  2.0, das Kabelformat ändert sie nicht. Offen ist der Test mit einem Gerät, das 6 oder 8 Kanäle LPCM meldet (AV-Receiver
  oder EDID-Emulator).
- `cfg+0x10 = 2` (AudioSecond) ist ebenfalls stereo; `cfg+0x10 = 1` (AudioAout) öffnet mit `rc=0`, liefert aber keine Pakete.
- Für 7.1 nimmt er Sunshines Reihenfolge an (hinten vor seitlich), der Host seit 0.3.1 die von `sceAudioOut` (seitlich vor
  hinten). Für 5.1 ergeben beide dasselbe. Gemessen ist keine.

### Belegter Steuerport (2026-11-05, 13.00, GitHub-Issue 24)

Auf 11.40 stürzte der Host beim Stream-Start ab: `SIGSEGV` im Thread `rtsp::handler`, Lesezugriff auf 0. Die Rücksprünge
aus dem Kernel-Log des Nutzers, auf das ELF von 0.3.2 abgebildet: `cmd_announce` → `session::start` → `start_broadcast` →
`control_server_t::bind` → `net::host_create`. Dort liest Sunshine `host->socket`, auch wenn `enet_host_create` mit
`nullptr` gescheitert ist (UDP 47999 nicht zu binden). Patch 0019 fängt das ab und loggt Port und errno.

Nachgestellt mit `out/port_hold_probe.elf` (bindet UDP 47999 für 60 s) und dem Testbuild `v0.3.2-fw1140-1` (der Patch hieß
dort 0017): Der Testclient bekam auf `ANNOUNCE` HTTP 500, `sunshine.log` meldete `ENet couldn't open UDP port 47999: Address
already in use (errno 48)` und `Couldn't bind Control server to port [47999]`, der Host lief weiter
(`SUNSHINE_SERVER_FREE`). Nach Freigabe des Ports, Mario-64-Demo: H.264 und HEVC 1080p je 30 s mit 1796 Bildern, ohne
Lücke, längste Pause 21 ms, fehlerfrei dekodiert. Laut Nutzer hielt ps5-tailscale den Port: Er hatte die Konsole selbst
unter „Game streaming“ eingetragen; ohne den Eintrag streamt er mit Tailscale (Bericht, hier nicht gemessen).

Auf `main` übernommen als Patch 0019 (2026-11-06, 13.00, Host mit allem bis PR 16): Mit der Probe auf 47999 scheiterte der
Stream mit RTSP 500, `sunshine.log` wie oben, der Host blieb bei `SUNSHINE_SERVER_FREE`. Nach Freigabe des Ports,
Home-Bildschirm: H.264 und HEVC 1080p je 20 s mit 1193 Bildern, ohne Lücke, fehlerfrei dekodiert, Ton ohne Fehler.

### Release 0.3.2 auf der Konsole (2026-11-05, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.018`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.2 commit=86c5eb2d` und `only the host can press PS and Create`. Testclient, Mario-64-Demo: H.264 und HEVC 1080p
je 30 s mit 1795 und 1796 Bildern, ohne Lücke, längste Pause 23 und 21 ms, fehlerfrei dekodiert, keine Tonfehler. Die
Host-Rolle selbst ist im Abschnitt zu Issue 15 gemessen, nicht noch einmal auf diesem Build.

### Firmware-Sonde auf 10.20 und 10.40 (2026-11-05, Bericht eines Mitwirkenden)

`firmware_probe.elf` aus diesem Stand (`docs/firmware-probe.md`), je ein Lauf ohne Optionen und ohne Proben, auf dem
Startbildschirm. PS5 Pro mit 10.20: Host 0.3.1 lief, `QUIT` hielt ihn an, am Ende lief das installierte ELF wieder. PS5 mit
10.40: kein Host lief, keiner wurde gestartet. Beide Läufe 147 s, der Startbildschirm blieb im Vordergrund. Hier nicht
nachgemessen; die Angaben stammen aus den beiden `report.txt`.

| | 10.20 (Pro) | 10.40 |
| --- | --- | --- |
| Datensatz des Hosts | `0x2c8` (−0x18), als einzige Größe von −0x40 bis +0x20 angenommen; Baukasten = `configure()` | ebenso |
| H.264 1080p60, 3 s | 180 Bilder, 34 Slices je Bild | 180 Bilder, 34 Slices |
| HEVC 8/10 Bit nur mit `cfg+0x24` | 17 Slices | 17 Slices |
| QP 20/24 in `0xc4`/`0xc8` gegen ohne | 3,5-fache Bytes; 52 scheitert an `Start` (`0x80c78000`) | 4,8-fache Bytes; 52 ebenso |
| `SetVideoBitrate` von 6 auf 20 Mbit/s | danach 1 Bild, dann `0x80c78000` | 0 Bilder, dann `0x80c78000` |
| ResourceArbitrator | per NID, Firmware in der Tabelle | per NID (`0xde0`, `0xfe0`, `0x1220`, `0x1360`), 10.40 nicht in der Tabelle |

- Auf beiden lehnt der Validator dieselben Wörter ab (`0x80`, `0x188`, `0x1fc`, `0x2b0`), die Bitratenfelder von 13.00
  lassen `Start` scheitern, ebenso H.264 10 Bit (`0x80c70004`) und HEVC mit den Remote-Play-Feldern von 13.00 (`0x81950001`).
- Alle zwölf Modi liefern Bilder (360p60, 540p60, 720p60, 1080p30, 1440p60, 2160p60 in H.264 und HEVC).
- **H.264-Slices:** Die Slice-Felder von 0.3.1 (`0x40`/`0x44`/`0x48`) nimmt die Firmware an, die Zahl der Slices ändern
  sie nicht: 360p 23, 540p und 1080p 34, 720p, 1440p und 2160p 45. HEVC: 6, 9, 12, 17, 23, 34. Über 32 Slices lehnen die
  Hardware-Decoder von FFmpeg unter Windows ab.

### Firmware-Sonde auf 9.60 (2026-11-06, PS5, nicht Pro, Bericht von IlushaKAI in Issue 30)

Hier nicht nachgemessen, es gibt keine Konsole mit 9.60. Die Angaben stammen aus dem Fork des Mitwirkenden (Branch
`firmware-9.60`, `STATUS.md` dort), `firmware_probe.elf` aus 0.3.4, ein Lauf ohne Optionen, 137 s.

- **Datensatz:** Von −`0x40` bis +`0x20` öffnet nur `0x2c0` (−`0x20`), ohne innere Köpfe. `0x2c8` (10.x), `0x2d0`, `0x2e0` (13.00)
  und `0x2e8` (13.60) lehnt der Validator mit `0x81950001` ab. Er schützt `0x80`, `0x168`, `0x1d8` und `0x2a8`.
- **Bilder:** H.264 und HEVC 8/10 Bit (nur `cfg+0x24`) von 360p bis 1440p. 2160p scheitert an `OpenVideo` mit `0x81950008`
  (`video out mode` `0x34`, ohne das 4K-Bit). Slices wie auf 10.x: H.264 1080p 34, HEVC 17.
- **QP:** `cfg+0xc4`/`+0xc8` ändern hier nichts. Die Phase `ratesearch` fand ihn in `cfg+0xac`/`+0xb0`, 0x18 tiefer als auf 10.x.
  Startbildschirm, 1 s, IDR alle 6 Bilder: ohne 4692 kbit/s, 24/28 6275, 18/22 11016, 12/16 19315, 8/12 28197.
  `SetVideoBitrate` mit 1 bis 12 Mbit/s antwortet 0 und ändert nichts, 16 und 20 Mbit/s beenden den Kanal (`0x80c78000`).
- **Systembibliotheken:** `libSceAvcap2` hat 5 Exporte weniger als 13.00. ResourceArbitrator per NID an den Offsets von 10.20.
- **Streams:** Mit dem Datensatz `0x2c0` auf 0.3.4 H.264 1080p60 mit Ton und Controller aus Moonlight für Android. Auf 0.4.0 mit
  denselben Änderungen 2 Minuten HEVC HDR in einem Spiel. 1440p im Spiel fiel auf 1080p zurück. Die Webseite lief dort nicht.
- **Die App sieht `/data` auf dieser Konsole nicht** (Sandbox eines Titels). Ohne die Brücke über den Host im Fork kann die App
  dort keine Einstellungen speichern.
- **Drei ungeklärte Abschaltungen** der Konsole während der Tests, die Logs enden davor. Die Konsole schaltet sich laut
  Mitwirkendem auch sonst gelegentlich ab.
- **Übernommen** (2026-11-07): der Datensatz `0x2c0`, der QP-Offset (`ps_qp_offset`) und die Probe-Phasen `ratesearch` und
  `qp`. 9.60 bleibt ungeprüft (`known=0`, Experimental-Schalter wie auf anderen ungeprüften Versionen). Alles unter 10 bekommt
  diesen Datensatz, für 8.40 (Issue 23) ist das geraten. Für 13.00 und 10.20 bleiben die Datensätze byte-gleich (`tests/capture_records.golden`).

### Tastatur-Pad nach dem Review von PR 20 (2026-11-05, 10.20, PS5 Pro)

Host aus `keyboard-mouse-pad` (0.3.2 mit den Patches 0017 und 0018 und den Korrekturen aus dem Review), vorübergehend per
ELF-Loader gestartet. Moonlight für Linux auf einem PC ohne Controller, dazu Hostlog und Kernel-Log der Konsole.

- **Maus und Rad allein öffnen kein Pad:** Im zweiten Stream (gut 1,5 Minuten) bewegte der Besitzer nur die Maus und drehte
  das Rad, dazu ein Klick, um das Fenster nach vorn zu holen. Kein „play as virtual pad“, im Kernel-Log kein `DEVICE_ADDED`.
  Im ersten Stream hatte ein Klick ins Bild das Pad geöffnet (die linke Maustaste ist in `standard` R2), wie vorgesehen.
- **Die erste Taste öffnet es:** im dritten Stream 3 s nach dem Verbinden „PS5 virtual pad 0 attached as the host's
  controller“ (die Rolle aus 0.3.2). Die Konsole wies es dem Benutzer zu, ohne „Wer benutzt diesen Controller?“.
- **Spiel und Menüs:** „menu keys, no game running“ auf dem Home-Bildschirm, `gta` für GTA III aus der eigenen Liste. Dort
  laut Besitzer Laufen, Kamera, Zielen und Schießen mit der Maus und der Waffen-Cheat (3 3 Q 3 und die Pfeiltasten)
  einwandfrei.
- **Streamende:** „pad 0 detached (0), the host role is free“; im nächsten Stream öffnete die erste Taste das Pad wieder.
- **Mausempfindlichkeit:** Strg+Alt mit dem Rad, rund 60 Schritte im Log, bei schon offenem Pad. `keyboard_sensitivity.txt`
  hielt danach den letzten Wert (750 % für `standard`), eine `.new`-Datei blieb nicht übrig.

- **Nach dem zweiten Review (das Pad entsteht beim Druck, bevor es ihn sieht):** gut 40 s Maus, Rad, Shift und M einzeln
  und Strg+Alt mit dem Rad (drei Schritte im Log; in `standard` ist Strg kein Knopf): kein Pad, kein `DEVICE_ADDED`. W
  öffnete es als Pad des Hosts, dem Benutzer zugewiesen; am Streamende „the host role is free“.

Nicht geprüft (nur im Unit-Test oder im Code): kein Pad frei (vier belegt) und der neue Versuch nach 10 s, Strg zuerst in
`shooter` oder `sports`, eine Taste, die nach dem Abziehen eines Controllers wiederholt wird.

### Host-Rolle am Gerät (2026-11-05, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Host `commit=roles-1` (der Stand dieses Commits ohne die Doku), drei gepairte Identitäten des Linux-Testclients (A, B, C),
alle H.264 1080p60 auf „PS5 Screen“ mit `--no-script`; B, C und A beim zweiten Mal treten per `/resume` bei. Ablauf und Log:

- A allein: Pad 0 „attached as the host's controller“.
- B tritt bei: Pad 1 „the console asks who uses it“. Drei PS-Drücke von B (`--tick-kind ps`): dreimal „PS or Create
  ignored, only the host has them“.
- A geht (sein `/cancel` beendet nur seine Sitzung): „pad 0 detached (0), the host's device keeps the role“.
- C tritt bei und bekommt den freien Slot 0: „the console asks who uses it; the host is away and keeps the role“. Zwei
  Select-Drücke von C (`--tick-kind back`, Create): beide ignoriert.
- C geht, A tritt wieder bei: Pad 0 „attached as the host's controller“.
- A geht, dann B; danach sind alle Pads abgemeldet (`STREAMS` leer). Alle vier Streams fehlerfrei dekodiert (1795, 4479,
  883 und 704 Bilder).
- Ein Lauf davor ohne Beitritt (B, C und A rief `/launch` statt `/resume`): Jedes Gerät war allein, wurde Host, und nach
  seinem Abmelden war die Rolle frei („the host role is free“).

Nicht geprüft: echtes Moonlight, ein echter DualSense an der Konsole, und ob der Dialog „Wer benutzt diesen Controller?“
einem Gast den Nutzer des Hosts anbietet.

## Bauen und testen

Unter Linux mit Docker, aus dem Wurzelordner des Repos. Das Image `prosperosunshine-sdk:v0.45` entsteht einmal aus
`scripts/Dockerfile`:

```sh
docker build -t prosperosunshine-sdk:v0.45 -f scripts/Dockerfile .
docker run --rm -e PROSPERO_COMMIT=$(git rev-parse --short=8 HEAD) --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/build.sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Der Build baut zuerst fehlende Abhängigkeiten (`out/deps`, erster Lauf ~30 min, vor allem Boost), dann alles mit CMake nach
`out/`. Mit `-e PROSPERO_DEBUG=1` entsteht die Debug-Variante in `out/debug/`. `scripts/test.sh` führt die portablen Tests
unter ASan/UBSan aus. Unter Windows tut `scripts/build.ps1` (`-Debug`) dasselbe. App, Testclient und die übrigen Befehle:
`CLAUDE.md`, „Build and test“, und README, „Building from source“.

Host starten: `out/prospero_sunshine.elf` an den ELF-Loader (Port 9021) schicken, oder „Restart host“ in der App. Befehle des
Steuer-Sockets (TCP 47995): `CLAUDE.md` und README, „Development control socket“. Testclient, Pairing und Smoke-Test:
`tools/moonlight/README.md`.

## Wichtige ABI-Erkenntnisse (FW 13.00)

- Avcap2-Videokonfiguration 0x2E0 Bytes; `0x2A4 = 2` zusammengesetzter Bildschirm; RA-Profil 180 (keine Benutzer-ID); Details in `src/platform/ps5/capture.c`.
- Audio-Konfiguration 0x38 Bytes; `cfg+0x1C` ist dieselbe RA-Id 180.
- Descriptor-Wort 9 = Aufnahmezeit in TSC-Ticks (~1,596 GHz); Wort 3 Bildtyp; Wort 4 Framezähler (`docs/avcap2-descriptor.md`).
- `sceAvcap2*` werden per Name über `kernel_dynlib_dlsym` aufgelöst; Export-Tabelle mit 59 NIDs in `docs/re/avcap2-exports.md`.
- `__builtin_cpu_supports` und AVX-Typen in `immintrin.h` funktionieren auf dem `__SCE__`-Ziel nicht ohne Umweg (siehe `scripts/deps/nanors.sh`).
- `std::jthread` braucht `-fexperimental-library` (im CMake gesetzt).

## Verwandtes Repository ps5-mcp

Die direkte Avcap2-Screenshot-Methode für `ps5_screenshot` ist dort umgesetzt und live geprüft (59 Python-Tests, Selbsttest mit elf Prüfungen bestanden). Nicht veröffentlicht; Restpunkte: veralteter Kopfkommentar in `payload/avcap_probe/avcap_probe.c`, ungetesteter Release-Download des neuen ELF.

## Herkunft und Lizenz

Sunshine `a679b059` und alle Bibliotheken mit Pins in `deps.lock`; Lizenztexte nach dem Build in `out/deps/licenses/`. Eigene Änderungen an Sunshine nur als begründete Patches in `patches/sunshine/` (0001 bis 0025, jede mit ihrer Begründung im Kopf; die ersten: 0001 libvirtualhid, 0002 NVENC/VAAPI-Konstanten, 0003/0004 ohne libcurl). Analysen von Sony-Binärdateien enthalten keine Sony-Daten; die entschlüsselten Module liegen nur im ignorierten `.cache/re`.
