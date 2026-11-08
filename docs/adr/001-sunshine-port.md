# ADR-001: Sunshine als gepinnter Upstream mit PS5-Plattformschicht

Status: angenommen (28. Oktober 2026). Endgültig bestätigt, sobald der Host-ELF mit gelinktem Sunshine-Kern auf der Konsole startet (ROADMAP P0.6).

## Kontext

ProsperoSunshine soll für unveränderte Moonlight-Clients wie ein Sunshine-Host wirken. Sunshine (`LizardByte/Sunshine`, GPL-3.0-only) enthält das komplette GameStream-Protokoll: Pairing und Serverinfo (`nvhttp.cpp`), RTSP-Aushandlung (`rtsp.cpp`), Paketierung, FEC, Verschlüsselung und Steuerkanal (`stream.cpp`), Eingabeauswertung (`input.cpp`) und Opus-Audio (`audio.cpp`). Auf der PS5 liefert Avcap2 bereits fertig kodiertes H.264; es gibt keinen Desktop, keine Prozesse zu starten und keine Web-UI-Umgebung.

Nachgewiesen vor dieser Entscheidung (Details in `docs/status-archive.md`, „Nachweise nach Roadmap“):

- Toolchain: C++23, `std::jthread` mit `-fexperimental-library`, Sockets.
- Gebaut und live geprüft: OpenSSL 3.5.9, Opus 1.6.1, ENet (Moonlight-Fork), nanors, FFmpeg 9.0.2 minimal mit CBS (Sunshines eigenes `cbs.cpp` läuft unverändert auf der Konsole).
- Capture: 10 Minuten 59,94 fps, fünf saubere Zyklen, IDR alle 60 Frames, Hardware-Zeitstempel im Descriptor.
- Natives virtuelles Pad ohne Mono.

## Entscheidung

Sunshine wird im festen Commit `a679b059` (Hash in `deps.lock`) als Quellbaum gebaut. Übernommen werden die Protokolldateien möglichst unverändert; ersetzt werden genau die Nahtstellen, an denen Sunshine PC-Hardware oder Desktop-Funktionen anspricht:

| Nahtstelle | Upstream | PS5 (`src/sunshine/`) |
| --- | --- | --- |
| Video | `video.cpp` (Encoder-Pipeline) | `video_ps5.cpp`: Avcap2-Frames als `packet_raw_generic`, Zeitstempel aus dem Descriptor (TSC) |
| Prozesse | `process.cpp` | `process_ps5.cpp`: eine feste App „PS5 Screen“, Launch startet nichts |
| Plattform | `platform/linux/*` | `platform_ps5.cpp`: UDP-Versand, natives Pad, stummes Mikrofon bis R2 |
| Start | `main.cpp`, `entry_handler.cpp` | `main_ps5.cpp` plus Steuer-Socket (PIN, Status, Ende) |
| Desktop-Funktionen | `display_device.cpp`, `system_tray.cpp` | `stubs_ps5.cpp` (bewusst leer) |

Weggelassen: Web-UI (`confighttp.cpp`), UPnP, NVENC/VAAPI, libvirtualhid, curl. Eingriffe in übernommene Dateien liegen einzeln begründet unter `patches/sunshine/` und werden von `scripts/deps.sh` angewendet.

Abhängigkeiten folgen Sunshines eigenen Pins, wo vorhanden (Boost 1.92.0 aus `package-lock.cmake`, Submodule für moonlight-common-c, Simple-Web-Server, libdisplaydevice; FFmpeg-Release des Commits, den `build-deps` pinnt). Von libdisplaydevice wird nur der plattformneutrale Teil gebaut, weil `display_device.h` und `logging.cpp` dessen Typen und Logger verwenden.

## Verworfene Alternative

Nur die Protokolldateien in ein eigenes schlankes Gerüst übernehmen und Logging, Konfiguration und HTTP selbst stellen. Weniger Abhängigkeiten, aber jede Upstream-Änderung müsste von Hand nachgezogen werden, und Kompatibilitätsfehler wären wahrscheinlicher. Bleibt Rückfallebene, falls Boost oder der Gesamtbau nicht tragen.

## Folgen

- Kompatibilität kommt aus dem Upstream-Protokollcode; eigene Fehler beschränken sich auf die Nahtstellen.
- Upstream-Updates: Pin anheben, Patches neu anwenden, Nahtstellen gegen geänderte Signaturen prüfen.
- Sunshines `config.h` und FFmpegs internes `config.h` kollidieren; der Build bindet den Sunshine-Baum deshalb per `-idirafter` ein.
- Lizenz: Gesamtwerk GPL-3.0-only. Lizenztexte aller Bibliotheken liegen nach dem Build in `out/deps/licenses/` (MIT, BSD, LGPL-2.1 für FFmpeg ohne `--enable-gpl`, Boost Software License, OpenSSL Apache-2.0).
