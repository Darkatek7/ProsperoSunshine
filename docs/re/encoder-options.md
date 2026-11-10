# Encoder-Optionen hinter Avcap2: Bildrate, Verlust, IDR-Größe, Slices, Codecs (FW 13.00)

Statische Analyse, kein Zugriff auf die Konsole. Eingaben: entschlüsselte ELFs `SceAvCapture` (IPMI-Server hinter
libSceAvcap2), `libSceVenc` (Encoder-Bibliothek, die der Server lädt) und `SceRemotePlay`, dieselbe Firmware wie
`docs/re/avcap2-exports.md`. Alle Offsets sind modulrelativ (= vaddr) in dieser Firmware. `cfg+X` meint den
OpenVideo-Datensatz des Hosts (0x2E0 Byte auf 13.00, `ps_capture_build_config()` in `src/platform/ps5/capture.c`). Es
ist kein Sony-Code im Repository; beschrieben werden Offsets, Feldbedeutungen und Ablauf.

Konfidenz wie in `docs/re/avcap2-exports.md`: **sicher** = direkt aus Code oder Hash belegt, **wahrscheinlich** =
mehrere konsistente Indizien, **Vermutung** = plausibel, aber nicht belegt (braucht einen Live-Test). **gemessen**
verweist auf eine Messung an der Konsole, die schon in `STATUS.md` oder `docs/status-archive.md` steht; neue Messungen
enthält dieses Dokument nicht.

## 1. Kurzantworten

| Frage | Antwort | Konfidenz |
|---|---|---|
| 120 oder 90 fps über Avcap2? | Nein. libSceVenc hat Presets nur für 29,97 und 59,94 fps; jede andere Rate endet in `0x80c70004`. | sicher (Code), passt zur Messung |
| 24, 25, 48, 50 fps? | Nein, aus demselben Grund. Avcap2 kodiert mit 29,97 oder 59,94 fps. | sicher |
| Verlust ohne IDR beheben? | Ja, über `sceAvcap2SetInvalidFrame` (RFI, so wie Remote Play). Long-Term-Referenzen über `cfg+0x138` erreichbar. Kein Intra-Refresh gefunden. | RFI-Pfad sicher, Wirkung ungetestet |
| IDR-Größe begrenzen? | Keine „max frame size“. Hebel: QP-Grenzen `cfg+0xd4`/`+0xd8` (13.00), IDR-QP `cfg+0xc4` (10.x), weniger IDRs (RFI). | wahrscheinlich |
| Slices? | Beim Öffnen `cfg+0x38`-Block, zur Laufzeit Slice-Size-Control `cfg+0xe0` (nur H.264). 10.x-Feld weiter offen. | sicher (13.00), offen (10.x) |
| AV1? | Gibt es nicht, in keinem der Module. Der dritte Venc-Codec ist wahrscheinlich VP9. | sicher (AV1), wahrscheinlich (VP9) |
| B-Frames? | Keine; nur I und P. | gemessen |
| Bietet der Host an, was geht? | Fast: H.264 auf 10.x scheitert an Windows-Hardware-Decodern, RFI wird nicht angeboten, Bildraten kann das Protokoll nicht ablehnen. | sicher (Code) |

## 2. Methodik

Skripte in `tools/re/` wie in `docs/re/avcap2-exports.md`, Abschnitt 1, dazu drei neue (Python, lesen nur
`.cache/re/*.elf`):

| Skript | Zweck |
|---|---|
| `xref.py <Elf> <hex-addr\|"text">` | RIP-relative Verweise auf eine Adresse oder einen String (lea/mov), mit Funktionsstart |
| `callers.py <Elf> <hex-addr>...` | direkte `call`/`jmp` (E8/E9) auf eine oder mehrere Adressen |
| `immsearch.py <Elf> <hex-wert>` | 32-Bit-Konstante im Code (Fehlercodes `0x80c7xxxx`, Raten `0xbb5`/`0x176a`/`0x2ed4`) |

`capstone` (für `disasm.py`/`funcdump.py`) lief in einer eigenen venv außerhalb des Repositorys. `.cache/re` darf ein
Symlink auf einen gemeinsamen Cache sein (`.cache/` ist ignoriert).

Namen: Die NID-Suche (`bruteforce*.py`) über `sceVenc` + Verb + Wörter fand vier neue Exporte von libSceVenc (Tabelle in
Abschnitt 3); sie stehen jetzt in `tools/re/consumer_names.txt`. Bei 64-Bit-Hashes gelten Treffer als **sicher**.

## 3. Vom Datensatz zum Encoder

### Blöcke des OpenVideo-Datensatzes (13.00)

| Bereich | Inhalt | Konfidenz |
|---|---|---|
| `cfg+0x08..0x30` | Kopf: `0x10` Kanalart, `0x14` Zweck, `0x18` Auflösung, `0x1c` Timing, `0x20` 10 Bit, `0x24` Codec, `0x28` Ressource | sicher (`avcap2-exports.md`) |
| `cfg+0x38` (0x18) | Slices beim Öffnen: u8 `0x40` an, `0x44` Anzahl, `0x48` Modus, `0x4c` Größe | sicher (Kopierer `0x18080`) |
| `cfg+0x50` (0x10) | Level: u8 `0x58` an, `0x5c` Level-Index | wahrscheinlich |
| `cfg+0x60` | u8 `0x68`; Remote Play setzt es für HEVC | nicht verfolgt |
| `cfg+0x88` (0x30) | `0x94` IDR-Abstand in Bildern (gemessen, `hevc.md`); `0x98` Vorgabe für Fach +0x08 (unten) | sicher / Vermutung |
| `cfg+0xb8` (0xE0) | Encoder-Block mit Unterblöcken, siehe unten | sicher |

### Encoder-Block `cfg+0xb8` → Venc-Konfiguration

Der Server füllt aus dem Encoder-Block einen Container mit Zeigern auf Venc-Teilkonfigurationen (Größe 0x60) und ruft
danach `sceVencSetConfig`. Das geschieht an zwei Stellen:

* **Beim Öffnen**: `0x3bb0` ruft `0x3cd0` (QueryPreset `0x4e20`, Überschreiben aus dem Datensatz `0x5010`,
  Codec-Vtable +0x140 → `0xaa90` → `sceVencCreateEncoder`) und direkt danach `0x3de0`: `sceVencGetConfig`, Füllen des
  Containers in `0x14a00`, `sceVencSetConfig`. **sicher.** Damit wirken alle Unterblöcke schon ab dem ersten Bild.
* **Zur Laufzeit**: Handler von `SetVideoEncodeConfig` `0x3520`, dieselben Kopierer, danach `sceVencSetConfig`.
  **sicher.**

| Fach | Quelle im Datensatz | Kopierer | Venc-Setter (NID, Offset in libSceVenc) | Konfidenz |
|---|---|---|---|---|
| +0x08 | `cfg+0x188`: u8 `0x190`, u32 `0x194` | `0x18160` | unbenannt (`dqA5cHFo1wQ`, `0x6ac0`) | sicher (Fluss), Bedeutung offen |
| +0x10 | kein Feld; Server setzt 1 oder 2 aus `cfg+0x10`, `cfg+0x24`, `cfg+0x2c8` | `0x14a00` | unbekannt | sicher (Fluss) |
| +0x18 | `cfg+0xc0` Rate Control: `0xc8`, `0xcc`, `0xd0`, `0xd4`, `0xd8` (je nur ≠ 0) | `0x18190` | wohl wie `sceVencSetRateControlConfig` (`cHss3kYujzc`) | sicher (Fluss), Setter wahrscheinlich |
| +0x30 | `cfg+0x120`: u8 `0x128`, u32 `0x12c`, u32 `0x130` | `0x181e0` | unbenannt (`B5iyzWMm+DY`, `0x7150`) | sicher (Fluss), Bedeutung offen |
| +0x38 | `cfg+0x100` Frame Dropping: u8 `0x108`, 16 Byte `0x10c..0x11b` | `0x18210` | `sceVencSetFrameDroppingConfig` (`Vm5uPoERWmg`, `0x72a0`) | sicher |
| +0x40 | `cfg+0xe0` Slice Size Control (zur Laufzeit nur bei Codec ≠ HEVC) | `0x18230` | `sceVencSetSliceSizeControlConfig` (`6nGfjRPqCUM`, `0x73f0`) | sicher |
| +0x50 | `cfg+0x138` Long-Term-Referenz: u8 `0x140` an, u32 `0x144` Anzahl, bis 16 u32 `0x148..0x187` | `0x18260` | `sceVencSetLongTermReferenceConfig` (`JoBLKJB-Nck`, `0x7690`) | sicher (Fluss), Felder wahrscheinlich |
| +0x58 | wird nicht gefüllt | – | `sceVencSetTemporalSvcConfig` (`766UkIIwtxU`, `0x77e0`) | sicher |

Einzelheiten aus `0x14a00` (Öffnen):

* Fach +0x08: Ist die Slice-Size-Control (`cfg+0xe8`) aus, schaltet der Server das Fach selbst ein, mit `cfg+0x194`,
  wenn gesetzt, sonst `cfg+0x98`, mindestens 1. Bei H.264 wird es übernommen, wenn das u8 ≠ 0 ist, bei HEVC nur bei
  genau 1; beim dritten Codec bleibt es leer (`0x18160`). **sicher.** Remote Play setzt für HEVC `cfg+0x98 = 1`,
  `cfg+0x190 = 1`, `cfg+0x194 = 1` und für H.264 stattdessen die Slice-Size-Control. **Vermutung:** Fach +0x08 ist
  entweder eine feste Slice-Zahl je Bild (die Alternative zur Steuerung nach Byte) oder die Zahl der Referenzbilder
  (Remote Play kennt den Schlüssel `dpbSize`); Remote Play wählt für HEVC 1. Der Test zu RFI (Abschnitt 5) unterscheidet
  beides.
* Fach +0x40: Ist die Slice-Size-Control an und sind `cfg+0xf4`, `+0xf8`, `+0xfc` alle 0, übernimmt der Server Anzahl,
  Modus und Größe aus dem Slice-Block beim Öffnen (`cfg+0x44`, `+0x48`, `+0x4c`). **sicher.** Beim Öffnen gibt es keine
  Codec-Prüfung, nur zur Laufzeit.

## 4. Bildrate (B.1)

**Ergebnis: Avcap2 kodiert auf 13.00 nur mit 29,97 oder 59,94 fps. 90 und 120 fps scheitern im Encoder selbst. sicher.**

1. `cfg+0x1c` (Timing) prüft der Server in `0x17cc0`: Werte über 2 lehnt er ab (gemessen: 3 → `0x81950001`). 0 und 1
   übersetzt die Tabelle bei `0x268d8` in 2997 und 5994 (Hundertstel fps), 2 in 0. **sicher.**
2. Timing 2 heißt „Rate der Bildausgabe übernehmen“: `0x17ec0` liest zwei Raten der Konsolenausgabe (`0x86c0` und
   `0x8730`, Ergebnis 30, 60, 90, 120 oder roh), verlangt, dass sie gleich sind (sonst `0x8195000a`), und gibt sie an
   den Encoder weiter. Bei 0 und 1 fragt er dagegen AvSetting (Import `s-Egk+i6sAY`, über `0x87c0`). **sicher**
   (Ablauf), **wahrscheinlich** (Bedeutung der zwei Raten).
3. libSceVenc `0x9140` (gerufen von CreateEncoder `0xa680` und von QueryMemorySize): Ist die Rate 0 und die Quelle die
   Bildausgabe, nimmt es deren Rate und übersetzt 30 → 2997, 60 → 5994, 120 → 11988. **sicher.**
4. libSceVenc `0x7960` sucht als Erstes das Preset in `0x84d0`, ohne Bedingung. `0x84d0` hat für jede Auflösung Einträge
   nur für 2997 und 5994 (480×270 nur 30 fps; 360p bis 2160p in zwei Preset-Familien). Jede andere Rate liefert 0, und
   `0x7960` meldet bei `0x7a06` (Zeilenmarke `0x1ab`) **`0x80c70004`**. **sicher.**
5. Die Kopf-Presets für H.264 (`0x930`) und HEVC (`0x1840`) kennen ebenfalls nur 2997 und 5994 (0 → 5994). **sicher.**
6. Code für 89,91 und 119,88 gibt es: `0x7960` rechnet 2997/5994/8991/11988 später in 30/60/90/120 um
   (`0x7b97..0x7be5`), die Helfer `0x87c0`, `0x8800`, `0xc210`, `0xc280` kennen `0x231f` und `0x2ed4`. Er ist aber erst
   nach der Preset-Suche erreichbar. **sicher.** Ein Preset für 90 oder 120 fps fehlt in dieser Firmware.
7. Remote Play (`0x6f964`) lässt nur 30 und 60 durch. **sicher.**

Messung dazu (`docs/status-archive.md`, „HDR-Metadaten, Slices, 120 fps …“): Mario-64-Demo mit 120-Hz-Ausgabe, Timing 2
→ `0x80c70004` bei H.264 und HEVC. Das ist genau Schritt 3 und 4: Die Ausgabe meldet 120, Venc findet kein Preset.
Der TSC-Stempel im Descriptor ergibt genau 59,94 fps (`docs/avcap2-descriptor.md`, w9), und frühere Smoke-Läufe zeigen
`video_fps_avg 59.94`.

`sceAvcap2GetVideoEncoderCapability` (Server `0x28d0` → libSceVenc `0xa4d0`, Export `39+oWg0ZObY`) liefert nur zwei
Bits: Presets 100 und 200 je nach SDK-Version des Aufrufers (Schwelle `0x840f80`). Über Bildraten sagt sie nichts.
**sicher.**

**Klärender Live-Test:** die vorhandenen Varianten `h264_1080_t2` und `hevc_1080_t2` von `slice_rate_probe`, diesmal
bei 60-Hz-Ausgabe (Startbildschirm). Erwartet: öffnet, 59,9 fps, TSC-Abstand wie Timing 1. Öffnet sie nicht, ist
Schritt 2 falsch gelesen (dann den AvSetting-Pfad neu ansehen).

## 5. Verlust ohne Keyframe (B.2)

**RFI (sicher erreichbar, Wirkung ungetestet).** `sceAvcap2SetInvalidFrame(h, u32)` → Server-Vtable +0xC0 (`0x3890`) →
`sceVencSetReferenceFrameInvalidationConfig` mit dem Bildzähler. Remote Play ruft das bei Verlust mit dem kleinsten
verlorenen Encoder-Bildzähler, begrenzt auf den des letzten IDR (`docs/re/avcap2-exports.md`, Abschnitt 4, Punkt 5).
Der Zähler ist `desc+0x20` (w4, 1-basiert, `docs/avcap2-descriptor.md`). Was der Encoder danach als Referenz nimmt (ein
älteres Bild oder eine Long-Term-Referenz), steht nicht im Server; es liegt in libSceVenc und ist nicht verfolgt.

**Was Remote Play bei Verlust tut (sicher, aus `avcap2-exports.md`):** IDR über `RequestKeyFrame`, höchstens alle 1000
ms, außer bei sofortiger Anforderung, oder `SetInvalidFrame` über ein eigenes Flag. Strings in `SceRemotePlay`:
`VFrameInvalidate`, `IDRREQUEST`, `=RFI`, `dpbSize`. Remote Play hat also beide Wege.

**Long-Term-Referenz (wahrscheinlich nutzbar).** SceAvCapture importiert `sceVencSetLongTermReferenceConfig` nicht,
füllt aber dessen Fach (+0x50) aus `cfg+0x138` beim Öffnen und bei `SetVideoEncodeConfig` (Tabelle oben). Remote Play
lässt den Block auf 0. Was die bis zu 16 u32 bedeuten (Bildabstände oder Indizes), ist **Vermutung**. Der Host schreibt
den Kopf `cfg+0x138` (u64 Größe 0x50) heute nicht; ob der Validator ihn verlangt, ist offen.

**Temporal SVC: nicht erreichbar (sicher).** Fach +0x58 füllt der Server nie, und Avcap2 hat keinen Export dafür.

**Intra-Refresh: nicht gefunden.** Keiner der 75 Venc-Exporte passt zu 132 Namen wie `sceVencSetIntraRefreshConfig`,
`…GradualDecoderRefresh…`, `…RollingIntra…`, `…Gdr…` (Set/Get/Query × Config/Param/Mode), und SceRemotePlay hat keine
solchen Strings. Die unbenannten Fächer +0x08 und +0x30 schließt das nicht aus. **Vermutung:** gibt es nicht.

**Der Host heute (sicher, Code):** `last_encoder_probe_supported_ref_frames_invalidation = false`
(`src/sunshine/video_ps5.cpp:35`). Sunshine nimmt `x-nv-video[0].refPicInvalidation` dann nicht in die DESCRIBE-Antwort
auf (`rtsp.cpp` 962–971), und moonlight-common-c verlangt für jeden Verlust ein IDR (`ControlStream.c` 386–410). Der
Host drosselt das auf eines je 200 ms (`video_ps5.cpp:459–465`).

**Klärender Live-Test:** Probe, die nach 100 Bildern `SetInvalidFrame(h, k)` ruft, mit `k` = w4 des Bildes von vor 3
Bildern, und die folgenden 10 Bilder speichert. Erwartet: `rc == 0`, kein IDR (w3); das nächste Bild ist ein P-Bild auf
ein älteres Bild oder, falls der Encoder nur ein Referenzbild hält, ein I-Bild ohne IDR (groß, Slice-Typ I). Beides
dekodiert der Client, wenn der Host das Bild als „nach Invalidierung“ (Typ 5) markiert. Offline dekodieren mit FFmpeg
ohne die Bilder ab `k` bis zum Aufruf (`-err_detect explode`): fehlerfrei heißt, das Bild nach dem Aufruf referenziert
nur Bilder vor `k`, so wie Remote Play den Wert verwendet (erstes verlorenes Bild).

## 6. IDR-Größe (B.3)

**Kein direkter Deckel (wahrscheinlich).** Weder in den Venc-Teilkonfigurationen, die der Server füllt, noch in den
Namen fand sich eine maximale Bildgröße oder ein Minimal-QP.

* **Rate Control** (Fach +0x18, aus `cfg+0xc0`): +8 Mittel (`cfg+0xc8`), +0xc Spitze (`0xcc`), +0x10 Bit je Bild
  (`0xd0`), +0x14 (`0xd4`), +0x18 (`0xd8`). **sicher** (Kopierer). libSceVenc setzt als Vorgabe (`0x7e92..0x7ec1`) 51 in
  beide letzten Felder (beim dritten Codec 255) und für die Streaming-Presets 300/500/800 35 in +0x14. **sicher.**
  Deutung: **wahrscheinlich** QP-Obergrenzen (51 = höchster H.264/HEVC-QP, 255 = höchster VP9-Index). Passt zur Messung
  in `docs/re/hevc.md`: `cfg+0xd4 = 51` beseitigte die HEVC-Lücken nach IDRs bei 15 Mbit/s, weil das IDR gröber werden
  darf. Der Host setzt für HEVC Remote Plays 35/51.
* **Frame Dropping** (Fach +0x38, `cfg+0x100`): u8 an (`0x108`) und das Budget je Bild `0x10c` (gemessen: ein Bild, das
  N Budgets kostet, lässt N−1 folgende aus, `docs/re/hevc.md`). Das begrenzt nicht das IDR, nur die Bilder danach.
  **sicher** (Fluss), gemessen (Wirkung).
* **10.x und 9.60** haben keine Rate-Felder, dafür zwei QP-Felder (`ps_qp_offset`, gemessen). Der Host setzt dort den
  IDR-QP 4 feiner als den der P-Bilder (`src/platform/ps5/encoder_qp.h:30–35`), also größere IDRs als nötig.

Hebel in der Reihenfolge ihres Nutzens: (1) auf 10.x IDR-QP = P-QP; (2) auf 13.00 `cfg+0xd4` zwischen 35 und 51
abstufen; (3) weniger IDRs durch RFI (Abschnitt 5).

**Klärender Live-Test:** `slice_rate_probe` mit HEVC 1080p bei ihrer Bitrate (16 Mbit/s) und bewegtem Bild,
`cfg+0xd4` = 35 (Vorgabe des Hosts), 41, 46, 51, je 3 s. Gemessen werden die Größe des ersten Bildes (das IDR nach dem
Öffnen) und die größte TSC-Lücke. Erwartet: Das IDR wird mit steigendem Wert kleiner, und die Lücke von zwei Intervallen
nach dem IDR verschwindet ab einem Wert unter 51. Bleibt das IDR gleich groß, ist `cfg+0xd4` keine QP-Grenze.

## 7. Slices (B.4)

**Beim Öffnen (sicher):** `cfg+0x38`-Block → `0x5010` → `0x148f0` → `0x18080` → Fach +0x50 der Create-Parameter: Ist u8
`cfg+0x40` ≠ 0, übernimmt der Server Anzahl `cfg+0x44`, Modus `0x48` und Größe `0x4c`, jedes nur, wenn es ≠ 0 ist.

**Vorgabe in libSceVenc (sicher):** Für die Presets 300/500/800 (`0x7960`, `0x8181..0x81e3` und `0x83e1..0x8442`):
Anzahl = Zahl der Blockzeilen, höchstens `0x44` (68); Modus 4 bei H.264, 1 bei HEVC (aus dem Codec berechnet); Größe
`0x2bb0`. Auf 13.00 kommt trotzdem 1 Slice je Bild, mit und ohne Felder (gemessen). Welches Preset der Host dort trifft
und ob die Vorgabe dann aktiv ist, ist offen.

**Remote Play (sicher):** `0x6f7f0` setzt für H.264 `cfg+0x40 = 1`, `cfg+0x44 = min(Eingabe, 0x3c000 / fps / 60)` (68
bei 60 fps, 136 bei 30), `cfg+0x48 = 1`, `cfg+0x4c = Eingabe × 8`. `0x70130` schaltet für H.264 die Slice-Size-Control
ein: `cfg+0xe8 = 1`, `cfg+0xec = min(cfg+0x4c, 0,262 · kbit/s + 1129,9)`, `cfg+0xf0 = min(cfg+0x4c, 0,2103 · kbit/s +
554,98)`, `cfg+0xf4/0xf8/0xfc` aus `cfg+0x44/0x48/0x4c`. Für HEVC: Slice-Size-Control 0, dafür `cfg+0x190 = 1`,
`cfg+0x194 = 1`. JSON-Schlüssel in Remote Play: `sliceEncodeEnabled`, `sliceMaxCount`, `sliceMaxSize`, `numberOfSlices`,
`maxSlices`, `minMbCount`.

**10.x (gemessen, Formel abgeleitet):** H.264 in ganzen Makroblockzeilen, höchstens 45 Slices: Slices = Zeilen / ⌈Zeilen
/ 45⌉. Das ergibt 360p 23, 540p 34, 720p 45, 1080p 34 (68 Zeilen, je 2), 1440p 45 (90, je 2), 2160p 45 (135, je 3),
genau die Werte in `STATUS.md`. HEVC: eine Slice je CTU-Zeile zu 64 Pixeln (6, 9, 12, 17, 23, 34). **Vermutung:** 10.x
hat dieselbe Vorgabe mit Obergrenze 45 statt 68, und die Felder `cfg+0x40..0x48`, die der Host dort setzt, liegen im
10.x-Datensatz woanders oder werden nicht übernommen.

**Wonach ein 10.x-Dump zu durchsuchen ist** (`SceAvCapture` und `libSceVenc` von 10.20 oder 10.40):

1. In SceAvCapture die Entsprechung von `0x18080`: liest ein u8 bei Block+8 und kopiert drei u32 nur, wenn sie ≠ 0 sind.
   Von dort zurück zum OpenVideo-Handler: Das ergibt den Offset des Slice-Blocks im 10.x-Datensatz.
2. Die Entsprechung von `0x14a00` und `0x18230` (Slice-Size-Control) und im `SetVideoEncodeConfig`-Handler den Zweig für
   Codec ≠ HEVC. Gibt es ihn, reicht vielleicht ein `SetVideoEncodeConfig` zur Laufzeit.
3. In libSceVenc die Konstanten `0x2bb0` zusammen mit `0x44` oder `0x2d` (45) in der Preset-Vorgabe und `lea eax,
   [rcx+rcx*2+1]` (Modus 4 oder 1 aus dem Codec). `immsearch.py` findet beides.
4. Die NID `6nGfjRPqCUM` (`sceVencSetSliceSizeControlConfig`) als Import oder Export.

**Klärender Live-Test:** zuerst der Dump (keine Messung an der Konsole nötig). Danach auf 10.x eine
`slice_rate_probe`-Variante mit dem gefundenen Offset und Anzahl 30; erwartet: H.264 1080p mit höchstens 30 Slices.

## 8. Weitere Schalter, AV1 (B.5)

| Schalter | Befund | Konfidenz |
|---|---|---|
| Level | `cfg+0x58`/`+0x5c` → Level-Index im Venc-Query (+0x2c), H.264 bis 16, HEVC bis 15, 0 = Preset-Vorgabe (9 bei 300/500/800). Remote Play und der Host setzen für HEVC 9, also die Vorgabe. | wahrscheinlich |
| Profil | folgt der Bittiefe (`cfg+0x20`); kein eigenes Feld gefunden | wahrscheinlich |
| Entropie (CABAC/CAVLC), Tier | kein Feld gefunden; im Preset fest | Vermutung |
| B-Frames | keine, der Stream hat nur I und P | gemessen |
| IDR-Abstand | `cfg+0x94` in Bildern | gemessen (`hevc.md`) |
| Konstante Qualität | auf 13.00 nur die Rate Control mit QP-Grenzen; auf 10.x/9.60 feste QPs | sicher (Felder), gemessen (10.x) |
| Low Latency | `B+6003nL0AI` (PictureConfig Typ 0, Wert 1 oder 2) bleibt unbekannt | Vermutung |
| Temporal SVC | in libSceVenc vorhanden, über Avcap2 nicht erreichbar | sicher |

**AV1: gibt es nicht. sicher.** Keines der Module (`libSceAvcap2`, `SceAvCapture`, `libSceVenc`, `SceRemotePlay`)
enthält `AV1`, `av01`, `aom` oder einen AV1-Klassennamen; die NID `tOrRi-v3AOM` enthält die Buchstaben nur zufällig.
SceAvCapture kennt nur die Codec-Klassen `AvcapVideoCodecAvc` und `AvcapVideoCodecHevc`.

**Der dritte Venc-Codec** (Venc-Codec 3, aus `cfg+0x24 = 2`) ist **wahrscheinlich VP9**: Ausrichtung auf 64 Pixel,
QP-Grenze 255, Level-Liste in `0xf500` genau die von VP9 (1.0 bis 6.2), kein Kopf (`0xf810` liefert 0). Moonlight hat
kein VP9, und SceAvCapture hat keine Klasse dafür. Für den Host ohne Nutzen.

**Unklar:** `cfg+0x2c4` (`0x6e`). Remote Play (`0x7041b`) setzt es bei `cfg+0x2c0 == 1` auf `0x6e` bei 60 fps und `0x37`
bei 30 fps; der Host setzt immer `0x6e`. `0x6e` ist auch ein Formatcode in `0x17e60`. Bedeutung: **Vermutung**, eine
Taktangabe, die halb so groß sein sollte, wenn nur 30 fps kodiert werden. Live-Test: HEVC 1080p30 mit `0x37` gegen
`0x6e`; erwartet bei richtiger Deutung ein gleichmäßigerer TSC-Abstand oder weniger Last.

**Klärender Live-Test (wahlweise):** `slice_rate_probe`-Variante mit `cfg+0x24 = 2`. Erwartet: OpenVideo oder Start
scheitert, weil SceAvCapture keine Codec-Klasse dafür hat. Öffnet es doch, die Datei auf VP9-Rahmen prüfen. Für AV1 gibt
es nichts zu testen.

## 9. Was der Host anbietet (B.6)

**serverinfo (sicher, Code; gemessen in früheren Läufen):** `ServerCodecModeSupport` = H.264 immer, HEVC ab
`active_hevc_mode ≥ 2`, Main10 bei 3 (`nvhttp.cpp` 1256–1286, 1347). Ein früherer Lauf auf 13.00 meldete 769 =
H.264 + HEVC + HEVC Main10. Kein AV1, kein 4:4:4. `MaxLumaPixelsHEVC` = 1869449984 (nur bei HEVC), `IsHdrSupported` bei
Modus 3.

**RTSP DESCRIBE (sicher, Code):** kein `refPicInvalidation` (siehe Abschnitt 5), `sprop-parameter-sets` nur für HEVC,
keine AV1-Zeile, solange `active_av1_mode = 1`. HEVC- und AV1-Anfragen lehnt Sunshine mit 400 ab, wenn sie aus sind
(`rtsp.cpp` 1290–1301).

**Bildrate:** Das GameStream-Protokoll kann keine Bildrate ablehnen. `maxFPS` und `clientRefreshRateX100` kommen vom
Client (`rtsp.cpp` 1198–1208), der Host liefert 29,97 oder 59,94 und meldet nichts zurück.

**Klärender Live-Test:** Testclient mit `--fps 120` und mit `--fps 24` (`run_smoke.sh`, `STREAM_ARGS="--no-script --fps
…"`). Erwartet: `video_fps_avg` 59,94 bzw. 29,97, im Host-Log `requested …@120, delivering …@60` bzw. `@24 … @30`, und
`serverinfo` mit `ServerCodecModeSupport` 769 auf 13.00 bei eingeschaltetem HEVC.

**Abweichungen zwischen Angebot und Wirklichkeit:**

1. H.264 wird auf 10.x angeboten, scheitert aber an Hardware-Decodern unter Windows (34 oder 45 Slices, FFmpeg nimmt
   32). gemessen.
2. Eine 120-fps-Anfrage bekommt still 59,94 fps. Sicher aus Code; im Log steht nur `delivering …@60`.
3. `patches/sunshine/0025-missing-frames.patch` behauptet im Kommentar, der Host biete `refPicInvalidation` an. Das
   stimmt nicht, daher bleibt `missing_frames` bei modernen Clients 0. sicher (Code).
4. HEVC auf ungeprüfter Firmware gibt es nur hinter dem Schalter in der App (gewollt).

## 10. Nächste Live-Tests

Geordnet nach dem, was sie freischalten. Nr. 3, 4, 6 und 7 laufen als Varianten von `src/probes/slice_rate_probe.cpp`
(bis zu drei Felder je Variante, speichert den Elementarstrom je Variante; 7 ist schon darin, die anderen sind neu).
Nr. 1 braucht eine Probe oder einen Host-Schalter, Nr. 2 einen Testbau des Hosts, Nr. 5 zuerst einen Dump.

| # | Test | Schaltet frei | Erwartet | Sonst |
|---|---|---|---|---|
| 1 | `SetInvalidFrame` nach 100 Bildern (neue Probe oder Host-Schalter), offline dekodieren | Verlust ohne IDR: kleinere Spitzen, keine HEVC-Lücken nach Verlust | `rc == 0`, kein IDR, Folgebild dekodiert ohne die verlorenen Bilder | IDR wie heute; RFI nicht anbieten |
| 2 | 10.x: IDR-QP = P-QP, HEVC 8 Bit wieder mit 21 bei 20 Mbit/s (Testbau, `encoder_qp.h`) | kleinere IDRs auf 10.x; zeigt, ob der Kanalverlust von Issue 50 am IDR liegt | IDR-Bytes etwa −30 bis −40 %, P-Bilder gleich; 25/25 hält, wo 21/25 verlor | zurück auf idr + 4 und 24/28 |
| 3 | 13.00: `cfg+0xd4` = 35/41/46/51 bei HEVC 1080p, 16 Mbit/s, bewegtes Bild | IDR-Größe steuerbar, Lücken nach IDR weg | IDR kleiner mit steigendem Wert | Feld ist kein QP-Deckel: Deutung streichen |
| 4 | `cfg+0x138` = 0x50, `0x140` = 1, `0x144` = 1 (H.264 und HEVC) | Long-Term-Referenz als Ziel für RFI | öffnet, Bildgrößen kaum anders | `0x80c7xxxx`: Felder falsch gedeutet |
| 5 | 10.x-Dump nach Abschnitt 7, dann Slice-Variante | H.264 auf 10.x unter Windows mit Hardware-Decoder | ≤ 30 Slices | Moonlight-Patch abwarten |
| 6 | HEVC 1080p30, `cfg+0x2c4` = `0x37` gegen `0x6e` | richtige Werte für 30 fps | gleiches oder ruhigeres TSC-Raster | Feld lassen |
| 7 | Timing 2 auf dem Startbildschirm (60 Hz) | bestätigt das Modell aus Abschnitt 4 | öffnet, 59,9 fps | Abschnitt 4 Schritt 2 neu lesen |
| 8 | `B+6003nL0AI(h, 0)` und `(h, 1)` | vielleicht ein Latenzmodus | Änderung im Abstand w9 − Lesezeit | streichen |

## 11. Ausgeschlossen

* **120/90 fps über Avcap2** (Abschnitt 4): kein Preset in libSceVenc, unabhängig von Timing, Auflösung und Codec.
* **24/25/48/50 fps**: dieselbe Tabelle; der Encoder kennt nur 29,97 und 59,94.
* **AV1**: kein Code in irgendeinem Modul.
* **Temporal SVC**: Fach wird nie gefüllt, kein Avcap2-Export.
* **Ein IDR-Größendeckel** auf 13.00: kein solches Feld; nur QP-Grenzen und das Frame-Dropping-Budget.
* **Intra-Refresh**: nicht gefunden (Vermutung, dass es keinen gibt).
