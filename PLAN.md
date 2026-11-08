# ProsperoSunshine: Ziel, Ansatz und Regeln

Stand: 8. November 2026, nach Release 0.5.0. Der Host ist gebaut und streamt seit Release 0.1.0 (30. Oktober 2026). Dieses
Dokument beschreibt Ziel, technischen Ansatz und die verbindlichen Regeln für das PS5-Backend. Was als Nächstes kommt, steht
in `ROADMAP.md`, was live geprüft ist, in `STATUS.md`, was ausgeliefert ist, in `CHANGELOG.md`. Der Ausgangsbefund vom
28. Oktober 2026, die Meilensteine M0 bis M6 und die ersten Arbeitspakete stehen unverändert in
[`docs/roadmap-archive.md`](docs/roadmap-archive.md).

## Ziel

ProsperoSunshine stellt direkt auf einer PS5 einen Sunshine-kompatiblen Streaming-Host bereit. Ein unveränderter
Moonlight-Client verbindet sich mit der PS5, empfängt Bild und Ton und überträgt Controller-, Tastatur- und Mauseingaben
zurück. Ein PC dient beim Entwickeln und zur Diagnose, im Streaming-Betrieb vermittelt er nicht. Nach Sitzungsende sind alle
eigenen Ressourcen freigegeben.

## Technischer Ansatz

Sunshine ist bereits ein Host für Moonlight. ProsperoSunshine baut Sunshine auf einem festen Commit (`deps.lock`) mit kleinen,
begründeten Patches (`patches/sunshine/`) und ersetzt nur die Teile, die an PC-Hardware, Prozessstart oder Desktop hängen.
Die Entscheidung und die Nahtstellen stehen in `docs/adr/001-sunshine-port.md`. [Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/)

Die native Anwendung besteht aus Sunshines Protokollteil und einer PS5-Plattformschicht. Der Python-MCP-Server aus
`ps5-mcp` dient beim Entwickeln zum Deployment und zur Diagnose. Er transportiert im Host keine Medien.

```text
PS5 Avcap2 → H.264-/HEVC-Frames → begrenzte Warteschlange → Video-Transport → Moonlight
PS5 Audio → PCM → Opus → Audio-Transport → Moonlight
PS5 virtuelles Pad, Tastatur und Maus ← Eingabeübersetzung ← Steuerkanal ← Moonlight
                        Sitzungsmanager und gemeinsame Zeitbasis
```

Avcap2 liefert bereits komprimiertes Video. Der Host verpackt diese Frames direkt; ein erneutes Decodieren und Encodieren
wäre zusätzliche Last und Latenz. FFmpeg ist nur minimal gebaut, für Sunshines Coded-Bitstream-Teil (`cbs.cpp`).
Upstream kennzeichnet Sunshine als `GPL-3.0-only`; Lizenztexte, Herkunft und Änderungen stehen in `docs/licenses.md` und
`docs/THIRD_PARTY.md`.

## Verbindliche Regeln für das PS5-Backend

- Firmware 13.00 und 10.20 sind geprüft. Andere Versionen nur, wenn die ResourceArbitrator-Aufrufe per NID auflösen, und dann
  als ungeprüft im Log; HEVC, HDR, große und kleine Modi und hohe Bitraten dort nur hinter den Experimental-Schaltern. Vor
  einem Port `docs/firmware-porting.md` lesen.
- Den Avcap2-Datensatz je Firmware aus `docs/firmware-porting.md` nehmen (13.00: `0x2E0` Bytes, 10.x: `0x2C8`). Die
  Datensätze von 13.00 und 10.20 bleiben byte-gleich (`tests/capture_records.golden`). Ältere SharpProspero-Strukturen mit
  `0x2B8` Bytes passen nicht unverändert.
- `0x2A4 = 2` für den zusammengesetzten Bildschirm beibehalten. Wert `3` erfasste nur die App und führte im Home-Menü zu
  schwarzen Bildern.
- Profil-IDs (180, 182 über 1080p, 175 für den zweiten Kanal) sind ResourceArbitrator-Ressourcen, keine Benutzer-IDs. Ist
  eine bereits belegt, den Start ablehnen und die Belegung melden.
- Beim Anfordern auf die Freigabe warten, Zeitüberschreitung begrenzen und einen späteren Ressourcenentzug behandeln. Keine
  fremde Sitzung oder Systemprozesse beenden.
- Speicherverwaltung beim Dienst belassen. Externe Frame-Puffer sind nicht nötig.
- Alle Erfolgs- und Fehlerpfade müssen eigene Channels stoppen und schließen, Avcap2 terminieren und die eigene
  Ressourcenregistrierung freigeben. Bei Teilinitialisierung nur tatsächlich erworbene Ressourcen aufräumen.
- Bei Disconnect oder Sitzungsfehler alle gedrückten Eingaben freigeben und das eigene virtuelle Pad sauber lösen.

Für `sceAvcap2ReadVideo` gilt: positiver Rückgabewert ist die kopierte Bytezahl, `0x81950002` bedeutet noch kein Frame. Der
Descriptor ist `0x70` Bytes groß. SPS/PPS kommen zusätzlich über `sceAvcap2GetNonVclNalUnitStream`. Weitere ABI-Details
stehen in `src/platform/ps5/capture.c` und `docs/`; dort nachlesen, statt sie neu zu erraten.

## Messungen und praktische Tests

1. Per LAN testen und Clientversion, Auflösung, angeforderte Bildrate und Bitrate festhalten.
2. Home-Menü, Homebrew und mindestens ein reguläres Spiel prüfen. Der Home-Bildschirm fordert den Encoder kaum.
3. Tatsächlich empfangene und dargestellte Frames messen. SPS-Metadaten allein sind kein Nachweis für eine stabile Bildrate.
4. Capture-, Queue- und Transportzeiten getrennt erfassen. Ende-zu-Ende-Latenz über eine geeignete gemeinsame Messung
   bestimmen; Zeitstempel verschiedener Geräte nicht ungeprüft voneinander abziehen.
5. Paketverlust, Bandbreitenbegrenzung, Kabelziehen und Clientabbruch gezielt auslösen. Wiederherstellung und
   Ressourcenfreigabe kontrollieren.

Die MCP-Werkzeuge unterstützen Deployment, Logs und Zustandsprüfungen. Galerie-Screenshots eignen sich zur Sichtkontrolle,
nicht zur Streaming-Latenzmessung. Ergebnisse gehören mit Messweg in `STATUS.md`.
