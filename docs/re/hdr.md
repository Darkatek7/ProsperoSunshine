# HDR und 10 Bit über Avcap2 (statisch)

Stand 2026-11-01, Firmware 13.00, Build J03851548. Statische Analyse von `SceAvCapture`, `SceRemotePlay`, `SceGameLiveStreaming`, `libSceAvcap2` und `libSceVenc`. **Live geprüft sind seitdem die Schritte 3 und 9 des Spikes bis zum Testclient, Ergebnisse in `docs/status-archive.md`, „HDR“:** `cfg+0x20 = 1` ergibt ohne weitere Änderung Main 10, `yuv420p10le`, bt2020nc/bt2020/smpte2084, auch in 4K, und der Host bietet es an. Schritt 9 lief auch mit einem echten Moonlight am HDR-Fernseher, Metadaten mit Nullen; das Bild passt laut Besitzer. Alles Übrige hier ist weiter nur gelesen. `libSceAvSetting` und die Encoder-Firmware liegen nicht vor; was dort geschieht, ist offen. „Gelesen“ heißt: steht so im Code, Adresse dabei. „Vermutung“ heißt: nicht belegt.

## Ergebnis

* **`output & 3` ist nicht HDR, sondern VR.** Die Sperre in `capture.c` trifft PS VR und PS VR2. HDR ist Bit `0x4`, und das ist in `0x13c` gesetzt.
* **`cfg+0x20 = 1` ist 10 Bit** (P010), ab da setzen Dienst und Encoder alles Übrige selbst, auch BT.2020 und PQ in der Kennzeichnung. Nur HEVC.
* **Aufnahme und Ausgabe müssen im Dynamikumfang nicht passen.** Remote Play nimmt die Wahl vom Client und prüft Bit `0x4` nicht. Ob das Bild dabei richtig umgesetzt wird, sagt der Code nicht.
* **Moonlight-Seite:** wenige Stellen, siehe unten. Metadaten liefert Avcap2 nicht.

## Der Wert von `GetVideoOutMode`

Gelesen: `SceRemotePlay 0x3124b0` schreibt den Wert als Text in eine Zeichenkette, der Dienst berechnet ihn in `SceAvCapture 0x8990` aus einem Zustand, den der AvSetting-Rückruf `0x8450` füllt.

| Bit | Text in Remote Play | Bedingung im Dienst |
| --- | --- | --- |
| `0x1` | `VR_GEN1` | Rückruf-Feld `+0x80` = 1 |
| `0x2` | `VR_GEN2` | `+0x80` = 2; nur dann füllt `0x8450` auch einen zweiten Ausgang (`+0x48..`) |
| `0x4` | `HDR` | Import `3jnRsjnzIjI` von `libSceAvSetting` (Rückruf-Feld `+8`) ergibt 1 (`0x8493`) |
| `0x8` | `4K_SCAN_IN` | Ausgabeklasse 6, 60 Hz; auf einer Konsole mit `sceKernelHasTrinityMode` auch 120 Hz und Klasse 7 |
| `0x10` | `60Hz` | Bildrate 3 oder `0x8003` |
| `0x20` | `WQHD` | Ausgabeklasse 5 oder 6 mit 60 Hz (Trinity wie bei `0x8`) |
| `0x40` | – | Bildrate `0xd` oder `0x800d`, also 120 Hz |
| `0x100` | `4K` | Ausgabeklasse 6 |
| `0x200` | `8K` | Ausgabeklasse 7 |

`0x13c` heißt also `HDR, 4K_SCAN_IN, 60Hz, WQHD, 4K`. `0x8` und `0x20` passen zu den öffentlichen Angaben in `docs/firmware-porting.md`.

Was der Dienst damit tut (gelesen):

* Er vergleicht den ganzen Wert mit `cfg+0x2a0`. Abweichung bei `Start`: `0x81950008` (`0x14340`). Abweichung im laufenden Kanal: das Lesen meldet `0x81950009` (`0x15f8a`). Eine HDR-Umschaltung beendet also den Kanal, falls sich Bit `0x4` dabei ändert. Der Host öffnet dann neu.
* Sonst wertet er Bit `0x4` nicht aus; der Getter `0x8950` hat keinen Aufrufer.
* Remote Play (`0x70460`) wertet nur aus: `0x1` und `0x2` wählen `cfg+0x2a4`, `0x20` und `0x8` sperren Modus 5 und 6, `0x210` sperrt kleine Modi. Bit `0x4` steht dort nirgends. Eine Sperre bei VR hat Remote Play nicht, es nimmt nur eine andere Quelle.

Offen: Der Wert gilt laut `docs/firmware-porting.md` und ps5-web-stream als der SDR-Fall, und öffentliche Berichte nennen ihn auch bei 1080p. Entweder lief die Konsole bei allen Messungen in HDR, oder `0x4` (und `0x100`) meint eine Fähigkeit statt des Zustands. Das hängt an `3jnRsjnzIjI`, dessen Code fehlt. Spike-Schritt 1 klärt es ohne Risiko.

## Felder im Datensatz

Gelesen, `cfg+0x20`:

* Validator `0x17aa0`: ≤ 1. Bei 1 darf das Byte `+0x88` der Speicher-Anfrage nicht 1 sein (Remote Play und der Host setzen 0).
* `0x17d0e`: geht als Bittiefe 8 oder 10 in `sceVencQueryPreset`. `libSceVenc 0x7a57` lässt nur diese zwei zu, `0x7a6f` lehnt 10 Bit bei AVC ab (`0x80c70004`).
* `0x17e60`: Encoder-Format `0x90` und `0x78` statt `0x7a` (`0x6f` in den zwei kleinsten Modi) und `0x6e`. `libSceVenc 0x9401` und `0x94c0` verlangen genau dieses Paar, bei 10 Bit außerdem Pitch `0x100`.
* `libSceVenc 0xbef8`: Format `0x90` setzt die Farbwerte 9, 9, 16, also BT.2020 und ST 2084 (PQ); `0x7a` setzt 1, 1, 1, `0x6f` 6, 6, 6. Die Reihenfolge der Felder ist Vermutung. Ob sie im VUI des SPS landen, zeigt erst der Spike.
* `0x4a54`: Das schwarze Ersatzbild hat bei 1 je Abtastwert 16 Bit, Y `0x1000`, UV `0x8000`. Das ist P010.
* `0x8137`: Wert + 1 geht als Formatfeld `+0x14` in den Scan-In (`3gVX6ejR1hM` von `libSceAvSetting`).
* `SceRemotePlay 0x703c8`: Puffer für Image Paste `× 3` statt `× 1,5`; `0x71434`: schreibt dort 16 statt 8 Bit.

Bezug in Remote Play: JSON-Feld `dynamicRange` (`0x2a5d79`; `SDR` 0, `HDR` 1, `AUTO` 1 und zusätzlich ein Merker) → `0x23227a` → `0x6c0b4` → Eingabe `+0xc` von `0x6f7f0` → `cfg+0x20`. Die Konvertierung `0x70e60` gibt denselben Wert an `GetNonVclNalUnitStream2`. Die Einblendung `0x3ad53` nennt die Fälle `AVC`, `HEVC SDR`, `HEVC HDR10`. Wofür der `AUTO`-Merker dient, ist nicht gefunden.

Andere Felder:

* **Profil und Stufe:** Kein eigenes Feld. `cfg+0x5c = 9` setzt Remote Play in beiden Fällen. `libSceVenc 0x828b` setzt bei 10 Bit ein Byte im HEVC-Profil-Block (Vermutung: Main 10).
* **Speicher:** Keine Felder. `0x17dd0` fragt mit der Bittiefe, also wächst die Reservierung von selbst. Der Host kopiert `cfg+8..` nach dem Füllen, das passt. Der Sonderweg `SceRemotePlay 0x6fdd5` (zwei Aufrufe mit Modus 2 und 4) gilt nur, wenn die Eingabe `+0x20` gesetzt ist; was das bedeutet, ist nicht geklärt.
* **`cfg+0x220..0x29f` sind keine Farbfelder.** Das sind bis zu 8 Einträge je 16 Byte, dieselben wie im Aufruf `aAbMwQwWHyM` (`0x70cb0`, `0x70d50`, Dienst `0x145b0`). `0xff` heißt leer. Vermutung: Bereiche für Image Paste.
* **`cfg+0x204..0x210`:** Maße des Image-Paste-Puffers. Der Host lässt sie 0.
* **`cfg+0x2a4` ist die Quelle, keine Farbe** (Tabelle `0x26448`, Ausgang `0x7ed0`). ps5-web-stream nennt es einen Farbwähler; der Code stützt das nicht. Nebenbei: Remote Play setzt bei Zweck 6 ohne VR 1 (`0x7054a`), der Host 2.

## 8 Bit bei HDR-Ausgabe

Gelesen:

* Der Client bestimmt `cfg+0x20`, nicht die Ausgabe (oben). Keine Prüfung in Remote Play, im Dienst (`0x13f70`) oder in `libSceVenc` verbindet Bit `0x4` mit `cfg+0x20`.
* Das Quellformat folgt der aktuellen Ausgabe: `0x14970` → `0x17ec0` nimmt die Klasse aus `0x8920` und setzt `0x7a`, `0x90` oder `0x96` (Tabelle `0x26968`; den Aufrufer von `0x14970` habe ich nicht verfolgt, er läuft wohl über eine virtuelle Methode, direkt nutzt es die Speicherabfrage `0xa4c4`). `libSceVenc 0x9a3a` und `0x9a47` geben Quell- und Zielformat getrennt an die Firmware; Quelle gilt bei Scan-In immer als 10 Bit (`0x99e6`). Für Scan-In gibt es keinen Vergleich der beiden (`0x996e`).

Daraus folgt: Die Kombination Ausgabe HDR, Stream 8 Bit ist vorgesehen und wird nicht abgelehnt. Die Umsetzung geschieht unterhalb von dem, was hier vorliegt (Scan-In oder Firmware). **Ob dabei Tone Mapping stattfindet oder nur Farbraum und Tiefe, oder gar nichts, ist statisch nicht zu sehen.** Vermutung: Es findet eine Umsetzung statt, weil Sony sonst bei `dynamicRange = SDR` ein falsches Bild zeigte. `SceGameLiveStreaming` prüft ebenfalls nichts und reicht `GetVideoOutMode` nur durch (`0x4d243`).

Für Besitzer von HDR-Fernsehern heißt das: Die Sperre in `capture.c` trifft sie laut Code nicht, nur VR-Sitzungen. Die vermutete Folge „Konsole in HDR kann nicht streamen“ stützt der Code nicht. Die Sperre heißt deshalb nun `PS_CAPTURE_VR`, auch der Text in der App nennt PS VR.

## Sunshine

Stellen in `.cache/deps/src/sunshine/src`:

* **Anbieten:** `active_hevc_mode = 3` und `last_encoder_probe_supported_dynamic_range_for_codec[1] = true` in `probe_encoders`. Das ergibt `SCM_HEVC_MAIN10` (`nvhttp.cpp:1229`) und `IsHdrSupported` (`nvhttp.cpp:1368`).
* **Wunsch des Clients:** `x-nv-video[0].dynamicRangeMode` → `config.dynamicRange` (`rtsp.cpp:1175`), 0 = 8 Bit, 1 = 10 Bit (`video.h:43`). `video_ps5.cpp` muss ihn an die Capture geben, bei `videoFormat == 1`.
* **Client umschalten:** `video_ps5.cpp:88` hebt heute immer `hdr_info_raw_t(false)` aus. Für HDR `hdr_info_raw_t(true, metadata)`; `stream.cpp:1143` schickt das, sobald der Client da ist. Avcap2 liefert keine Metadaten. Feste Werte (BT.2020-Primärfarben, Helligkeit geschätzt) oder Nullen; wie Clients Nullen behandeln, ist nicht geprüft.
* **Capture:** neuer Wert für `cfg+0x20`, auch in `mode_snapshot`, sonst übernimmt `ps_capture_mode_current` einen offenen 8-Bit-Kanal. Die Sperre `output & 3` umbenennen; ob VR überhaupt gesperrt bleiben soll, ist eine eigene Frage.
* **Lesen:** `ps_capture_read2` und `ps_capture_headers` brauchen nichts. Der Dienst nutzt das 10-Bit-Flag nur an vier Stellen (Ersatzbild, Scan-In), nicht beim Füllen des Rings, und der Descriptor hat kein Feld dafür. `ReadVideo3` liefert `0x2dc` Byte mehr (Satz-Offset `0x88`), die weder Remote Play noch GLS importieren; ihr Inhalt ist nicht bestimmt. `encoderCscMode` bleibt wirkungslos, die Farbe steht im SPS.
* **Testclient:** `tools/moonlight/stream_test.c:393` bietet kein `VIDEO_FORMAT_H265_MAIN10` an, das muss für den Stream-Test dazu. `cl_hdr` protokolliert die Umschaltung schon.

## Spike, in dieser Reihenfolge

Vor jedem Schritt `serverinfo` prüfen. HDR-Einstellung und Spielstart übernimmt der Besitzer. Keine Screenshots während eines Kanals.
`out/hdr_spike.elf` macht die Schritte 3 und 4 ohne die Bildpuffer-Felder `cfg+0x204..0x210`; die Dateien in `/data/prosperosunshine/`
danach mit `ffprobe` prüfen und löschen.

1. **Nur lesen:** `GetVideoOutMode` loggen (steht schon als `video out mode` in `host_diag.log`) bei HDR aus, HDR „wenn unterstützt“ auf dem Startbildschirm, HDR in einem HDR-Spiel. Klärt Bit `0x4`, und ob es wechselt.
2. **Ohne Encoder:** `GetNonVclNalUnitStream2` mit `{u64 0x28, Art 0, Zweck 2, Modus 4, Takt 1, cfg+0x20 = 1, Codec 1, Ressource}`, Ausgabe `0x1a8` wie `0x70e60`. SPS mit `ffprobe` oder einem Parser lesen: Main 10, 10 Bit, BT.2020, PQ, Bereich. Dasselbe mit `cfg+0x20 = 0` als Gegenprobe.
3. **HEVC 1080p60:** Zweck 2, Profil 180, alle Felder wie heute, nur `cfg+0x20 = 1`. Erwartet: Speicher, `OpenVideo`, `Start` melden 0, die Reservierung ist größer als `0x3e8000` und `0x1fec000`. Erstes IDR in `ffprobe`: `yuv420p10le`, `bt2020nc`, `smpte2084`.
4. **Falls `Start` `0x80c78000` meldet**, je ein Schritt, jeweils mit `ps_capture_override_config` oder im Spike:
   1. `cfg+0x10c` wie Remote Play (Spitze × 1001 / (fps × 1000) / 8); `0x60000` ist nur für 8 Bit gemessen.
   2. Bitrate 15 statt 30 Mbit/s.
   3. `cfg+0x204..0x210` wie `0x703b3..0x703f1`, Größe × 3.
   4. Profil 182 statt 180.
   5. Zweck 6 statt 2.
5. **Matrix 1080p, je 10 s, Bilder gegen einen späteren Screenshot derselben Szene vergleichen:**
   1. Ausgabe SDR, `cfg+0x20 = 0` (Bezug).
   2. Ausgabe HDR, `cfg+0x20 = 0`. Sagt, ob 8 Bit bei HDR ohne Client-Änderung brauchbar ist.
   3. Ausgabe HDR, `cfg+0x20 = 1`.
   4. Ausgabe SDR, `cfg+0x20 = 1`.
6. **Umschalten im laufenden Kanal:** 8-Bit-Kanal offen, HDR-Spiel starten. Erwartet `0x81950009` beim Lesen, falls sich Bit `0x4` ändert; der Host öffnet neu. Mit dem Wert aus Schritt 1 abgleichen.
7. **Große Modi:** 4K und 1440p mit Zweck 6, Profil 182, `cfg+0x10c` wie im Host, dann falls nötig kleiner. Encoder-Last und Reservierung beobachten.
8. **Gegenprobe AVC:** `cfg+0x24 = 0`, `cfg+0x20 = 1`. Erwartet `0x80c70004`. Nur zur Bestätigung, entfällt bei Zeitmangel.
9. **Ende-zu-Ende:** `active_hevc_mode = 3`, `dynamicRangeMode = 1`, Testclient mit Main 10, danach mit dem Besitzer ein Moonlight auf einem HDR-Gerät. Metadaten erst mit Nullen, dann mit festen Werten.

## Offen

* Was `3jnRsjnzIjI` prüft, also die genaue Bedeutung von Bit `0x4` und dem Rückruf-Feld `+8`.
* Tone Mapping oder nicht (Scan-In, Firmware).
* Bedeutung von Format `0x96` (Klasse 3).
* Inhalt der `0x2dc` Byte bei `ReadVideo3`.
* Der `AUTO`-Merker in Remote Play und ob sich der Wert später noch ändert.
