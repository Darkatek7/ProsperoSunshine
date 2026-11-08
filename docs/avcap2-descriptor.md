# Avcap2: Frame-Descriptor von `sceAvcap2ReadVideo` (FW 13.00)

Stand: 28. Oktober 2026. Gemessen mit `src/probes/capture_soak.cpp` (fünf 5-s-Zyklen und ein 10-min-Lauf im Home-Menü, 1080p H.264). Der Descriptor ist 0x70 Bytes groß; Wort 0 ist die Größe, die der Aufrufer vorbelegt. Die Tabelle nennt 64-Bit-Wörter in Little-Endian.

| Wort | Offset | Beobachtung | Deutung | Sicherheit |
| --- | --- | --- | --- | --- |
| w1 | 0x08 | `w1` eines Frames ist `w2` des nächsten | Ende-Position des Frames in einem internen Ringpuffer | wahrscheinlich |
| w2 | 0x10 | siehe oben; `w1 − w2` passt zur Framegröße (IDR 4004 Bytes) | Start-Position im Ringpuffer | wahrscheinlich |
| w3 | 0x18 | obere Hälfte `1` bei IDR, `3` bei P-Frames | Bildtyp | sicher (60 IDR-Stichproben) |
| w4 | 0x20 | 1, 2, 3, … je Sitzung | Framezähler (1-basiert) | sicher |
| w5 | 0x28 | steigt alle 60 Frames um 1, auch über Sitzungen hinweg | GOP-Zähler des Encoders | sicher |
| w6 | 0x30 | Abstand ≈ 16,66 · 10⁶ je Frame, läuft über | untere 32 Bit eines ns-Zeitstempels | Vermutung |
| w7 | 0x38 | `0x780` in der oberen Hälfte | Breite 1920 | sicher |
| w8 | 0x40 | `0x438` | Höhe 1080 | sicher |
| w9 | 0x48 | Abstand 26,63 · 10⁶ je Frame, Verhältnis zur Monotonuhr 1,5967 Ticks/ns | **Aufnahmezeitpunkt in TSC-Ticks** (≈ 1,597 GHz); ergibt genau 59,94 fps | sicher |
| w10 | 0x50 | 0x4200 bis 0x4900 (≈ 17 000 bis 18 700) | unbekannt; Kandidat Encode-Dauer in µs | Vermutung |
| w11 | 0x58 | obere Hälfte bei IDR ≈ 0xB9xx bis 0xC4xx, bei P kleiner | unbekannt, abhängig vom Bildtyp | Vermutung |
| w12, w13 | 0x60, 0x68 | immer 0 | reserviert | – |

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
