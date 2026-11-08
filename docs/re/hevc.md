# HEVC über Avcap2 (P9.7)

Stand 2026-11-01, Firmware 13.00; 10.x im Abschnitt „Firmware 10.x“. `src/hevc_spike.cpp` ist der Spike, der Host nutzt den Weg über `video_ps5.cpp`; die Messwerte stehen in `docs/status-archive.md`.

## Ergebnis

Avcap2 liefert HEVC Main (8 Bit, 4:2:0, BT.709, nur I- und P-Bilder) in 1080p (Level 5.0), 1440p und 4K (Level 6.0). Über 1080p braucht es einen anderen Zweck als Remote Play, siehe „1440p und 4K“. Der Host bietet HEVC auf Firmware 13.00 und 10.20 an (`ps_capture_hevc_verified`, siehe „Firmware 10.x“), auf anderen nur H.264, außer mit dem experimentellen Schalter (`docs/firmware-porting.md`).

## Was nötig war

Auf 13.00 reicht `cfg+0x24 = 1` allein nicht: `sceAvcap2OpenVideo` meldet 0, `sceAvcap2Start` dann `0x80c78000`. `ps_capture_open`
macht für HEVC zusätzlich drei Dinge wie Remote Play (`SceRemotePlay 0x6f7f0`, Bitraten `0x70130`):

1. **Encoder-Speicher reservieren** (Memory Mode 1): `cfg+0x198 = 0x50`, `cfg+0x1a0 = 1` und vor dem Öffnen der Import
   `yN0w4tWivqs(in*0x90, out*0x20, out*0x20)`. Die Eingabe enthält die Größe `0x90`, ab `+8` eine Kopie von `cfg+8..+0x30`,
   ab `+0x30` eine Kopie von `cfg+0x30..+0x88`. Die beiden Ausgaben landen in `cfg+0x1a8` und `cfg+0x1c8` (je Größenfeld,
   `0x3c`, Länge, Adresse; gemessen `0x3e8000` und `0x1fec000` Byte). Freigabe: `96UQhNnBlxc` nach `Close`, vor `Terminate`.
2. **Bitrate in die Konfiguration:** `cfg+0xc8` Mittel, `+0xcc` Spitze, `+0xd0` Bit je Bild, `+0x10c` Byte je Bild
   (Spitze × 1001 / (fps × 1000)).
3. **Die übrigen Felder von Remote Play**, siehe `configure()`. Das ist nicht die kleinste nötige Menge. Bekannt ist nur,
   dass der Start ohne diese Felder scheitert, auch mit reserviertem Speicher: Die Kombinationen „nur HEVC-Felder“ und
   „alle Felder außer den Quantisierer- und Endwerten“ (`+0xd4`, `+0x220..`, `+0x2b8..`) ergaben beide `0x80c78000`.

Das Ressourcen-Profil ist nicht die Ursache: 175, 177, 180, 182 und 185 scheiterten ohne die Punkte 1–3 gleich.

## Firmware 10.x

Gemessen auf 10.20 mit einer Sonde auf dem Datensatz von 10.x (`docs/firmware-porting.md`): `cfg+0x24 = 1` allein startet HEVC,
mit 8 und 10 Bit, 30 und 60 fps. Die Speicher-Reservierung (an `0x180`, wo 10.x das `0x198` von 13.00 hat) und die
Remote-Play-Felder außerhalb des Blocks `0xb8` ändern nichts; die inneren Felder an den Stellen von 13.00 lehnt der Validator ab
(`0x81950001`). Der Host lässt auf 10.x deshalb beides weg und bietet HEVC auf 10.20 an; andere 10.x-Versionen sind ungemessen. Jedes Bild hat 17 Slice-Segmente (H.264 dort: 34). 10 Bit ergibt
Main 10 mit BT.2020 und PQ. Die Quantisierer-Felder von 10.x (`cfg+0xc4` IDR, `cfg+0xc8` P) wirken auch bei HEVC.

Auf einem Windows-PC (FFmpeg mit `err_detect=explode`, wie Moonlight) decodieren D3D11VA, DXVA2 und D3D12VA alle HEVC-Bilder von
10.20; das H.264 von 10.20 scheitert dort an jedem Bild, weil FFmpeg für H.264 höchstens 32 Slices annimmt (`MAX_SLICES`), für
HEVC 256.

## Lesen und Header

* Lesen geht nur mit `sceAvcap2ReadVideo2`; `sceAvcap2ReadVideo` ist AVC-only. Im Descriptor (0x88 Byte) gemessen: `+0x2c`
  Bildtyp (1 = IDR, 3 = P), `+0x30` Bildnummer ab 1, `+0x38`/`+0x40` Sekunden/Nanosekunden, `+0x4c`/`+0x50` Breite/Höhe,
  `+0x58` TSC-Stempel (beim alten `ReadVideo` `+0x48`), `+0x6c` Bytes des Bildes.
* `sceAvcap2GetNonVclNalUnitStream` liefert die Parameter in der Reihenfolge **SPS, PPS, VPS** (53, 11 und 28 Byte,
  inklusive Startcode). `ffmpeg` decodiert erst, wenn das VPS vorn steht, und `moonlight-common-c` erkennt daran den
  Anfang eines IDR. `ps_capture_headers` sortiert sie um.
* IDR-Bilder (NAL-Typ 20) tragen keine Parameter-Sets. Eine `RequestKeyFrame`-Anforderung ergab teils zwei IDR hintereinander.

## Grenzen

* **Bitrate nur senken:** `SetVideoBitrate` über den Wert beim Öffnen hinaus tötet den Kanal, das nächste `ReadVideo2`
  liefert `0x80c78000`. Gemessen: 20 → 40 Mbit/s bricht ab; 30 → 10 → 30 Mbit/s läuft durch.
* **Obergrenze bei extremem Bild:** H.264 hält die Bitrate bei Rauschen über die ganze Fläche nicht ein (1080p 35–39, 4K rund 132 Mbit/s, unabhängig
  von der Vorgabe); bei Rauschen auf einem Viertel der Fläche folgt sie der Vorgabe.
* **HEVC über 50 Mbit/s (13.00, 4K, je 15 s, Testclient):** HEVC folgt bei vollem Rauschen der Vorgabe: 51 → 73, 102 → 102, 154 → 151 Mbit/s; HDR 102 → 99. Bei Rauschen auf einem Viertel der Fläche bleibt es darunter (51 → 45, 81 → 57, 102 → 63, 154 → 67), HDR 102 → 36. Keine fehlenden Bilder, längste Lücke höchstens 34 ms. Der Host klemmt auf 13.00 deshalb auf 150 Mbit/s, auf anderer Firmware weiter auf 50, weil das Öffnen mit hoher Bitrate dort nicht gemessen ist (`ps_capture_high_bitrate_verified`), außer mit dem experimentellen Schalter (`docs/firmware-porting.md`).
* **Last nur begrenzt gemessen:** Getestet mit dem Home-Bildschirm, der Demo von Mario 64 und Rauschseiten im Browser. Mario erzeugte bei 100 Mbit/s
  nur rund 33 Mbit/s. Ob ein aufwendiges Spiel und ein echter Moonlight-Client über 50 Mbit/s sauber laufen, ist offen.

## 1440p und 4K

Mit Zweck 2 (`cfg+0x14`, Remote Play) scheitert die Speicher-Reservierung in Modus 5 und 6 mit `0x81950003`, auf allen getesteten RA-Profilen
(180, 177, 185, 175, 182). Ursache (statisch, `SceAvCapture`): Der Manager `0x17e0` nimmt bei `cfg+0x14 == 2` und `cfg+0x18` 5 oder 6 den Weg `0xa7a0`, der
zwei Speicherpools einblendet. Die legt `0xa000` beim Start des Dienstes an (16 MB und 240 MB im festen Bereich `0x1100000000..0x1200000000`). Bleibt ihr Wert auf
der Vorgabe `0x5000000000` oder ist der Pool schon belegt (`0xb5c0`), kommt `0x81950003`. Warum die Pools hier fehlen, ist nicht geklärt. Alle anderen Fälle
reservieren je Anfrage (`0x16c0`, `0xa610`).

Der Host nimmt deshalb für HEVC über 1080p **Zweck 6**. Gemessen mit `src/hevc_memory_spike.cpp` und RA-Profil 182:

| Zweck | Öffnen in Modus 5 und 6 | GTA III, 6 s, 30 Mbit/s, Modus 6 |
| --- | --- | --- |
| 0 VideoRecording, 3 SocialScreen, 4 Shareplay | `0x81950008` | – |
| 1 GameLiveStreaming | Bilder | IDR 8–51 statt 1, Verzögerung 22–27 ms: fällt raus |
| 2 Remote Play | `0x81950003` | – |
| 5 ShareScreen, 6 VideoChat, 7 PS Now | Bilder | 359–360 Bilder, 1 IDR, 18 ms |

Zum Vergleich Zweck 2 in Modus 4: 363 Bilder, 23 ms. Unter den dreien gibt die Tabelle Zweck → Voreinstellung (`0x268f0`: `100, 200, 300, 400, 700, 500, 300, 500, 800`) den Ausschlag:
**Zweck 6 hat dieselbe Voreinstellung 300 wie Remote Play**, 5 und 7 haben 500. 1080p und H.264 bleiben bei Zweck 2, H.264 öffnet dort mit den Profilen 177 und 182
(`docs/re/avcap2-exports.md`, Abschnitt 8.1).

Der Validator `0x17aa0` lässt `cfg+0x14 < 8` zu, 8 nur mit Sonderprüfung. In manchen Titeln verweigert die Konsole 4K unabhängig vom Codec; dann fällt der Host auf 1080p und damit
auf Zweck 2 zurück. Dasselbe gilt, wenn `OpenVideo` oder `Start` in Modus 5 oder 6 scheitert. Nach PlayBridge und PBS ist das der Fall, wenn die Bildausgabe der Konsole kleiner ist als der Modus (`0x81950008`). Selbst nachgestellt ist nur das Scheitern von `Start` mit Modus 6 auf Profil 180, nicht eine kleine Ausgabe.

Die Systemmeldung „Gameplay recording paused because you entered a blocked scene“ erschien im Vorspann von GTA III mit HEVC Zweck 5, 6, 7 in Modus 6 und ebenso mit H.264 Zweck 2 in Modus 6,
nicht in Modus 4 (HEVC Zweck 2 und 6, je 40 s, jedes 200. Bild angesehen). Sie hängt also am Modus, nicht am Zweck. In beiden Fällen stand sie auf Bild 200 nach dem Öffnen, an verschiedenen
Stellen des Vorspanns; sie folgt demnach dem Öffnen, nicht der Spielszene. Vermutung: Das Profil 182 (`prio[1]`) nimmt der Hintergrundaufnahme der Konsole die Mittel. Nicht geprüft. Sie liegt als Einblendung im Bild, die Bilder laufen weiter.

## Schlüsselbild-Abstand und ausgelassene Bilder

`cfg+0x94` ist der Abstand periodischer IDR in Bildern. Gemessen mit einem Spike, der das Feld überschrieb (je Kanal 8 oder 45 s
Home-Bildschirm, in der Mitte ein `RequestKeyFrame`): 120 ergibt 120, 600 ergibt 600. 30 ergibt 60, also den bisherigen Takt von
einer Sekunde. Bei 0 und 3600 kam im Messfenster kein periodisches IDR. `RequestKeyFrame` wird bei jedem Wert nach 1–3 Bildern bedient.

Der HEVC-Encoder lässt nach einem IDR Bilder aus, um dessen Bytes auszugleichen; die Bildnummern laufen dabei lückenlos weiter.
Mit dem alten Abstand von einer Sekunde, gemessen über die Zeitstempel am Client: bei 15 und 20 Mbit/s ein Bild je IDR (Abstand
33 ms, 59 statt 60 fps), bei 5 Mbit/s fünf Bilder (100 ms, 54 fps). H.264 lässt keine aus. Das Überschreiben von `cfg+0x10c`
(Byte je Bild) mit dem Vierfachen beseitigte die Lücken im selben Spike, auch bei 5 Mbit/s, ebenso `cfg+0xd4 = 51` bei 15, nicht
bei 5 Mbit/s.

Der Host setzt deshalb `cfg+0x94` auf zehn Sekunden (`fps * 10`), für beide Codecs, und fordert zu Beginn des Streams ein IDR an, weil
der Tonkanal den Kanal schon früher geöffnet haben kann. Clients fordern nach Verlust ohnehin selbst eines an.

`cfg+0x10c` ist ein Budget: PlayBridge (öffentlich, 13.60, H.264) beschreibt, dass ein Bild, das N Budgets kostet, N−1 Bilder auslässt, und setzt 1 MiB. Bei HEVC auf 13.00 startet das
nicht in jedem Modus. Gemessen je Modus mit 5 und 50 Mbit/s, ohne Unterschied zwischen den Bitraten, 0x20000 bis 0x100000:

| Modus | startet bis | `Start` meldet `0x80c78000` ab |
| --- | --- | --- |
| 4 (1080p) | `0x60000` | `0x80000` |
| 5 (1440p) | `0xc0000` | `0x100000` |
| 6 (4K) | `0x100000` (mehr nicht geprüft) | – |

Unter 1080p, gemessen mit 3 Mbit/s (`mode_spike`, `docs/status-archive.md`). Zwischen den genannten Werten ist nichts geprüft:

| Modus | startet bis | `Start` meldet `0x80c78000` ab |
| --- | --- | --- |
| 1 (360p) | `0x20000` | `0x60000` |
| 2 (540p) | `0x20000` | `0x30000` |
| 3 (720p) | `0x40000` | `0x60000` |

Der Host setzt diese Höchstwerte. Die Bitrate hält trotzdem, Messwerte in `docs/status-archive.md`.

Unter 1080p gibt es zusätzlich eine Grenze für die Bits je Bild, die auch H.264 trifft und nichts mit `cfg+0x10c` zu tun hat: Ab dort meldet `Start` ebenfalls `0x80c78000`. Der Host deckelt die Bitrate je Modus (`max_average_of`, Messwerte in `docs/status-archive.md`).

Nicht geprüft: Die öffentlichen Quellen nennen `cfg+0x5c = 4` statt 9 bei 4K, damit H.264 unter Level 6.0 bleibt (ps5-web-stream). Bei uns meldet `ffprobe` in 4K für beide Codecs Level 6.0. Ob Clients daran scheitern, ist nicht gemessen.

## H.264

Das Öffnen mit der Bitrate in `cfg+0xc8/0xcc/0xd0` hilft auch H.264: 20, 30 und 50 Mbit/s liefen im Spike ohne Fehler, während
`SetVideoBitrate` ab 16 Mbit/s den Encoder stoppte. Für H.264 reichen diese drei Felder, Speicher-Reservierung und die übrigen
Felder braucht es nicht. Der Host öffnet daher beide Codecs so und ruft `SetVideoBitrate` nicht mehr auf.

## `AvActivation`

`libSceVenc` ruft beim Anlegen eines HEVC-Encoders (`0xa680`) `AvActivation` mit dem Typ `HwHevcEncode` (`0x10001`) und einer
Titel-ID auf, die aus der Voreinstellung folgt (`0xbac0`; Voreinstellung 300, also `cfg+0x14` 2 und 6, ergibt `NPXS40102`, Remote Play). Das Ergebnis wertet
die Funktion nicht aus (`0xba07`). Es ist also keine Sperre, nur eine Meldung. Ob es Folgen außerhalb der Konsole hat, ist
nicht bekannt.
