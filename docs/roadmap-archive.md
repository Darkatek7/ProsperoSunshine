# Roadmap: Archiv

Die erledigten Pläne, unverändert aus `ROADMAP.md` hierher verschoben (Stand 7. November 2026, nach Release 0.5.0); am Ende der frühere Umsetzungsplan aus `PLAN.md`. Was als Nächstes kommt, steht in [`ROADMAP.md`](../ROADMAP.md).

## Stand (7. November 2026, nach Release 0.5.0)

Seit 0.1.5 sind folgende Releases dazugekommen:

- **0.1.6:** Spiele in der App-Liste von Moonlight, HDR, Touch und Bewegung.
- **0.1.7:** Firmware 10.x und der Bereich „Experimentell“ in der App; auf 13.00 bis 150 Mbit/s.
- **0.1.8:** „Beenden“ in Moonlight schließt das Spiel.
- **0.1.9:** Gekoppelte Geräte in der App.
- **0.2.0:** Modi unter 1080p auf 10.20 und 13.00.
- **0.2.1:** Tastatur und Maus; ein neuer Stream vom selben Gerät löst den alten ab.
- **0.2.2:** Weitere Spieler von anderen Geräten oder mit einem zweiten Controller.
- **0.2.3:**
  - Beim Co-op bleibt der laufende Modus, wenn das erste Gerät geht.
  - Ein Gerät mit falschem Dynamikbereich wird mit Meldung abgewiesen.
  - Ein Titelwechsel unter 1080p fällt nicht mehr auf 1080p zurück.
  - Der Host wartet länger auf die Zuteilung der Aufnahme.
- **0.2.4:**
  - Ein zweiter Aufnahmekanal: Ein Gerät bekommt einen eigenen Codec, Dynamikbereich oder eine eigene Auflösung bis 1080p.
  - Die App zeigt, wer streamt, und beendet den Stream eines Geräts (`STREAMS`, `ENDSTREAM`).
  - HDR-Streams senden HDR10-Metadaten statt Nullen.
- **0.3.0:**
  - PS-Taste nur für Spieler 1 (Einstellung „PS for all players“).
  - Die App zeigt je Gerät die tatsächliche Bitrate, die Laufzeit und Verluste.
  - Beitreten zeigt das erste Bild nach 0,4 statt 2,2 s; eine Übernahme öffnet den Kanal nicht neu.
  - README-Abschnitt zum Spielen über das Internet.
- **0.3.1:**
  - Raumklang 5.1/7.1 hinter dem Schalter „Surround“, Kanalreihenfolge ungeprüft.
  - H.264 auf 10.x verlangt 4 Slices je Bild; auf 10.20 wirkungslos (gemessen von einem Mitwirkenden, `STATUS.md`).
- **0.3.2:**
  - Die Host-Rolle (Konsolennutzer, PS, Create) gehört einem gepairten Gerät; Gäste verlieren auch Create (Issue #15).
  - Firmware 13.40 bekommt den Datensatz von 13.60 (Issue #14, von einem Tester bestätigt).
- **0.3.3:**
  - Tastatur und Maus als eigener Controller für ein Gerät ohne Controller, sieben Profile, Profil je Spiel (PR #20).
  - Firmware-Sonde `firmware_probe.elf` im Release-Archiv, Start auch per `PROBE` (PR #25).
  - Raumklang folgt einem breiteren Stream, der später beitritt (PR #16); der Host stürzt bei belegtem Steuerport nicht mehr
    ab (Issue #24).
- **0.3.4:**
  - Der Controller eines Gasts erscheint auf der Konsole erst bei seiner ersten Eingabe; ein angemeldeter, nie benutzter
    Controller (Steam Input, Bildschirmsteuerung) blockiert nicht mehr mit „Wer benutzt diesen Controller?“ (Issue #26,
    vom Melder auf 13.60 bestätigt).
- **0.4.0:**
  - Eine Webseite zur Steuerung des Hosts (Issue #21, PR #27): Status, PIN, Streams, Geräte, Einstellungen, Logs,
    Diagnose, Neustart; von einem Mitwirkenden auf 10.20 und am iPhone getestet.
- **0.4.1:**
  - Firmware 9.60: eigener Datensatz und QP-Felder, die App erreicht ihre Dateien über den Host, wo ein Titel kein `/data`
    hat (Issue #30, aus dem Fork von IlushaKAI, hier nicht prüfbar).
  - Der Steuerport schließt Verbindungen, die mit einer HTTP-Anfrage beginnen; die Webseite leitet `http://` auf `https://`
    um (Issue #31); der Prozess heißt `ProsperoSunshine` (Issue #32).
- **0.4.2:**
  - View und Menu zusammen sind das Touchpad für Controller ohne eines (Xbox, Moonlight für Windows gibt Share nicht
    weiter); „PS5 Screen“ hängt nach einem Stream nicht mehr bei Moonlights „Quitting…“.
- **0.5.0:**
  - Konsolenprofile pro gekoppeltem Gerät (PR #33): nur diese Profile, nur zuschauen oder freie Wahl; Fehler in der
    Profildatei führen zu „nur zuschauen“.
  - Apps vor nicht vertrauten Geräten verstecken (Issue #36), auch Start und Fortsetzen per ID werden abgelehnt.
  - Ein Debug-Build in jedem Release, von CI aus demselben Commit gebaut (Issue #28, PR #29).

Firmware: 13.00 und 10.20 sind geprüft. 9.60 (Issue #30), 10.40, 11.40, 12.70, 13.40 und 13.60 streamen laut Nutzerberichten (Issues #1, #2, #14, #17, #24), dort aber nicht
gemessen.

Co-op: Weitere Geräte hängen sich an den laufenden Stream. Ein Tester spielte auf 10.20 mit vier Spielern, auch über das
Internet (Issue #11). Seit 0.2.4 gibt es zwei Aufnahmekanäle (RA-Profil 180 und 175, auf 13.00 gemessen): Ein Gerät mit
anderem Codec, Dynamikbereich oder einer Auflösung bis 1080p bekommt den zweiten, alle weiteren teilen sich einen davon.

## Stand nach Release 0.1.5 (1. November 2026)

Erledigt und live belegt (Details in `STATUS.md`): Phase 0 komplett, Spikes R1–R5 positiv (R6 ohne mDNS, das kam mit P9.2), Phase 1 komplett, P2.1–P2.4, P3.1–P3.4, P4.1–P4.2, P5.1, aus P6 die 30-Minuten-Sitzung und zehn Wiederverbindungen, P6.4 (automatischer Testclient in `tools/moonlight/`), P4.3 geprüft (Pad wird nach Abbruch neutral), P6.2 Paketverlust (10 % ohne Abbruch), P7.1 bis P7.5 und P8.1 bis P8.5, P8.7, P8.8. Ende-zu-Ende-Stream mit einem moonlight-common-c-Client funktioniert.

Releases: `v0.1.0` am 2026-10-30 auf GitHub (CI grün, Katalog-Eintrag `PPSA47989` gemergt), `v0.1.1` mit der PS-Taste über Select, `v0.1.2` am 2026-10-31 mit HEVC, Rumble und Lichtleiste, `v0.1.3` am selben Tag mit Diagnosebericht, adaptiven Triggern und Erholung nach dem Ruhemodus, `v0.1.4` ebenfalls am selben Tag mit H.264 in 1440p und 4K und dem Rückfall auf 1080p, `v0.1.5` am 2026-11-01 mit HEVC in 1440p und 4K, größerem Frame-Budget und einem ersten Versuch für 13.60. Die Store-API meldet `0.1.5`. Die Installation aus dem Store hat laut Nutzerbericht (2026-10-31) geklappt; die Installation der späteren Versionen ist nicht eigens geprüft (P8.9).

Echte Clients: Moonlight Web, Moonlight am PC und Moonlight auf dem iPhone sind gepairt und streamen, das iPhone findet die PS5 per mDNS von selbst. Spiele sind als Bildquelle möglich (P1.4), der Host stellt Bild und Ton nach Spielstart/-ende selbst wieder her, es läuft nur eine Sitzung gleichzeitig. HEVC bis 50 Mbit/s (Nutzertest mit GTA III am PC bei 15 und 49,5 Mbit/s, iPhone bei 7 Mbit/s), Rumble (Nutzertest) und Lichtleiste (am Testclient) sind drin. H.264 und HEVC laufen in 1440p und 4K, HEVC-4K auch in Moonlight am PC (Nutzertest vor dem größeren Frame-Budget).

Verifiziert ist nur Firmware 13.00. Für andere Versionen gibt es jetzt konkrete Spuren: Der Video-Datensatz hängt von der Firmware ab (`docs/firmware-porting.md`). Für 13.60 enthält 0.1.5 ein Layout aus öffentlichen Projekten, ungeprüft (Issue #1). Für 10.20 meldet ein Tester Streams mit einem kürzeren Datensatz, 10.40 dürfte ihn ebenfalls brauchen (Issue #2). Das bestimmt die nächsten Schritte, siehe „Nächste konkrete Schritte“ am Ende.

## Zielbild v1.0

Ein unveränderter Moonlight-Client im lokalen Netz fügt die PS5 per IP hinzu, paart sich und streamt den PS5-Bildschirm mit H.264 1080p SDR, Stereo-Ton und einem Controller. Kein PC vermittelt im Betrieb. Nach Sitzungsende sind alle eigenen Ressourcen freigegeben.

Nicht in v1.0: HEVC/AV1, HDR, 4K, mehrere Clients oder Controller, Mikrofon, Rumble, adaptive Trigger, Internetzugriff, Maus/Tastatur.

Davon ist seit 0.1.2 HEVC (bis 50 Mbit/s), Rumble und die Lichtleiste des Pads dazugekommen, seit 0.1.4 1440p und 4K, jeweils nur auf Firmware 13.00. HDR läuft im Testclient und mit einem echten Moonlight am HDR-Fernseher (`STATUS.md`), veröffentlicht seit 0.1.6. Seit
0.2.2 kommen weitere Geräte und Controller als Spieler dazu. Weiter nicht vorgesehen: AV1, Mikrofon, Internetzugriff durch
den Host selbst (Moonlight über das Internet geht mit der eigenen Portfreigabe). Tastatur und Maus
gehen seit dem Stand nach 0.2.0 auf eine virtuelle Tastatur und Maus der Konsole, gemessen auf 13.00 (`STATUS.md`).

## Architekturentscheidung (Vorschlag ADR-001, Abschluss in Phase 0)

**Sunshine wird als festgehaltener Upstream-Stand gebaut, ergänzt um eine PS5-Plattformschicht.** Protokollcode (Pairing, RTSP, Paketierung, FEC, Verschlüsselung, Steuerkanal, Eingabeauswertung) bleibt möglichst unverändert, damit Kompatibilität und spätere Upstream-Updates erhalten bleiben. Ersetzt wird nur, was an PC-Hardware, Prozessstart oder Desktop hängt.

Die Nahtstellen im Sunshine-Commit `a679b059` sind eng und gut abgegrenzt:

| Nahtstelle | Upstream | PS5-Umsetzung |
| --- | --- | --- |
| Video | `stream.cpp` ruft `video::capture(mail, config, session)` und liest `video::packet_t` aus `mail::video_packets`; IDR-Wünsche kommen über `mail::idr`. | Eigene `video::capture()` liefert Avcap2-Frames als `packet_raw_generic`. Kein Encoder, kein Bildformatwandel. |
| Audio | `audio::capture()` holt PCM über `platf::audio_control()->microphone()` und kodiert mit Opus. | PS5-Implementierung von `audio_control_t`/`mic_t`. Opus-Pfad bleibt upstream. |
| Eingabe | `input.cpp` ruft `platf::alloc_gamepad`, `gamepad_update`, `free_gamepad`. | Natives virtuelles PS5-Pad, dazu eine virtuelle Tastatur und Maus der Konsole (`src/platform/ps5/kbm.c`). |
| Plattform | `platform/common.h`: `init`, `appdata`, `send`/`send_batch`, Hostname, MAC, Timer, Threadpriorität. | `src/platform/ps5/`. |

Einordnung der Sunshine-Quellen:

| Behandlung | Dateien |
| --- | --- |
| Übernehmen, höchstens kleine Patches | `nvhttp.cpp`, `httpcommon.cpp`, `rtsp.cpp`, `stream.cpp`, `crypto.cpp`, `network.cpp`, `audio.cpp`, `input.cpp`, `config.cpp`, `logging.cpp`, `cbs.cpp`, `file_handler.cpp`, `globals.cpp`, `stat_trackers.cpp`, Header wie `sync.h`, `thread_safe.h`, `thread_pool.h`, `task_pool.h`, `utility.h`, `uuid.h`; aus moonlight-common-c `RtspParser.c` und nanors |
| Ersetzen | `video.cpp` (PS5-Capture statt Encoder-Pipeline), `process.cpp` (feste App „PS5-Bildschirm“, kein Prozessstart), `main.cpp`/`entry_handler.cpp` (Daemon-Start, Konfigurationspfad), `platform/linux/*` → `platform/ps5/*` |
| Weglassen, Aufrufe per Stub | `confighttp.cpp` (Web-UI), `system_tray.cpp`, `display_device.cpp` + libdisplaydevice, `upnp.cpp` + miniupnpc, NVENC, `virtualhid_input`, curl-Teile in `httpcommon.cpp` |

Upstream-Code bleibt unverändert im Cache. Änderungen liegen als einzelne, begründete Dateien unter `patches/sunshine/` und werden beim Build angewendet. Sunshine selbst kommt mit Hash in `deps.lock`.

**Folge für die Abhängigkeiten:** Boost wird mit den von den übernommenen Dateien tatsächlich genutzten Teilen gebaut (Asio, SSL, property_tree, endian, Log, program_options, filesystem; locale und process nur, falls nach dem Weglassen noch referenziert). FFmpeg wird minimal gebaut, nur `libavutil` und `libavcodec` ohne Encoder/Decoder, aber mit H.264-Parser und Coded-Bitstream (`cbs_h264`). So bleiben `video.h` und `cbs.cpp` unverändert, und Sunshines SPS-Anpassung für niedrige Decoder-Latenz funktioniert.

Gegenentwurf, falls Boost oder der Gesamtbau scheitern: Nur `nvhttp`/`rtsp`/`stream`/`crypto`/`input`/`audio` in ein eigenes, schlankes Gerüst übernehmen und Logging, Konfiguration und HTTP-Server selbst stellen. Mehr Eigenarbeit, schwerer zu aktualisieren. Entscheidung fällt mit P0.6.

## Phasenübersicht und kritischer Pfad

```text
Phase 0  Fundament ──► Phase 2  Sitzung & Pairing ──► Phase 3  Video in Moonlight ──► Phase 6  Härtung ──► Release 0.1
   │                        ▲                                ▲                          ▲
   ├─► Phase R  Risiko-Spikes (IDR, Audio, Encoder, Pad) ─────┼──────────┐               │
   └─► Phase 1  Capture-Dauerbetrieb ────────────────────────┘          ├─► Phase 4  Controller
                                                                         └─► Phase 5  Audio
```

Kritischer Pfad: P0.6 (Sunshine-Kern baut für PS5) → P2.3 (Pairing) → P3.2 (erstes Bild). Die Risiko-Spikes laufen früh und parallel, weil ihr Ergebnis den Funktionsumfang von v1.0 verändern kann.

Aufwand: **S** etwa eine Arbeitssitzung, **M** zwei bis drei, **L** mehr. Jede Aufgabe ist erst erledigt, wenn ihr Nachweis in `STATUS.md` steht; auf der PS5 bedeutet das einen Live-Lauf mit Log, nicht nur einen erfolgreichen Build.

## Phase 0: Fundament (offener Rest von M0)

Bereits erledigt: Toolchain, C++23, `std::jthread` (mit `-fexperimental-library`), Sockets, OpenSSL 3.5.9, Opus 1.6.1, ENet, nanors, gepinnte Quellen in `deps.lock`.

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P0.1 | Sunshine-Tarball und moonlight-common-c in `deps.lock` aufnehmen, Patch-Mechanismus `patches/sunshine/` in `deps.sh` | S | Reproduzierbarer, hashgeprüfter Quellbaum unter `.cache` |
| P0.2 | Boost ≥ 1.89 cross-bauen (b2 mit Prospero-Toolchain), zuerst header-only Teile, dann Log, program_options, filesystem; Bedarf an locale/process anhand der Includes nach Weglassen bestimmen | M | Statische Bibliotheken in `out/deps`; Live-Probe: Asio-TCP-Server mit TLS über libssl, Boost.Log nach klog, property_tree XML |
| P0.3 | Simple-Web-Server (gepinnter Commit) und nlohmann_json bereitstellen | S | Header in `out/deps`; HTTPS-Endpunkt im Probe |
| P0.4 | Minimales FFmpeg: `--disable-everything --enable-avcodec --enable-avutil --enable-parser=h264 --enable-bsf=h264_metadata` (oder was `cbs.cpp` braucht) | M | `libavcodec.a`, `libavutil.a`; Probe parst SPS aus `out/capture.h264` und schreibt VUI um |
| P0.5 | Eigenes CMake-Projekt statt `build.sh`: Toolchain `prospero.cmake`, Ziele für Capture-Bibliothek, Probes, Tests und später den Host | M | `build.ps1` ruft CMake; alle vorhandenen Probes bauen weiter |
| P0.6 | Sunshine-Kern mit PS5-Stubs kompilieren und linken: alle Dateien aus „Übernehmen“, Stubs für Plattform, Video, Audio, Eingabe, Prozess | L | `prospero-sunshine.elf` startet, lädt Konfiguration, beendet sich sauber; Liste der nötigen Patches mit Begründung |
| P0.7 | ADR-001 in `docs/adr/001-sunshine-port.md` festschreiben, inklusive Lizenz- und Herkunftsdokumentation | S | Entscheidung, verworfene Alternative, Lizenzliste |

**Abnahme Phase 0:** Der Host-ELF mit gelinktem Sunshine-Kern startet auf der PS5 und beendet sich auf Signal ohne Leck an eigenen Ressourcen. Alle Bibliotheken sind gepinnt, gehasht und lizenzdokumentiert.

## Phase R: Risiko-Spikes (parallel zu Phase 0 und 1)

Jeder Spike endet mit einer schriftlichen Antwort in `docs/` („geht, so“ oder „geht nicht, Ersatz ist …“), nicht mit Produktcode.

| ID | Frage | Vorgehen | Ersatzlösung, falls negativ |
| --- | --- | --- | --- |
| R1 | Lässt sich bei Avcap2 ein IDR-Frame sofort anfordern? Moonlight fordert nach Paketverlust einen an. | Exporte von `libSceAvcap2` auflisten (NIDs mit bekannten Namen abgleichen), Konfigurationsfelder um `0x2E0` durchsuchen, Kandidaten live testen | Kurzes festes GOP (gemessen: 11 IDR in 601 Frames, etwa alle 55 Frames ≈ 0,9 s) und bis zum nächsten IDR einfrieren; Abstand per Konfiguration verkürzen, falls möglich |
| R2 | Wie kommt der Spielton an? | Audio-Funktionen in Avcap2 suchen (analog zu `ReadVideo`), sonst Remote-Play-/Share-Pfad (`SceRemotePlay`, `SceAvCapture`, `orbis_audiod`) untersuchen; Format, Rate, Zeitstempel festhalten | v1.0 zunächst ohne Ton als „0.x Preview“ veröffentlichen; Audio wird eigenes Folgeziel |
| R3 | Lassen sich Bitrate, Bildrate und Auflösung einstellen? | Konfigurationsfelder variieren, Ergebnis mit ffprobe messen | Feste Werte als einzigen ausgehandelten Modus anbieten (Moonlight akzeptiert, was der Host meldet) |
| R4 | Laufzeitmodell: läuft der Host stabil stundenlang als Payload-Daemon, und wo liegen persistente Daten? | Langlauf eines Leerlauf-Daemons, Schreibtest unter `/data/prosperosunshine`, Single-Instance-Sperre, Verhalten bei Ruhemodus | Start durch die App der anderen Session statt eigenständigem Daemon |
| R5 | Natives virtuelles Pad ohne Mono | RemoteEye (`../ps5-mcp/payload/RemoteEye`, C#) nach C portieren: Gerät anlegen, Report senden, freigeben; Zuordnungsdialog „Wer benutzt diesen Controller?“ beobachten | Pad an bestehenden Benutzer binden; Dialog einmalig vor Streambeginn bestätigen lassen |
| R6 | Port-/Dienstkonflikte | Prüfen, ob 47984, 47989, 48010 (TCP) und 47998–48000 (UDP) frei sind; ob mDNS (5353) mit `SceSpZeroConf`/nanodns kollidiert | Basisport verschieben (Moonlight unterstützt `IP:Port`); Discovery erst nach v1.0 |

**Abnahme Phase R:** Für R1 bis R6 liegt je eine dokumentierte Antwort vor. Ist R1 oder R2 negativ, ist der angepasste v1.0-Umfang in diesem Dokument nachgetragen.

## Phase 1: Capture-Dauerbetrieb (M1)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P1.1 | `CaptureSession` (C++) über `capture.h`: Start, blockierendes Lesen mit Timeout, Stop, Ressourcenentzug als Ereignis | M | Klare Zustandsmaschine; Tests mit nachgebildetem Backend |
| P1.2 | Frame-Metadaten: Größe, IDR, SPS/PPS-Wechsel, lokale Monotonzeit; Descriptor-Zeitstempel untersuchen | M | Feldbedeutung in `docs/avcap2-abi.md` |
| P1.3 | Diagnose-Ausgabe: Stream per TCP an den PC (nur Entwicklung) statt Datei | S | `ffplay` am PC zeigt Live-Bild |
| P1.4 | Langlauf und Zyklen: 10 Minuten Home-Menü und Homebrew, 5× Start/Stop, ein reguläres Spiel | M | Frame-Abstände, Speicherverlauf, `ffprobe` fehlerfrei |

**Abnahme Phase 1:** Wie in `PLAN.md` M1: zehn Minuten decodierbares Video, gemessene Frame-Abstände und Speicher, fünf saubere Zyklen.

## Phase 2: Sitzung und Pairing (M2)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P2.1 | Host-Daemon: Konfigurationsdatei und Zustand unter `/data/prosperosunshine`, Hostidentität (Zertifikat, UUID) beim ersten Start erzeugen und behalten | M | Neustart behält Identität |
| P2.2 | `nvhttp` live: `/serverinfo` über HTTP/HTTPS, Applist mit genau einer App „PS5-Bildschirm“, nur tatsächlich unterstützte Modi melden | M | Moonlight zeigt die PS5 nach manuellem Hinzufügen als Host |
| P2.3 | Pairing mit PIN: Entwicklungsweg über lokalen Steuer-Socket (vom PC oder MCP-Tool), Produktweg über die Host-API (P2.5); zusätzlich PS5-Benachrichtigung „Pairing angefragt“ | M | Moonlight paart; ungepairter Client wird abgewiesen; Paarung übersteht Neustart |
| P2.4 | Launch, Resume, Cancel und RTSP-Aushandlung bis `PLAY`; zweite Sitzung mit klarer Meldung ablehnen | M | Moonlight startet die App, Sitzung wird mit Stub-Medien aufgebaut und sauber beendet |
| P2.5 | Host-API für die PS5-App (andere Session): C-Header `prospero_host.h` mit Start, Stop, Status, Pairing-PIN, Fehlercodes; Transport als lokaler Socket mit kleinem JSON-Protokoll | M | Header und Protokoll mit der App-Seite abgestimmt; Testclient am PC |

**Abnahme Phase 2:** wie `PLAN.md` M2, plus: Pairing ist ohne Web-UI vollständig über Steuer-Socket bzw. Host-API möglich.

## Phase 3: Video in Moonlight (M3)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P3.1 | `video::capture()` für PS5: `CaptureSession` → `packet_raw_generic` mit IDR-Kennung, Frame-Index, Zeitstempel; SPS/PPS vor jedem IDR | M | Pakete landen in `mail::video_packets` |
| P3.2 | Erstes Bild in Moonlight über den unveränderten `stream.cpp`-Pfad (Paketierung, FEC, Verschlüsselung) | M | Moonlight zeigt das Home-Menü |
| P3.3 | IDR-Anforderung und Referenzinvalidierung gemäß R1 bedienen; vorhandene `frame_queue.hpp`-Regeln (Verwerfen bis IDR) anbinden | M | Künstlicher Paketverlust führt zu Erholung in begrenzter Zeit |
| P3.4 | SPS-Anpassung über `cbs.cpp` (VUI, `max_dec_frame_buffering`) prüfen und aktivieren | S | Geringere Decoder-Latenz am Client messbar oder Begründung, warum nicht nötig |
| P3.5 | `send_batch` für die PS5 (`sendmmsg`, falls vorhanden, sonst Schleife), Pacing messen | S | Keine Burst-Verluste bei 20 Mbit/s |

**Abnahme Phase 3:** wie `PLAN.md` M3: kontinuierliches Bild aus Home-Menü und Homebrew, Wiederaufbau nach Abbruch, kontrolliertes Verhalten bei Verlust und Rückstau.

## Phase 4: Controller (M4)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P4.1 | Native Pad-Komponente aus R5 hinter `platf::alloc_gamepad`/`gamepad_update`/`free_gamepad` | M | Tasten, Sticks, Trigger kommen an |
| P4.2 | Abbildung Moonlight → DualSense: Wertebereiche, Totzonen, PS-Taste (Guide), Touchpad-Klick (Back/Select) | S | Tabelle in `docs/input-mapping.md`, Unit-Tests |
| P4.3 | Watchdog: Pad neutralisieren bei Eingabepause und Verbindungsverlust; Konflikt mit gleichzeitigen MCP-Eingaben verhindern | S | Test: Taste halten, Kabel ziehen → keine hängende Eingabe |

**Abnahme Phase 4:** wie `PLAN.md` M4.

## Phase 5: Audio (M5, abhängig von R2)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P5.1 | `audio_control_t`/`mic_t` für PS5 nach Ergebnis von R2; Stereo 48 kHz, Paketdauer laut Aushandlung | M | `audio::capture()` liefert Opus-Pakete |
| P5.2 | Gemeinsame Zeitbasis mit Video, Drift über 30 Minuten messen, Korrektur festlegen | M | Messverfahren und Grenzwert vor dem Test dokumentiert |

**Abnahme Phase 5:** wie `PLAN.md` M5.

## Phase 6: Härtung und Diagnose (M6)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P6.1 | Gemeinsamer Sitzungszustand (bereit, Aufbau, aktiv, Abbau, Fehler) über alle Komponenten | M | Zustandsdiagramm und Tests |
| P6.2 | Fehlerinjektion: Clientabsturz, belegte Capture-Ressource, Ressourcenentzug, Ruhemodus, Teilinitialisierung | M | Testprotokoll in `docs/tests/` |
| P6.3 | Begrenzte Logs (klog und Rotationsdatei) mit Sitzungs-ID, FPS, Bitrate, Queue-Dauer, Verlust, Abbaugrund; keine Schlüssel oder PINs | S | Log-Review |
| P6.4 | Automatisierter Smoke-Test vom PC: Moonlight-Client in Docker (Kandidat: moonlight-embedded, falls ein Datei-/Null-Ausgabemodus vorhanden ist) paart, startet, zählt Frames | M | Ein Befehl prüft einen Build gegen die Konsole |

**Abnahme Phase 6:** wie `PLAN.md` M6: 30-Minuten-Sitzung, zehn Wiederverbindungen, fünf Host-Zyklen, kein Speicheranstieg, keine hängenden Eingaben oder Ressourcen; danach ein zweiter Client auf anderer Plattform.

## Phase 8: PS5-App „ProsperoSunshine“ (läuft parallel, eigene Session)

Eine Homebrew-App auf der Konsole (Vorbild: PS5SD) macht den Host ohne PC bedienbar und ist der Produktweg aus P2.5.

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P8.1 | App-Gerüst, Build, Packaging, Installation nach dem PS5SD-Muster | M | App startet auf der Konsole |
| P8.2 | Einstellungen lesen/schreiben (`/data/prosperosunshine/sunshine.conf`: Hostname, Log-Stufe, `ping_timeout`, Verschlüsselung, Basisport) | S | Wert in der App ändern, Host neu starten, Wirkung im Log |
| P8.3 | Host-Status, wartende Pairing-Anfragen und PIN-Eingabe über den Steuer-Socket (lokal 127.0.0.1:47995) | S | Moonlight paart mit PIN-Eingabe auf der Konsole |
| P8.4 | Autoload des Host-ELF über den Payload Manager an/aus | S | Nach Neustart der Konsole läuft der Host bzw. nicht |
| P8.5 | Host starten/neu starten aus der App | S | Schaltfläche lädt das ELF neu |
| P8.6 | Optik auf das Niveau von ProsperoStore/ProsperoEden/Playden | M | Screenshots aller Seiten, komplett per Controller bedienbar |
| P8.7 | Icon (`icon0.png`, 512²) und Home-Hintergrund (`pic0.png`, 1920×1080), Quellen in `assets/app/`; Einordnung im Media-Tab wie Prospero Themes | S | Kachel im Media-Tab mit neuem Icon, App startet von dort |
| P8.8 | App-ID: `PPSA47989` gegen den ProsperoStore-Katalog und installierte Titel prüfen und vor dem Release erneut prüfen; bei Kollision eine freie ID wählen | S | ID taucht im Katalog nicht auf |
| P8.9 | Release im ProsperoStore (Kategorie `streaming`, ZIP mit Titelordner, Version = `contentVersion`), zusammen mit P7.4 | S | App lässt sich aus dem Store installieren und startet |
| P8.10 | Tastatur und Maus als Controller in der App: die Einstellungen (`keyboard_mouse_pad`, `keyboard_mouse_profile`, `keyboard_mouse_sensitivity`, `keyboard_mouse_profile_by_game`) und eine Seite, auf der man ein installiertes Spiel (auch Homebrew und Forwarder) wählt und ihm ein Profil gibt; sie schreibt `keyboard_profiles.txt` (`docs/keyboard-profiles.md`) | M | Profil für ein Spiel in der App setzen, Spiel starten, Profil im Log |

**Abnahme Phase 8:** Ein Nutzer ohne PC kann den Host einrichten, Moonlight pairen und Autoload festlegen.

Stand: P8.1, P8.3, P8.4 und P8.5 live geprüft; P8.2 ebenfalls live geprüft. P8.6 läuft in der App-Session. P8.7 live geprüft (2026-10-29): Die App steht im Media-Tab (`applicationCategoryType` 65536, `contentBadgeType` 1, `launchActivity`) und startet von dort; der `amm`-Block musste dafür aus `param.json` raus, mit ihm scheitert der Start mit 0x80020016. Für P8.8 wurde am 2026-10-29 geprüft: `PPSA47989` kommt im Store-Katalog (156 Apps, Stand 2026-10-28) nicht vor. P8.9 (2026-10-31): Release 0.1.2 steht im Store (die Store-API meldet `0.1.2` und `01.000.003`); die Installation aus dem Store klappte laut Nutzerbericht.

## Phase 9: Feinschliff vor dem Release (nach dem ersten Handy-Test)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P9.1 | Steuer-Socket nur auf `127.0.0.1` (die App spricht lokal); LAN-Zugriff für Entwicklung nur per Schalterdatei | S | Vom PC aus abgewiesen, aus der App erreichbar |
| P9.2 | Automatisches Finden in Moonlight (mDNS `_nvstream._tcp`) | M | Moonlight am PC/Handy listet die PS5 ohne IP-Eingabe |
| P9.3 | Bildrate des Clients beachten (30 fps gehen); unter 1080p laufen 360p, 540p und 720p im Host auf 13.00 und 10.20 (STATUS.md) | M | Client erhält die angeforderte Auflösung bzw. Bildrate, oder dokumentiert, warum nicht |
| P9.4 | Bild/Ton-Drift über 10 Minuten messen (`tools/moonlight/av_sync.py`) | S | Drift pro Minute im Toleranzbereich |
| P9.5 | Firmware-Porting vorbereiten: ResourceArbitrator-Funktionen per NID statt fester Adressen, Anleitung `docs/firmware-porting.md` | M | NID-Suche findet auf 13.00 die bekannten Adressen |
| P9.6 | Weitere Clients: Android, TV/Shield, Steam Deck (Moonlight Web, PC und iPhone laufen bereits) | S | Je ein Stream mit Bild, Ton und Eingabe |
| P9.7 | Spike: HEVC über den PS5-Encoder (erledigt, `docs/re/hevc.md`) | M | Ergebnis dokumentiert |
| P9.11 | HEVC im Host (erledigt, in 0.1.2; am Testclient gemessen; Moonlight am PC mit GTA III bei 15 und 49,5 Mbit/s und auf dem iPhone bei 7 Mbit/s liefen im Nutzertest gut). Der Windows-Testclient misst hohe Bitraten ohne Dockers Paketverlust. Offen: ob mehr als 50 Mbit/s sinnvoll sind | L | Moonlight streamt HEVC in einem Spiel, H.264-Clients laufen unverändert |
| P9.10 | Rumble: Motorwerte des virtuellen Pads an den Client (erledigt, Nutzertest 2026-10-31); Lichtleiste erledigt (am Testclient gemessen); adaptive Trigger im Host umgesetzt und mit Teardown und DualSense bestätigt; offen ist das Zurücksetzen in Moonlight nach dem Stream (`docs/re/adaptive-triggers.md`) | M | Controller am Client vibriert im Spiel |
| P9.9 | PS-Taste für Clients ohne diese Taste: Select halten sendet PS, kurzer Druck Create | S | Kontrollzentrum und Create-Menü öffnen sich im Stream; Test in Moonlight am Handy |
| P9.8 | Virtuelles Pad nach Sitzungsende: Laut App-Session bekam die App nach einem Handy-Stream (App blieb „running“, kein Client) keine Eingaben vom RemoteEye-Test-Pad, erst nach Host-Neustart. Prüfen, ob `free_gamepad` läuft (Log „detached“) oder ob sich Host-Pad und RemoteEye-Pad stören; ggf. „PS5 Screen“ nach Client-Abbruch beenden | S | Nach Streamende erreichen Eingaben wieder Apps, ohne Host-Neustart |

Stand: P9.1 live geprüft (2026-10-29): Der Steuer-Socket ist vom PC aus abgewiesen, das Log meldet „local only“. Moonlight auf dem iPhone streamt seit 2026-10-29 mit Bild und Ton; vorher kam nur Ton, weil der Host fälschlich HEVC angeboten hatte (`STATUS.md`).

Stand P9.5 (2026-10-29): erledigt für 13.00. Die vier ResourceArbitrator-Funktionen sind unter NIDs exportiert und werden jetzt per `kernel_dynlib_resolve` gefunden, abgeglichen mit der Offset-Tabelle je Firmware; `ra_nid_probe.elf` meldet auf 13.00 `result=PASS` mit `0xdb0`/`0xee0`/`0x10b0`/`0x11f0`. Byte-Signaturen wurden verworfen (Code nur über den Kernel lesbar, Muster wären Sony-Code-Auszüge). Andere Firmware bleibt ungeprüft; Avcap2-Konfiguration, Descriptor, HEVC-Konfiguration (Encoder-Speicher, `ReadVideo2`) und Pad-Layouts sind 13.00-Messungen (Checkliste in `docs/firmware-porting.md`). HEVC bietet der Host auf 13.00 und 10.20 an.

Stand P9.2 (2026-10-29): Responder gebaut, Tests grün; mit `mdns_probe` live geprüft (PC sieht PTR/SRV/TXT/A von der Konsole, `tools/mdns_query.py`). Am 2026-10-30 auch im echten Host bestätigt; am 2026-10-31 meldete der Nutzer, dass Moonlight auf dem iPhone die PS5 von selbst listet. Stand P9.4 (2026-10-29): 10 Minuten, Ton relativ zu Bild −0,01 ms/min, also kein Drift; Klickton im Home-Menü diesmal nicht erkannt (0/380) und Bildreaktion Median 265 ms statt früher 23–25 ms. Neu gemessen (2026-10-31, `STATUS.md`): Die 23–25 ms waren keine Reaktion, der Detektor sprach auf die Schlüsselbilder an; `av_sync.py` überspringt sie jetzt. In der App liegt Taste → Bild im Median bei 57 ms, auf dem Home-Bildschirm bei 95 ms, Ton liegt ±10 ms um das Bild. Ein neuer 10-Minuten-Lauf am 2026-10-31 ergab Bild 96 ms (380/380), Ton 8,6 ms danach (221/380), kein Drift zwischen beiden. Die 265 ms kamen nicht wieder, P9.4 ist damit erledigt.

Stand P9.3 (2026-11-03): Bildrate und Auflösung werden beachtet, in den festen Modi von 360p bis 4K; unter 1080p auf 13.00 und 10.20 der nächstgelegene von 720p, 540p und 360p, sonst mit dem Experimental-Schalter. Stand P9.9 (2026-10-31): Select halten sendet PS, im Stream am Testclient gemessen und in Moonlight auf dem iPhone bestätigt. `back_button_timeout = -1` (PS-Taste aus) ist am 2026-10-31 live geprüft.

Stand P9.8 (2026-10-30): behoben und live geprüft. Sunshine hielt Pads für ein späteres Fortsetzen, solange die App lief; jetzt endet „PS5 Screen“ mit dem letzten Client (Patch 0006). Lizenzen: Texte der SDK-Laufzeit liegen in `licenses/ps5-payload-sdk-runtime/` und werden von CI und App-Build mitgeliefert.

## Release 0.1 (öffentlich)

| ID | Aufgabe | Aufwand |
| --- | --- | --- |
| P7.1 | GitHub Actions: Docker-Build, portable Tests, ELF als Artefakt | S |
| P7.2 | Lizenzprüfung: GPL-3.0 für eigenen und Sunshine-Code, Lizenztexte aller Bibliotheken im Release, Patch-Liste mit Herkunft | S |
| P7.3 | Englische Nutzerdoku: Voraussetzungen (Firmware 13.00, ELF-Loader), Installation, Pairing, bekannte Grenzen | S |
| P7.4 | Release mit ELF, Prüfsummen und Changelog | S |
| P7.5 | Gleichzeitig die PS5-App im ProsperoStore veröffentlichen (P8.8, P8.9) | S |

Ist R2 negativ, erscheint 0.1 als Vorschau ohne Ton und benennt das deutlich.

## Nach v1.0

Erledigt seit 0.1.0: 60 fps (59,94 fps in der 30-Minuten-Sitzung), Moonlight-Discovery per mDNS, HEVC, Rumble und Lichtleiste.

Offen, in dieser Reihenfolge, jeweils mit eigenem Spike: weitere Firmware-Versionen (Checkliste `docs/firmware-porting.md`; ResourceArbitrator per NID, Rest live zu prüfen), Haptik geht über Moonlight nicht (`docs/re/adaptive-triggers.md`), eigener Modus je Gerät im Co-op. Mehrere Controller
gibt es seit 0.2.2. 1440p und 4K laufen seit dem 2026-10-31 (`STATUS.md`).

## Risikoregister

| Risiko | Wirkung | Wahrscheinlichkeit | Gegenmaßnahme |
| --- | --- | --- | --- |
| ~~Kein IDR auf Anforderung (R1)~~ | erledigt: `RequestKeyFrame` liefert IDR nach 2–3 Frames | – | – |
| ~~Kein Audio-Zugang (R2)~~ | erledigt: PCM über `OpenAudio` mit RA-Id 180 | – | – |
| ~~Boost-Teile bauen nicht (P0.2)~~ | erledigt: Boost 1.92 gebaut, ein Patch für Process v2 | – | – |
| ~~Encoder-Bitrate über 15 Mbit/s~~ | erledigt: Der Host setzt die Bitrate beim Öffnen des Encoders (`cfg+0xc8/0xcc/0xd0`) statt per `SetVideoBitrate` und klemmt auf 50 Mbit/s (`docs/re/hevc.md`) | – | – |
| Encoder-Last über 50 Mbit/s | Am Testclient mit Rauschbild bis rund 151 Mbit/s sauber, in einem Spiel oder mit echtem Moonlight nicht gemessen; Mario 64 erzeugte bei 100 Mbit/s nur rund 33 Mbit/s | unbekannt | Klemme auf 150 Mbit/s nur auf 13.00, sonst 50; das Nutzer-Setting von Moonlight entscheidet, wie viel ein Client anfragt |
| Firmware-Update ändert Offsets oder Strukturen | Capture fällt aus | sicher bei Update | ResourceArbitrator per NID mit Tabellenabgleich, Ablehnung bei Fehlschlag; Avcap2-Konfiguration und Pad je Version live prüfen (`docs/firmware-porting.md`) |
| Avcap2 im Spiel belegt oder gesperrt | Kein Bild in bestimmten Titeln | unbekannt | Früh mit regulärem Spiel testen (P1.4), Meldung statt Absturz |
| Zuordnungsdialog des virtuellen Pads | Bedienung blockiert | mittel | R5, einmalige Bestätigung vor Streambeginn |
| Upstream-Drift bei Sunshine | Aufwändige Updates | mittel | Fester Commit, kleine dokumentierte Patches, Updates nur gezielt |

## Meilensteine

0.2 und 0.3 laufen parallel: Bei 0.2 scheitern echte Nutzer, es hängt aber an Berichten von außen; 0.3 geht unabhängig davon voran.

| Meilenstein | Ziel | Fertig, wenn | Abhängig von |
| --- | --- | --- | --- |
| 0.2 Mehr Konsolen | Andere Firmwares, zuerst 10.40 und 13.60 aus den Issues | Mindestens eine weitere Firmware besteht die Checkliste in `docs/firmware-porting.md`. Stand: 10.20 besteht sie seit 0.1.7; 10.40 und 13.60 streamen laut Nutzerberichten, die Checkliste ist dort offen | Diagnoseberichten und Testern; hier läuft nur 13.00 |
| 0.3 Alltagstauglich | Der Host läuft zuverlässig, ohne Pflege | Ruhemodus mit Controller, offenem Spiel und laufendem Stream geprüft; Android und TV geprüft; Wake-on-LAN geprüft (die MAC kommt beim Client an); Fehlstarts geklärt (Windows-Client: 0 in 150); Dauertest über mehrere Stunden im Spiel; mehrere Controller im Spiel geprüft | Teils Nutzertests |
| 0.4 Mehr Bild | 1440p, 4K, HDR | Mindestens eines davon stabil im Stream, oder belegt, warum nicht. Stand: H.264 und HEVC in 1440p und 4K laufen auch unter Last (`STATUS.md`); es fehlt der aktive Spielbetrieb. HDR läuft im Testclient und mit einem echten Client am HDR-Fernseher | Reverse Engineering |
| 1.0 Stabil | Klare Zusage, was unterstützt ist | Liste geprüfter Firmwares und Clients; mehrstündige Sitzung in mindestens zwei Spielen ohne Ausfall; Ruhemodus belegt oder klar ausgeschlossen; Neuinstallation aus dem Store nach Anleitung, von jemandem ohne Vorwissen | Den Meilensteinen davor |

Bewusst nicht geplant: Haptik (Moonlight hat keinen Kanal dafür); harte Trigger nach dem Stream lassen sich nur in Moonlight beheben.

## Geplant: Tastatur und Maus als Controller

Übernimmt ein Mitwirkender (Discord).

**Tastatur und Maus als Controller.** Seit 0.2.1 kommen Tastatur und Maus als echte Tastatur und Maus an der Konsole an.
Das hilft beim Tippen, aber nur wenige Spiele nehmen eine Maus. Neu: eine Einstellung in der App (aus per Vorgabe), mit
der Tastatur und Maus eines Geräts stattdessen auf ein virtuelles DualSense gehen. Hat das Gerät schon ein Pad, laufen
beide auf dieses, damit kein weiterer Spieler entsteht; sonst legt der Host eins an. Vorgeschlagene Belegung:

| Taste | Controller |
| --- | --- |
| WASD | linker Stick |
| Maus | rechter Stick |
| linke / rechte Maustaste | R2 / L2 |
| Leertaste | Kreuz |
| Strg oder C | Kreis |
| R | Quadrat |
| F | Dreieck |
| Q / E | L1 / R1 |
| Shift / mittlere Maustaste | L3 / R3 |
| Pfeiltasten | Steuerkreuz |
| Enter / Backspace | Kreuz / Kreis (für Menüs) |
| Esc / Tab | Options / Touchpad |
| Pos1 | PS-Taste (gilt die PS-Sperre aus 0.3.0, also nur für Spieler 1) |

Offen: wie die Mausbewegung in Stickausschlag umgerechnet wird (Empfindlichkeit, Totzone, Abklingen, wenn die Maus
stillsteht); das wird beim Testen abgestimmt. Prüfen mit dem Testclient (`--kbm-seq`), das Gefühl beim Zielen am Ende mit
echtem Moonlight beim Besitzer.

## Umsetzungsplan vom 28. Oktober 2026 (früher `PLAN.md`)

Der frühere Inhalt von `PLAN.md`, unverändert bis auf die eine Ebene tiefer gesetzten Überschriften. Verweise wie „`PLAN.md` M1“ oben meinen die Meilensteine hier.

Stand: 28. Oktober 2026. Dieses Dokument beschreibt Ziel, technischen Ansatz und verbindliche Regeln. Die konkreten Arbeitspakete, ihre Reihenfolge und Abnahmen stehen in `ROADMAP.md`. Im Projektordner ist bislang kein Streaming-Host implementiert.

### Ziel

ProsperoSunshine soll direkt auf einer PS5 einen Sunshine-kompatiblen Streaming-Host bereitstellen. Ein unveränderter Moonlight-Client verbindet sich mit der PS5, empfängt Bild und Ton und überträgt Controller-Eingaben zurück. Ein PC darf für Entwicklung und Diagnose nötig sein, soll im späteren Streaming-Betrieb aber keine Vermittlerrolle übernehmen.

Der erste vollständige Funktionsumfang umfasst einen Client im lokalen Netzwerk, H.264 mit 1920 × 1080 in SDR, Stereo-Audio und einen Controller. Zuerst werden dauerhaft 30 FPS nachgewiesen; 60 FPS sind ein anschließendes Mess- und Optimierungsziel. Beides sind Entwicklungsziele, keine bereits gemessenen Eigenschaften des vorhandenen Probes. Manuelles Hinzufügen der PS5-IP in Moonlight reicht für den ersten Meilenstein.

HDR, HEVC, 4K, Internetzugriff, mehrere Clients, mehrere Controller, Mikrofon, Rumble und adaptive Trigger folgen erst nach dem stabilen Grundbetrieb.

### Gesicherter Ausgangspunkt

Die Vorarbeit liegt unter `../ps5-mcp` und wurde auf einer PS5 mit Firmware 13.00 geprüft.

| Bereich | Nachgewiesen | Noch offen |
| --- | --- | --- |
| Bildschirmaufnahme | Avcap2 liefert decodierbares H.264 mit 1920 × 1080, einschließlich sichtbarem Home-Menü und Homebrew. | Dauerbetrieb, tatsächliche Bildrate, Latenz, Verhalten in kommerziellen Spielen. |
| Schwarze Bilder | Die Quellenauswahl bei Konfigurationsoffset `0x2A4` wurde von `3` auf `2` geändert. Damit wird der zusammengesetzte Bildschirm erfasst. | Verhalten bei weiteren Ausgabe- und Spielsituationen. |
| Capture-Datei | Der letzte geprüfte Lauf lieferte 100 decodierbare Frames und 291.551 Bytes einschließlich SPS/PPS. | Das ist ein kurzer Funktionsnachweis, kein Leistungsnachweis. |
| Ressourcen | Profil 180 lässt sich registrieren, anfordern und sauber freigeben. | Dauerhafte Konfliktbehandlung und Wiederanlauf im Host. |
| Eingabe | RemoteEye kann über ein virtuelles Pad Tasten, Sticks und Trigger an die PS5 senden. | Übersetzung der Moonlight-Pakete und Absicherung bei Verbindungsabbruch. |
| Audio | Kein integrierter PS5-Audiostream nachgewiesen. | Capture-Schnittstelle, PCM-Format, Opus und Synchronisation. |
| Moonlight | Noch keine Verbindung mit ProsperoSunshine implementiert. | Pairing, Sitzungsaufbau, Video-/Audio-Transport und Steuerkanal. |

Der Avcap2-Probe ist eigenständig. Der bestehende MCP-Befehl für Screenshots verwendet weiterhin die Galerie; ein neuer produktiver MCP-Screenshot-Backend ist damit noch nicht fertiggestellt. Die zehn MCP-Selbstprüfungen bestanden nach den Capture-Versuchen.

Lokale Referenzen:

- Capture und vollständiges Konfigurationslayout: `../ps5-mcp/payload/avcap_probe/avcap_probe.c`
- Build: `../ps5-mcp/payload/avcap_probe/build.sh`
- Recherche: `../ps5-mcp/docs/how-it-works.md`
- Pad-Implementierung: `../ps5-mcp/payload/RemoteEye/Program.cs`
- Sichtprüfung: `../ps5-mcp/payload/avcap_probe/out/verified-screen.png`

### Technischer Ansatz

Sunshine ist bereits ein Host für Moonlight. Für ProsperoSunshine sollen möglichst dessen Protokollbausteine übernommen und um PS5-spezifische Aufnahme und Eingabe ergänzt werden. Die genaue Grenze zwischen Übernahme und Anpassung wird zuerst durch einen Build- und Abhängigkeitstest festgelegt. Die vorhandene FreeBSD-Unterstützung von Sunshine beweist keine Kompatibilität mit der PS5-Laufzeit. [Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/)

Die native Anwendung besteht aus einem Sitzungsmanager, einem Protokollteil und einer kleinen PS5-Plattformschicht. Der vorhandene Python-MCP-Server dient beim Entwickeln zum Deployment und zur Diagnose. Er transportiert im fertigen Host keine Medien.

```text
PS5 Avcap2 → H.264-Frames → begrenzte Warteschlange → Video-Transport → Moonlight
PS5 Audio → PCM → Opus → Audio-Transport → Moonlight
PS5 virtuelles Pad ← Eingabeübersetzung ← Steuerkanal ← Moonlight
                        Sitzungsmanager und gemeinsame Zeitbasis
```

Avcap2 liefert bereits komprimiertes Video. Der Host soll diese Frames direkt verpacken. Ein erneutes Decodieren und Encodieren wäre zusätzliche Last und Latenz. Ob dafür Sunshine-Paketschnittstellen unverändert verwendbar sind und welche FFmpeg-Abhängigkeiten verbleiben, muss am gewählten Upstream-Stand geprüft werden. [Videoschnittstellen](https://github.com/LizardByte/Sunshine/blob/master/src/video.h)

#### Wiederverwendung prüfen

| Upstream-Bereich | Aufgabe im Projekt |
| --- | --- |
| `nvhttp.cpp` | Hostinformationen, Pairing, Anwendungsliste und Sitzungsaktionen. |
| `rtsp.cpp` | Aushandlung der Sitzung und ihrer Medienparameter. |
| `stream.cpp` | Video-/Audio-Paketierung, Steuerkanal, Fehlerkorrektur und ausgehandelte Verschlüsselung. |
| `input.cpp` | Empfang und Auswertung von Eingaben; PS5-Ausgabe separat anbinden. |
| `audio.cpp` | Opus-Verarbeitung und Zuordnung zur Sitzung; PS5-Aufnahme ersetzen. |

Quellen: [Pairing und HTTP](https://github.com/LizardByte/Sunshine/blob/master/src/nvhttp.cpp), [RTSP](https://github.com/LizardByte/Sunshine/blob/master/src/rtsp.cpp), [Streaming](https://github.com/LizardByte/Sunshine/blob/master/src/stream.cpp), [Audio](https://github.com/LizardByte/Sunshine/blob/master/src/audio.cpp).

Vor dem ersten Port einen konkreten Sunshine-Commit und dessen Unterprojekte festhalten. Der derzeitige Master verlangt C++23; Compiler, Standardbibliothek und Thread-Unterstützung müssen deshalb auf dem PS5-Toolchain-Pfad praktisch geprüft werden. Ebenso früh die tatsächlich benötigten Teile von OpenSSL, Boost, ENet, Opus, Fehlerkorrektur und FFmpeg erfassen. Upstream kennzeichnet das Projekt als `GPL-3.0-only`; Lizenztexte, Herkunft und Änderungen bei übernommenem Code dokumentieren. [Builddefinition](https://github.com/LizardByte/Sunshine/blob/master/CMakeLists.txt)

### Verbindliche Regeln für das PS5-Backend

- Zunächst ausschließlich Firmware 13.00 unterstützen und vor Verwendung der gemessenen Moduloffsets prüfen. Andere Versionen kontrolliert ablehnen.
- Das funktionierende Avcap2-Layout mit `0x2E0` Bytes übernehmen. Ältere SharpProspero-Strukturen mit `0x2B8` Bytes passen nicht unverändert.
- `0x2A4 = 2` für den bisher bestätigten zusammengesetzten SDR-Bildschirm beibehalten. Wert `3` erfasste nur die App und führte im Home-Menü zu schwarzen Bildern.
- Profil-ID 180 ist eine ResourceArbitrator-Ressource, keine Benutzer-ID. Wenn sie bereits belegt ist, den Start ablehnen und die Belegung melden.
- Beim Anfordern auf die Freigabe warten, Zeitüberschreitung begrenzen und einen späteren Ressourcenentzug behandeln. Keine fremde Sitzung oder Systemprozesse beenden.
- Speicherverwaltung zunächst beim Dienst belassen. Externe Frame-Puffer sind für den vorhandenen Nachweis nicht erforderlich.
- Alle Erfolgs- und Fehlerpfade müssen eigene Channels stoppen und schließen, Avcap2 terminieren und die eigene Ressourcenregistrierung freigeben. Bei Teilinitialisierung nur tatsächlich erworbene Ressourcen aufräumen.
- Bei Disconnect oder Sitzungsfehler alle gedrückten Eingaben freigeben und das eigene virtuelle Pad sauber lösen.

Für `sceAvcap2ReadVideo` gilt im Probe: positiver Rückgabewert ist die kopierte Bytezahl, `0x81950002` bedeutet noch kein Frame. Der Descriptor ist `0x70` Bytes groß. SPS/PPS kommen zusätzlich über `sceAvcap2GetNonVclNalUnitStream`; im Diagnoseformat werden sie mit Annex-B-Startcodes vorangestellt. Weitere ABI-Details aus der lokalen C-Datei übernehmen, statt sie neu zu erraten.

### Umsetzung in Meilensteinen

#### M0: Build und Abhängigkeiten nachweisen

Ein minimales natives Projekt mit reproduzierbarem Build und Logausgabe anlegen. Den vorhandenen SDK-Pfad als Ausgangspunkt nutzen; der Probe wurde mit PS5 Payload SDK v0.45 gebaut. C++23, Threads, monotone Uhr, Sockets und die benötigten Bibliotheken jeweils auf der Konsole erproben. Danach in einer kurzen Architekturentscheidung festhalten, welche Sunshine-Teile direkt gebaut und welche Schnittstellen ersetzt werden.

**Abnahme:** Ein ELF startet auf der PS5 und führt die benötigten Laufzeit- und Bibliotheksproben erfolgreich aus. Toolchain-Versionen und Upstream-Commits sind dokumentiert. Falls eine zentrale Abhängigkeit scheitert, ist die Alternative vor dem großen Protokollport entschieden.

#### M1: Capture als dauerhafte Komponente

Den funktionierenden Probe in eine `CaptureSession` mit explizitem Start, Lesen und Stop zerlegen. Das bisherige kurze Aufnahmefenster durch einen abbrechbaren Dauerbetrieb ersetzen. Frame-Grenzen, Größen, SPS/PPS, Zeitstempel, erkennbare Keyframes und Fehler erfassen. Die Bedeutung der Capture-Zeitstempel und des Timing-Feldes separat untersuchen.

Bitrate, Bildrate und erzwungenes IDR-Bild sind eigene Prüfpunkte. Ein funktionierender SPS/PPS-Aufruf beweist keine Möglichkeit, einen neuen Keyframe anzufordern. Erforderliche Encoderfunktionen müssen gemessen und anschließend über eine klare Schnittstelle angeboten werden.

**Abnahme:** Mindestens zehn Minuten decodierbares Video aus Home-Menü und Homebrew, gemessene Frame-Abstände und Speicherverbrauch, fünf saubere Start-/Stop-Zyklen. Ein einfacher Diagnoseempfänger darf dazu dienen; dieser Zwischenstand ist noch keine Moonlight-Kompatibilität.

#### M2: Pairing und Sitzungsaufbau

Hostidentität und Zertifikate dauerhaft speichern. Serverinformationen, Pairing und eine feste Anwendung „PS5-Bildschirm“ anbieten. Diese Anwendung bedeutet den aktuellen PS5-Bildschirm, nicht das Starten eines PC-Desktops. Launch, Resume und Cancel sowie die RTSP-Aushandlung implementieren. Nur tatsächlich unterstützte Medienmodi anbieten; zunächst einen gepairten Client gleichzeitig zulassen.

Die vorhandenen Upstream-Authentifizierungs- und Verschlüsselungsabläufe erhalten. Medien und Eingaben erst für eine autorisierte Sitzung freigeben. Nicht unterstützte Modi oder eine zweite Sitzung mit verständlichem Fehler ablehnen.

**Abnahme:** Ein unveränderter Moonlight-Client kann die PS5 manuell hinzufügen, pairen und die Bildschirm-Anwendung starten. Ein ungepairter Client kann keinen Stream öffnen. Ein Hostneustart erhält die autorisierte Clientidentität.

#### M3: Video in Moonlight

Die Avcap2-Frames an den übernommenen Transport anbinden. Paketgrößen, Sequenzen, Zeitstempel, Frame-Markierungen, Fehlerkorrektur, Steuerkanal und ausgehandelte Verschlüsselung gemäß dem festgehaltenen Upstream-Stand behandeln. Start und Wiederaufnahme benötigen einen decodierbaren Einstieg mit Parameter-Sets und geeignetem Keyframe.

Die Warteschlange zeitlich und nach Speicher begrenzen. Bei Rückstau keine beliebigen Bytes oder einzelne Referenzteile entfernen: vollständige Frames verwerfen, die Abhängigkeiten berücksichtigen und nötigenfalls bis zu einem neuen Keyframe synchronisieren. Den Wiederherstellungsweg zunächst nachweisen und erst dann Überlastbehandlung darauf aufbauen.

**Abnahme:** Moonlight zeigt Home-Menü und Homebrew kontinuierlich. Verbindungsabbruch und erneuter Start liefern wieder ein Bild. Paketverlust und künstlicher Rückstau führen zu begrenzter Verzögerung oder einem kontrollierten Sitzungsende.

#### M4: Controller anbinden

Die bewährten RemoteEye-HID-Strukturen als Referenz für eine native Pad-Komponente verwenden. Moonlight-Tasten, Sticks und Trigger übersetzen, Wertebereiche und Totzonen prüfen und PS-Taste beziehungsweise deren verfügbare Clientbelegung dokumentieren. Benutzerzuordnung ausdrücklich behandeln: Ein neuer virtueller Controller kann einen Zuordnungsdialog auslösen. Keine pauschale Bestätigung während eines laufenden Spiels senden.

Ein Watchdog neutralisiert das Pad nach ausbleibender Eingabe oder Verbindungsverlust. Gleichzeitige schreibende MCP-Eingaben während einer Streaming-Sitzung verhindern oder über dieselbe Eingabeinstanz koordinieren.

**Abnahme:** Menünavigation und ein Spiel sind mit einem Moonlight-Controller bedienbar. Tests mit gehaltenen Tasten und anschließendem Netzwerkausfall hinterlassen keine gedrückten Eingaben.

#### M5: Audio und Synchronisation

Die PS5-Audioaufnahme zunächst unabhängig nachweisen: Schnittstelle, Zugriffsrechte, Sample-Format, Kanäle, Rate und Zeitstempel erfassen. Falls Audio ebenfalls Ressourcen benötigt, deren Zusammenspiel mit Video vor der Integration prüfen. Den Capture-Pfad anschließend an den vorhandenen Opus- und Audiotransport anbinden. Stereo bei 48 kHz ist das erste Ausgabeziel; Paketdauer passend zur Aushandlung behandeln.

Audio und Video auf eine gemeinsame monotone Zeitbasis beziehen. Drift über längere Sitzungen messen und die Korrektur festlegen. Ein stummer Stream kann M3 testen, erfüllt aber nicht den vollständigen Funktionsumfang.

**Abnahme:** Spielton erreicht Moonlight verständlich und ohne dauerhafte Aussetzer. Bei einem reproduzierbaren Bild-/Ton-Ereignis bleibt der Versatz während 30 Minuten stabil; Messverfahren und tolerierter Grenzwert sind vor dem Test festgelegt.

#### M6: Stabilität und Bedienbarkeit

Alle Komponenten über einen gemeinsamen Sitzungszustand steuern: bereit, Aufbau, aktiv, Abbau und Fehler. Trennung, Clientabsturz, belegte Capture-Ressource, Ressourcenentzug und Teilfehler testen. Begrenzte Logs mit Sitzungs-ID, Bildrate, Bitrate, Warteschlangendauer, Paketverlust, Audiofehlern und Abbaugrund bereitstellen; keine Schlüssel oder vollständigen Pairing-Geheimnisse protokollieren.

**Abnahme:** Eine 30-Minuten-Sitzung mit Bild, Ton und Controller, zehn erfolgreiche Wiederverbindungen sowie fünf vollständige Host-Start-/Stop-Zyklen. Kein fortlaufender Speicheranstieg, keine hängen gebliebenen Eingaben und keine eigene Capture-Ressource nach dem Abbau. Anschließend denselben Ablauf mit einem zweiten Moonlight-Client auf einer anderen Plattform prüfen, nacheinander.

### Messungen und praktische Tests

1. Zunächst per LAN testen und Clientversion, Auflösung, angeforderte Bildrate und Bitrate festhalten.
2. Home-Menü, Homebrew und mindestens ein reguläres Spiel prüfen. Die bisherigen Capture-Ergebnisse belegen das Spielverhalten noch nicht.
3. Tatsächlich empfangene und dargestellte Frames messen. SPS-Metadaten allein sind kein Nachweis für eine stabile Bildrate.
4. Capture-, Queue- und Transportzeiten getrennt erfassen. Ende-zu-Ende-Latenz über eine geeignete gemeinsame Messung bestimmen, etwa eine schnelle Kamera mit lokalem und entferntem Bild; Zeitstempel verschiedener Geräte nicht ungeprüft voneinander abziehen.
5. Paketverlust, Bandbreitenbegrenzung, Kabelziehen und Clientabbruch gezielt auslösen. Wiederherstellung und Ressourcenfreigabe kontrollieren.
6. Erst nach bestandenem Grundbetrieb 60 FPS erproben und Engpässe anhand der Messwerte bearbeiten.

Die vorhandenen MCP-Werkzeuge unterstützen Deployment, Logs und Zustandsprüfungen. Galerie-Screenshots eignen sich zur Sichtkontrolle, nicht zur Streaming-Latenzmessung.

### Vorgeschlagene Projektstruktur

Die folgende Struktur ist ein Vorschlag für die Implementierung und noch nicht angelegt:

```text
src/
  main.cpp
  session/          Sitzungszustand, Konfiguration, Identität und Abbau
  protocol/         Adapter zu den übernommenen Sunshine-Bausteinen
  platform/ps5/     Avcap2, ResourceArbitrator, Audio und virtuelles Pad
third_party/        Festgehaltene Upstream-Stände beziehungsweise Build-Verweise
scripts/            Reproduzierbarer Build und Deployment
tests/              Paketfixtures, Fehlerpfade und Eingabeübersetzung
docs/               ABI-Erkenntnisse, Architekturentscheidung und Messprotokolle
```

Die Firmware-spezifischen Offsets in genau einer Plattformdatei bündeln. Protokollcode soll weder Moduloffsets noch HID-Layouts kennen. Tests besonders für Paketparser, Grenzwerte, Sitzungswechsel und Cleanup schreiben; Capture-Qualität und Konsolenressourcen zusätzlich auf echter Hardware prüfen.

### Erste konkrete Arbeitspakete

1. M0 durchführen und eine Entscheidung zum Umfang der Sunshine-Übernahme dokumentieren.
2. Die funktionierende Capture-Konfiguration unverändert in ein natives Daueraufnahme-Modul übernehmen und dessen Lebenszyklus absichern.
3. Parallel im technischen Ablauf, aber vor dem vollständigen Streaming-Port, die beiden größten offenen Plattformfragen klären: Keyframe-Anforderung und Audio-Capture. Scheitert einer dieser Punkte, den erreichbaren Zwischenstand und die Einschränkung ausdrücklich dokumentieren.
4. Danach Pairing und Video bis zum ersten echten Moonlight-Bild umsetzen, Controller und Audio anbinden und die Abnahmetests abschließen.

Der erste vollständige Stand ist erst erreicht, wenn ein unveränderter Moonlight-Client die PS5 selbstständig pairen und einen Stream mit Bild, Ton und Controller öffnen kann, der ohne PC-Vermittler läuft und nach Verbindungsende alle eigenen Ressourcen freigibt.
