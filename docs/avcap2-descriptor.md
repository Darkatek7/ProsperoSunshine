# Avcap2: Frame-Descriptor von `sceAvcap2ReadVideo` (FW 13.00)

Stand: 28. Oktober 2026. Gemessen mit `src/probes/capture_soak.cpp` (fünf 5-s-Zyklen und ein 10-min-Lauf im Home-Menü, 1080p H.264). Der Descriptor ist 0x70 Bytes groß; Wort 0 ist die Größe, die der Aufrufer vorbelegt. Die Tabelle nennt 64-Bit-Wörter in Little-Endian.

| Wort | Offset | Beobachtung | Deutung | Sicherheit |
| --- | --- | --- | --- | --- |
| w1 | 0x08 | `w1` eines Frames ist `w2` des nächsten; steigt je Bild um 4004 | Präsentationszeit in Ticks eines 240-kHz-Takts (siehe unten) | sicher (Takt), Deutung laut ps5-web-stream |
| w2 | 0x10 | `w1 − w2` = 4004 | Decodierzeit in denselben Ticks | sicher (Takt), Deutung laut ps5-web-stream |
| w3 | 0x18 | obere Hälfte `1` bei IDR, `3` bei P-Frames | Bildtyp | sicher (60 IDR-Stichproben) |
| w4 | 0x20 | 1, 2, 3, … je Sitzung | Framezähler (1-basiert) | sicher |
| w5 | 0x28 | steigt alle 60 Frames um 1, auch über Sitzungen hinweg | GOP-Zähler des Encoders | sicher |
| w6 | 0x30 | Abstand ≈ 16,66 · 10⁶ je Frame, läuft über | untere 32 Bit eines ns-Zeitstempels | Vermutung |
| w7 | 0x38 | `0x780` in der oberen Hälfte | Breite 1920 | sicher |
| w8 | 0x40 | `0x438` | Höhe 1080 | sicher |
| w9 | 0x48 | Abstand 26,63 · 10⁶ je Frame, Verhältnis zur Monotonuhr 1,5967 Ticks/ns | **Aufnahmezeitpunkt in TSC-Ticks** (≈ 1,597 GHz); ergibt genau 59,94 fps | sicher |
| w10 | 0x50 | 0x4200 bis 0x4900 (≈ 17 000 bis 18 700) | unbekannt; Kandidat Encode-Dauer in µs | Vermutung |
| w11 | 0x58 | obere Hälfte bei IDR ≈ 0xB9xx bis 0xC4xx, bei P kleiner | Größe des Bildes in Bytes (IDRs dort 47 bis 50 kB; bei `ReadVideo2` `+0x6c`, `docs/re/hevc.md`) | wahrscheinlich |
| w12, w13 | 0x60, 0x68 | immer 0 | reserviert | – |

### w1 und w2: ein 240-kHz-Takt, keine Ringpuffer-Positionen

Diese Datei las `w1 − w2` zuerst als Größe eines IDR in Bytes. Das passt nicht: 4004 Bytes sind für ein IDR viel zu wenig
(der Stream trug im Home-Menü 1,2 bis 1,3 Mbit/s, ein IDR dort 55 bis 70 kB, `src/video/frame_pacing.hpp`). ps5-web-stream
(GPL-3.0, `docs/AVCAP2.md` und `src/avcap.c` dort) liest `0x08` als Präsentations- und `0x10` als Decodierzeit in Ticks
eines 240-kHz-Takts; dort stiegen aufeinanderfolgende Bilder bei 60 fps um 4004 Ticks. Das ist genau ein Bild bei
59,94 fps (240 000 × 1001 / 60 000) und passt zu beiden Beobachtungen oben.

Gemessen am 2026-11-22 (13.00, Debug-Zeile unten, `reports`-Runde p56, Testclient, je 300 Bilder ab dem ersten IDR): `0x08` und
`0x10` steigen bei H.264 von Bild zu Bild um genau 4004, bei IDR wie P-Frames; auch bei HEVC, bei 4K und mit der Ausgabe der
Konsole auf 120 Hz (die Aufnahme läuft dann weiter mit 59,94 fps, 2002 kam nicht vor). Der Takt läuft über Sitzungen weiter:
Zwischen zwei Streams 86 s auseinander lag er um 86,0 s weiter, und `w1` geteilt durch 240 000 lag 15 bis 22 ms (etwa ein Bild) vor `w9`
geteilt durch die TSC-Frequenz (1,59626 Ticks/ns). Er zählt also im selben Zeitstrahl wie `w9`.

Weiter laut ps5-web-stream: Der Takt startet bei `TSC × 240 000 / TSC-Frequenz`, aber manche Encoder-Modi behalten ihren
Zähler über Pausen, sodass der Rohwert nach einem Größenwechsel Sekunden hinter der Uhr liegen kann; das Projekt verankert
deshalb das erste Bild jeder Sitzung am aktuellen TSC und übernimmt nur die Abstände. Der Ton zählt im selben Takt (5120 Ticks
je 1024 Samples bei 48 kHz), mit eigenem Nullpunkt, der beim ersten Anlegen des Tongeräts gesetzt wird und über Neustarts
der Aufnahme weiterläuft. Der Host nimmt für Bilder weiter den TSC-Stempel (`w9`), der Takt ist ein zweiter Weg.

### `ReadVideo2` (HEVC)

Der Descriptor von `sceAvcap2ReadVideo2` ist 0x88 Bytes groß: ein Kopf aus zwei Wörtern (Größe `0x88`, dann 0), danach
der Descriptor von `ReadVideo` Wort für Wort, mit seiner eigenen Größe `0x70` bei `+0x10`. Alles liegt also 0x10 weiter
hinten: der 240-kHz-Takt bei `+0x18`/`+0x20`, Bildtyp `+0x2c`, Bildnummer `+0x30`, Breite und Höhe `+0x4c`/`+0x50`,
TSC-Stempel `+0x58`, Bildgröße `+0x6c` (gemessen am 2026-11-22 auf 13.00 mit der Debug-Zeile, HEVC 1080p und 4K;
`docs/re/hevc.md`).

### Die Debug-Zeile

Für Firmwares, auf denen der Host nicht gemessen wurde (Issue 61: HEVC auf 11.40 lief bei zwei Clients ungleichmäßig, ohne
Verlust), wertet der Host nach jedem Öffnen und danach alle 5 Minuten 300 Bilder aus (`src/video/descriptor_clock.hpp`) und
schreibt auf Log-Stufe `debug` (Standard des Debug-Builds) zwei Zeilen dieser Form (Werte ausgedacht):

```text
PS5 video: channel 0 H.264 descriptor over 300 frames, read 14.20/16.68/19.31 ms, sent 16.60/16.68/16.75 ms, fallback 0; steps 08:4004..4004 10:4004..4004 20:1..1 48:26630000..26650000
PS5 video: channel 0 descriptor at the start of that: 00:0000000000000070 08:00000000000f51e4 10:00000000000f4240 ...
```

- `read` und `sent`: kleinster, mittlerer und größter Abstand der Lesezeiten und der Zeitstempel, die der Host an Sunshine
  gibt (`capture_time` in `video_ps5.cpp`).
- `fallback`: wie viele Zeitstempel die Lesezeit sind, weil das Feld keinen TSC-Wert der letzten Sekunde enthielt. Auf
  13.00 0 in allen vier gemessenen Streams; viele heißen, dass der Stempel auf dieser Firmware woanders liegt.
- `steps`: für jedes 64-Bit-Wort, das sich geändert hat, der kleinste und größte Schritt von Bild zu Bild. Der 240-kHz-Takt
  zeigt sich als 4004 (oder 2002), der TSC als etwa 26,6 Millionen je Bild bei 59,94 fps.
- Die zweite Zeile: alle Wörter des ersten Bilds der Auswertung.

Auf 13.00 (2026-11-22, Home-Bildschirm und Mario-64-Demo, je 300 Bilder): Lesen 9,8 bis 23,4 ms auseinander, die
weitergegebenen Zeitstempel aber 16,43 bis 16,95 ms, im Mittel 16,68 ms, also 59,94 fps wie beim Testclient. Der Host gibt
die Aufnahmezeit weiter, nicht die schwankende Lesezeit.

## Folgerungen für den Host

- Video-Zeitstempel für Moonlight aus w9 bilden (TSC → ns über die Kernel-TSC-Frequenz), nicht aus der Lesezeit. Damit sind Capture-Latenz und Bild-/Ton-Synchronisation (P5.2) messbar.
- IDR-Erkennung kann zusätzlich zu den NAL-Typen auf w3 gestützt werden.
- Ein Sprung in w4 zeigt verlorene Frames im Dienst an (bisher nie beobachtet).

## Gemessene Eigenschaften des Streams (Home-Menü)

- 60 fps (59,9 bis 60,2 je Zyklus), Leseabstand 14,4 bis 19,3 ms.
- IDR exakt alle 60 Frames, also einmal pro Sekunde (mit `cfg+0x94 = 30`; der Host setzt inzwischen zehn Sekunden, `docs/re/hevc.md`).
- 1,2 bis 1,3 Mbit/s bei statischem Bild (konfiguriert sind 6 Mbit/s in `0xC8`).
- Öffnen dauert 63 bis 80 ms; fünf Start/Stop-Zyklen ohne Fehler und mit vollständiger Freigabe.
- Speicher des Payloads stabil bei 12,8 MB (max RSS).
