# Firmware-Matrix

Stand: 10. November 2026, Host 0.6.0. Was auf welcher Firmware nachgewiesen ist, mit dem Nachweis. Geprüft sind nur 13.00
und 10.20 (`STATUS.md`, „Kurzfassung“); alles andere sind Berichte anderer oder Vermutungen. Was am Host firmwareabhängig ist
und wie eine neue Version geprüft wird: `docs/firmware-porting.md`. Die Sonde für eine unbekannte Firmware:
`docs/firmware-probe.md`.

## Zustände

Jede Zelle hat genau einen von vier Zuständen:

- **geprüft:** hier gemessen, mit dem Weg in `STATUS.md` oder im Archiv (`docs/status-archive.md`). Auf 10.20 stammen die
  Messungen von der PS5 Pro eines Mitwirkenden, die das Projekt als Prüfkonsole führt.
- **Bericht:** jemand anderes hat es gesehen; dahinter steht, womit (Log, Diagnosebericht, Sondenbericht oder nur die Aussage).
  Ein Bericht kann positiv oder negativ sein. Er wird nicht zu „geprüft“, auch wenn er gut belegt ist.
- **ungeprüft:** niemand hat es berichtet. Was der Host dort tut, folgt nur aus dem Code.
- **geht nicht:** mit dem Fehlercode aus einem Log. Ohne Code ist ein Fehlschlag ein negativer Bericht.

## Matrix

Die Firmware steht so da, wie `kernel_get_fw_version()` sie liefert. „Datensatz“ ist die Größe des Video-Datensatzes, den der
Host dort schickt (`record_shift` in `capture.c`); „Vermutung“ heißt, dass er auf dieser Version nie gemessen wurde.

| Firmware | Host startet | Bild H.264 | HEVC | HDR | Modi über / unter 1080p | Bitrate | Ton Stereo | Raumklang | Eingabe | App | Nachweis |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4.00 (`0x4000042`), Datensatz `0x2c0` (Vermutung) | Bericht (Diagnosebericht 0.5.0: `host ready`, Pairing) | geht nicht: `OpenVideo` `0x81950009` mit dem Datensatz von 9.60 (0.5.0), `0x81950001` mit dem von 13.00 (0.2.4) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Bericht (Diagnosebericht aus der App) | Issue 40, Nutzer mit Diagnosebericht |
| 8.40 (`0x8400005`), Datensatz `0x2c0` (Vermutung) | Bericht (Log 0.2.4: `host ready`, Pairing) | geht nicht: `OpenVideo` `0x81950008` mit dem Datensatz von 13.00 (0.2.4), Ressource 180 aktiv. Mit `0x2c8` (Testbuild `v0.3.1-fw840-1`) und `0x2c0` (ab 0.4.1) ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Bericht (Log: `game audio via Avcap2`, ob Ton ankam, steht nicht da) | ungeprüft | ungeprüft | Bericht (App 01.000.015 installiert) | Issue 23, Nutzer mit Hostlog |
| 9.60 (`0x9600004`, nicht Pro), Datensatz `0x2c0` | Bericht (Mitwirkender, Fork auf 0.4.1, Kernel-Log) | Bericht (Sonde; 1080p60 mit Moonlight für Android, Fork auf 0.3.4) | Bericht (Sonde 8 und 10 Bit; Stream im Spiel, Fork auf 0.4.1); im Host nur mit Schalter | Bericht (2 Minuten HEVC HDR im Spiel, Fork auf 0.4.0) | über: Bericht (Sonde: 1440p ja, im Spiel Rückfall auf 1080p); 2160p geht nicht: `0x81950008` bei `video out mode` `0x34`. Unter: Bericht (Sonde: 360p bis 720p) | Bericht (Sonde: keine Rate-Felder, QP in `0xac`/`0xb0` wirkt, `SetVideoBitrate` wirkungslos bis 12 Mbit/s, ab 16 Kanal weg) | Bericht (Stream mit Ton, Fork) | ungeprüft | Bericht (Controller, Fork; Trigger mit Artemis) | Bericht (die App sieht `/data` nicht; Brücke `FILE`/`INSTALL` im Fork geprüft, seit 0.4.1 im Host) | `STATUS.md` „Firmware-Sonde auf 9.60“, Issue 30, Mitwirkender mit Sondenbericht und Logs |
| 10.20 (`0x10200006`, Pro), Datensatz `0x2c8` | geprüft | geprüft (34 Slices je 1080p-Bild; Hardware-Decoder unter Windows zeigen kein Bild, Software geht) | geprüft (8 Bit) | geprüft | über: Bericht (Sonde: alle Modi bis 2160p liefern Bilder), im Host nur mit Schalter. Unter: geprüft | geprüft: keine Rate-Felder, der QP folgt der Bitrate (`encoder_qp.h`) | geprüft | ungeprüft | geprüft (Pads, Tastatur-Pad in GTA III) | geprüft | Archiv „HEVC auf Firmware 10.20“, „Modi unter 1080p auf Firmware 10.20“, „Quantisierer auf 10.x folgt der Bitrate“; `STATUS.md` „Debug-Build (Issue 28 …)“, „Tastatur-Pad nach dem Review von PR 20“ |
| 10.40 (`0x10400007`, nicht Pro), Datensatz `0x2c8` | Bericht (Diagnosebericht, Issue 2) | Bericht (Sonde; Mitwirkender streamte rund zwei Stunden über Tailscale) | Bericht (Sonde 8 und 10 Bit; Nutzer in Issue 2 streamt HEVC); im Host nur mit Schalter | Bericht (Mitwirkender: HEVC HDR blieb nach einer Weile hängen, Ursache offen) | Bericht (Sonde: alle zwölf Modi von 360p bis 2160p); im Host nur mit Schalter | Bericht (Sonde: QP in `0xc4`/`0xc8` wirkt, `SetVideoBitrate` beendet den Kanal mit `0x80c78000`) | Bericht (Sonde: PCM und Opus) | ungeprüft | ungeprüft | Bericht (Diagnosebericht, Autoload) | `STATUS.md` „Firmware-Sonde auf 10.20 und 10.40“; Archiv „Berichte von Testern zu 0.1.7“; Issue 2 |
| 11.20 (Slim), Datensatz `0x2e0` | Bericht: startet bei einem Nutzer (Issue 37), bei einem anderen nicht, ohne Log (Issue 51) | Bericht (Issue 37: Bild über Tailscale, ohne Log, Codec nicht genannt) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Bericht: kein Ton in allen Spielen (Issue 37, ohne Log) | ungeprüft | Bericht (Issue 37: Eingaben kommen an) | ungeprüft | Issues 37 und 51, Nutzer ohne Log |
| 11.40, Datensatz `0x2e0` | Bericht (Diagnosebericht, Issue 24) | Bericht (streamt laut Nutzer, nachdem ps5-tailscale UDP 47999 nicht mehr hielt; ohne Log) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Bericht (Diagnosebericht aus der App) | `STATUS.md` „Belegter Steuerport“, Issue 24 |
| 12.40, Datensatz `0x2e0` | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Nur die Größe `0x2e0` aus dem Log eines anderen Projekts (`docs/firmware-porting.md`) |
| 12.70 (`0x12700001`), Datensatz `0x2e0` | Bericht (Diagnosebericht 0.2.3) | ungeprüft (der Client nahm HEVC) | Bericht (Diagnosebericht: Schalter, `variant=minimal`, `rc=0`, erstes Bild; Nutzer: „fast fehlerfrei“) | ungeprüft (Schalter an, nicht versucht) | über: ungeprüft (1440p verlangt, ohne Schalter 1080p geliefert). Unter: ungeprüft | Bericht (44,6 Mbit/s beim Öffnen; 150 nicht versucht) | Bericht (Log: `game audio via Avcap2`, Opus 2 Kanäle) | ungeprüft | ungeprüft | Bericht (Diagnosebericht aus der App) | Issue 17, Nutzer mit Diagnosebericht |
| 13.00 (`0x13000040`), Datensatz `0x2e0` | geprüft | geprüft | geprüft | geprüft | geprüft (640x360 bis 4K; im Spiel verweigern manche Titel den großen Modus) | geprüft (bis 150 Mbit/s) | geprüft | geprüft nur der Weg (5.1/7.1-Stream); die Konsole im Test gibt Stereo aus, Inhalt ungeprüft | geprüft | geprüft | `STATUS.md` „Kurzfassung“ und die Release-Einträge; Archiv „Modi unter 1080p (2026-11-02, 13.00)“, „HDR“; „Raumklang folgt einem breiteren Stream“ |
| 13.40 (`0x13400002`, Slim Digital), Datensatz `0x2e8` | Bericht (Log) | Bericht (Testbuild `0.3.1-fw1340-1`: `OpenVideo` 0, `Start` 0, Bild auf Android) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | Bericht (Log: `OpenAudio` 0, `audio Start` 0) | ungeprüft | ungeprüft | Bericht (nur zum Pairing benutzt) | Issue 14, Nutzer mit Log; `STATUS.md` „Berichte nach 0.3.1“ |
| 13.60 (`0x13600007`), Datensatz `0x2e8` | Bericht (Diagnosebericht) | Bericht (ab 0.1.5 „confirmed working“) | Bericht (Pro: 10 Bit 1080p `variant=minimal`, `rc=0`) | Bericht (Nutzer: 4K HDR mit 150 Mbit/s am 4K-Fernseher, ohne Bericht) | über: geht nicht bei Ausgabe 1080p: `OpenVideo` `0x81950008` bei `video out mode` `0x10` (Rückfall auf 1080p); am 4K-Fernseher laut Nutzer 4K, ohne Log. Unter: ungeprüft | Bericht (150 Mbit/s laut Nutzer; fehlende Bilder bei 80 und 150 mit `min_log_level = verbose`) | Bericht (PR 16, Messung des Mitwirkenden) | Bericht: nur Stereo, Kanäle 2–11 exakt 0 bei 5.1-LPCM-Ausgabe, Fernseher meldet 2 Kanäle LPCM (Issue 18, Messung mit Rohdaten) | Bericht (DualSense, adaptive Trigger in Astro Bot, ohne Log) | Bericht (Diagnosebericht aus der App) | Issues 1 und 18, PR 16; Archiv „Berichte von Testern zu 0.1.7“; `STATUS.md` „Raumklang auf 13.60“ |
| 14.00, 14.10, Datensatz `0x2e0` (Vermutung) | ungeprüft (das SDK kennt die Kernel-Offsets) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | keiner |
| neuer als 14.10 | ungeprüft; laut Code des SDK endet der Host vor seiner ersten Zeile (unten) | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | ungeprüft | keiner |

Die übrigen 4.x-, 8.x- und 9.x-Versionen bekommen denselben Datensatz wie 4.00 und 8.40 (`0x2c0`), 10.00 bis 10.60 den von
10.20, 11.x bis 13.20 den von 13.00, 13.40 bis 13.99 den von 13.60; keine davon ist berichtet.

## Was vor dem Host scheitern kann: das SDK

Der Host ist mit dem PS5 Payload SDK v0.45 gebaut (Image `prosperosunshine-sdk:v0.45`). Dessen Startcode (`__crt_start` in
`crt1.o`) holt vor `main` die Kernel-Offsets der Firmware (`__kernel_init`). Kennt er die Version nicht (Schlüssel
`Version & 0xffff0000`), gibt `__kernel_init` −78 zurück und der Payload endet, bevor das Kernel-Log eingerichtet ist: keine
Zeile im Kernel-Log, kein `host_diag.log`. Die App meldet dann nur, dass der Host nicht innerhalb von 30 s hochkam. Gelesen
im Disassemblat von `crt1.o` aus dem Image, nicht auf einer Konsole beobachtet.

Das SDK v0.45 kennt: 1.00, 1.01, 1.02, 1.05, 1.10, 1.11, 1.12, 1.13, 1.14, 2.00, 2.20, 2.25, 2.26, 2.30, 2.50, 2.70, 3.00,
3.10, 3.20, 3.21, 4.00, 4.02, 4.03, 4.50, 4.51, 5.00, 5.02, 5.10, 5.50, 6.00, 6.02, 6.50, 7.00, 7.01, 7.20, 7.40, 7.60, 7.61,
8.00, 8.20, 8.40, 8.60, 9.00, 9.05, 9.20, 9.40, 9.60, 10.00, 10.01, 10.20, 10.40, 10.60, 11.00, 11.20, 11.40, 11.60, 12.00,
12.02, 12.20, 12.40, 12.60, 12.70, 13.00, 13.20, 13.40, 13.42, 13.60, 14.00, 14.10. Alle Versionen der Matrix sind dabei; 11.20
nutzt dieselben Offsets wie 11.00, 11.40 und 11.60.

Danach kann der Startcode noch scheitern, wenn eine Systembibliothek oder ein importiertes Symbol fehlt („Unable to load“,
„Unable to resolve“); das steht im Kernel-Log, aber nicht in `host_diag.log`, weil `main` nicht erreicht wird.

## Nächste Schritte je Firmware

- **4.00 (Issue 40):** Der Datensatz von 9.60 kommt am Validator vorbei (kein `0x81950001` mehr), `OpenVideo` meldet dann
  `0x81950009` (laut `docs/re/avcap2-exports.md`: Kanal beendet oder entzogen), bei allen vier Versuchen in 11 s, mit und
  ohne die Slice-Felder. Das ist kein Layout-Fehler im Sinn
  des Validators; welcher Zweig es ist, sagt nur die Sonde. Nächster Schritt: ein Lauf von `firmware_probe.elf` (Phasen
  `symbols`, `layout`, `purposes`), Bericht ins Issue. Ohne den keine Codeänderung.
- **8.40 (Issue 23):** Auf beide Testbuilds (`0x2c8`, dann `0x2c0` ab 0.4.1) kam keine Antwort. Nächster Schritt: ein
  Stream mit 0.6.0 und `host_diag.log`; scheitert er, ein Lauf der Sonde.
- **9.60 (Issue 30):** Der Mitwirkende testet die Releases. Offen: ein Bericht mit dem unveränderten Host (bisher nur der
  Fork), die Webseite, die Abschaltungen der Konsole. Danach Checkliste Schritte 8 und 9 (`docs/firmware-porting.md`).
- **10.20:** Modi über 1080p mit Schalter im Spiel; Slices unter H.264 (34, über dem, was Windows-Decoder nehmen).
- **10.40:** Bericht mit dem aktuellen Host im Spiel; Eingabe; der Hänger bei HEVC HDR.
- **11.20 (Issues 51 und 37):** Für 51 der Diagnosebericht und wie der Host gestartet wird; ab dem nächsten Release nennt
  `host_diag.log` belegte und nicht lauschende Ports. Für 37 die Zeilen `PS5 audio:` aus `sunshine.log` (kein Ton).
- **11.40:** ein Diagnosebericht nach einem Stream mit dem aktuellen Host.
- **12.40, 14.00, 14.10:** kein Bericht. Erster Schritt wäre ein Stream mit Diagnosebericht.
- **12.70:** H.264, HDR und 1440p/4K mit Schalter, jeweils mit Diagnosebericht.
- **13.40:** Eingabe, HEVC mit Schalter, längere Sitzung.
- **13.60 (Issue 18):** Raumklang braucht ein Ausgabegerät, das 6 oder 8 Kanäle LPCM meldet; ohne das keine Änderung am
  Host. Modi über 1080p mit Diagnosebericht an einem 4K-Fernseher.
- **Neuer als 14.10:** erst ein SDK, das die Version kennt. Bis dahin kann der Host dort nichts loggen; die App könnte es
  anhand der Firmware selbst sagen (noch nicht umgesetzt).
