# ProsperoSunshine Roadmap

Stand: 8. November 2026, nach Release 0.5.0. Dieses Dokument sagt, was als Nächstes kommt. Was ausgeliefert ist, steht in
`CHANGELOG.md`, was live geprüft ist, in `STATUS.md` (Kurzfassung und „Offen und bekannte Grenzen“). Ziel und Regeln für das
PS5-Backend: `PLAN.md`. Bei Widersprüchen gilt für Fakten `STATUS.md`, für Reihenfolge und Abnahme dieses Dokument.

Die erledigten Pläne (Phasen 0 bis 9 mit den Paketen P0.1 bis P9.11 und den Spikes R1 bis R6, Release 0.1, Zielbild v1.0,
Meilensteine, der Plan für Tastatur und Maus als Controller, die Übersicht je Release) stehen unverändert in
[`docs/roadmap-archive.md`](docs/roadmap-archive.md). Verweise wie „ROADMAP P9.1“ in Code und Doku meinen die Pakete dort.

Eine Aufgabe ist erst erledigt, wenn ihr Nachweis in `STATUS.md` steht; auf der PS5 heißt das ein Live-Lauf mit Log, nicht
nur ein erfolgreicher Build.

## Nächste konkrete Schritte

1. **Menüs je Profil (PR #38):** nach 0.5.0 auf `main`, mit dem Testclient auf 13.00 gemessen (`STATUS.md`,
   „Konsolenmenüs, PR 38“). Vor dem nächsten Release noch mit echtem Moonlight.
2. **Zweiter Aufnahmekanal** (seit 0.2.4) weiter prüfen. Auf 13.00 gemessen: gleicher Modus teilt Kanal 0, H.264 neben HEVC,
   1080p neben 720p und SDR neben HDR auf Kanal 1, das erste Gerät geht zuerst, ein drittes Gerät, die Übernahme in
   Kanal 1, 4K für Kanal 1 und 5 Minuten Kanal 1 allein (`docs/status-archive.md`, „Zweiter Aufnahmekanal im Host“). Offen:
   - echtes Moonlight auf mehreren Geräten;
   - 10.x: ob Profil 175 dort neben 180 aufgeht. Schlägt es fehl, tritt das Gerät wie in 0.2.3 bei oder wird abgewiesen.
   Eine eigene Bitrate je Gerät auf demselben Kanal geht nicht, beide Kanäle teilen bei gleichem Codec und gleicher
   Auflösung den Encoder (`docs/status-archive.md`, „HDR-Metadaten, Slices, 120 fps, zweiter Kanal“).
3. **120 fps:** `cfg+0x1c = 2` lehnt OpenVideo auf 13.00 mit 0x80c70004 ab (`slice_rate_probe`, `docs/status-archive.md`).
   Weitere Spuren nur mit einem Titel, der 120 Hz ausgibt, und über die Prüfroutine in `docs/re/avcap2-exports.md`.
4. **Raumklang:** seit 0.3.1 hinter dem Schalter „Surround“, mit angenommener Reihenfolge (`docs/status-archive.md`, „Für
   0.3.1“). Offen ist, welcher der 12 Kanäle welcher Lautsprecher ist; das braucht eine Konsole, die 5.1 oder 7.1 ausgibt.
   Der Host loggt alle 30 s die Pegel der 12 Kanäle (`12-channel levels`); ein Bericht damit und mit dem, was aus welchem
   Lautsprecher kam, bestätigt oder korrigiert `src/audio/channel_map.hpp`.
5. **13.60 (Issue #1):** Der Host wartet seit 0.2.3 bis zu 6 s auf die Zuteilung und schreibt `RUNNING granted after N ms`,
   wenn es länger dauert. Ein Bericht damit zeigt, wie lang die Zuteilung dort braucht. Danach die Checkliste in
   `docs/firmware-porting.md` für 13.60.
6. **10.x:** 10.20 ist geprüft, 10.40 streamt HEVC laut Nutzerbericht (Issue #2). Offen: H.264 kommt dort in 34 Slices je
   Bild, und die Hardware-Decoder unter Windows nehmen höchstens 32 (weißes Bild). Seit 0.3.1 setzt der Host dort die
   Slice-Felder von Remote Play (`cfg+0x40/0x44/0x48`, Offsets aus SceRemotePlay von 13.00); 10.20 nimmt sie an und
   schneidet trotzdem 34 Slices. Ein Mitwirkender hat dafür einen Patch an FFmpeg und an moonlight-qt-deps geschickt
   (November 2026, Discord), damit Moonlight für Windows solche Bilder dekodiert. Sobald er in einem Moonlight-Release ist:
   H.264 auf 10.x mit Moonlight für Windows nachprüfen und README und `STATUS.md` anpassen. Das Feld, das auf 10.x die
   Slice-Zahl setzt, bleibt eine Alternative (eine Sonde, die freie Wörter im Encoder-Block durchprobiert, oder
   SceRemotePlay von 10.20). Bis dahin HEVC empfehlen.
7. **Ruhemodus** mit offenem Spiel und laufendem Stream: Der Neustart der Dienste endet dort nicht in `host ready`
   (`docs/status-archive.md`, „Stand 30. Oktober 2026“). Das ist zu klären. Offen sind außerdem das Aufwecken per Controller
   und der einmalige Hänger im ersten Test. Wecken aus Moonlight geht nicht (`docs/status-archive.md`, „Wecken aus
   Moonlight“, Issue #10). Tests nur nach Rückfrage.
8. **Längere Spielsitzung** in 4K mit HEVC in Moonlight: Am 3. November 2026 lief sie beim Besitzer länger ohne Probleme
   (Bericht, Logs nicht ausgewertet). Ein ausgewertetes Log einer mehrstündigen Sitzung fehlt noch.
9. **Weitere Clients (P9.6):** Steam Deck und Shield sind ungeprüft. Co-op mit echtem Moonlight auf mehreren Geräten ist von
   einem Tester belegt (vier Spieler auf 10.20, Issue #11), hier nur mit dem Testclient. Noch ungeprüft ist, wie Moonlight auf
   dem Handy die Meldung bei falschem Dynamikbereich zeigt.
10. **HDR** weiter prüfen:
    - andere Clients und Bildschirme, wegen der festen HDR10-Werte statt echter Metadaten;
    - ein Spiel, das sicher HDR ausgibt;
    - die Matrix aus `docs/re/hdr.md`, Schritt 5 (die Einstellung ändert der Nutzer);
    - ein Gerät ohne HDR, das einem HDR-Stream beitritt.
    Außerdem Touch und Bewegung in einem Titel, der sie nutzt.
11. **Seltene Fehlstarts:** 1 in 86 Starts im Docker-Client, 0 in 300 im Windows-Client, davon 150 mit 0.2.4
    (`docs/status-archive.md`). Ohne neuen Fehlstart gibt es hier nichts zu suchen; das Docker-Netz bleibt der
    wahrscheinlichste Grund.
12. **Adaptive Trigger (P9.10):** Moonlight für PC müsste die Effekte beim Beenden zurücksetzen
    (`docs/re/adaptive-triggers.md`).
13. **Gäste und Profile (Issue #15):** Seit 0.3.2 gehört die Host-Rolle einem gepairten Gerät statt dem ersten Slot
    (`src/input/pad_roles.hpp`, Patch 0016), und Gäste haben weder PS noch Create. Seit 0.5.0 legt die Webseite je Gerät die
    Konsolenprofile fest, als die es spielt, oder dass es nur zuschaut (PR #33). Offen, mit einer Probe auf der Konsole: ob der
    Host einen echten Controller an der Konsole erkennt, was ein Stream-Pad mit zwei DualSense an der Konsole macht, und ein
    echter Controller, der über die Benutzerauswahl der Konsole dazukommt. Danach, falls noch nötig, ein Schalter „Guests can
    only play“.
14. **Webseite (Issue #21):** seit 0.4.0 im Release, auf 13.00 gemessen (`STATUS.md`, „Webseite“). Offen:
    - die Seite im Browser am PC (am Handy angesehen, ohne Befund);
    - PIN-Eingabe, „End stream“ während eines Streams, Umbenennen und Entfernen über die Seite;
    - „Save and restart host“, 403 von einer Adresse außerhalb, die Seite nach echtem Ruhemodus.
    Später, falls gewünscht: Geräte sperren, die Host-Rolle je Gerät auf der Seite zeigen.
15. **Tastatur und Maus als Controller (P8.10):** seit 0.3.3 im Release. Offen sind die Einstellungen dazu in der App und eine
    Seite, auf der man einem installierten Spiel ein Profil gibt; heute geht das nur über `sunshine.conf` und
    `keyboard_profiles.txt` (`docs/keyboard-profiles.md`). Ungeprüft in einem Spiel sind die Profile
    `driving`, `fighting`, `platformer` und `sports` und Firmware 13.00 (`docs/status-archive.md`, „Tastatur und Maus als
    Controller“).
16. **Große Dateien aufteilen,** sobald die offenen PRs gemergt sind: `src/sunshine/platform_ps5.cpp`,
    `app/src/psapp/screen.cpp` und `src/platform/ps5/capture.c` in kleinere Module. Verhalten bleibt gleich; beide
    Testsuiten und ein Stream auf der Konsole vor dem Merge.
17. **Formatierung für neuen Code:** eine `.clang-format` und eine `.editorconfig`, passend zum bestehenden Stil, nur für neuen
    oder geänderten Code, damit die Diffs alter Dateien klein bleiben.

## Bis 1.0

Aus den Meilensteinen (`docs/roadmap-archive.md`, „Meilensteine“) gilt weiter das Ziel für 1.0: eine klare Zusage, was
unterstützt ist. Fertig, wenn es eine Liste geprüfter Firmwares und Clients gibt, eine mehrstündige Sitzung in mindestens zwei
Spielen ohne Ausfall gelaufen ist, der Ruhemodus belegt oder klar ausgeschlossen ist und jemand ohne Vorwissen den Host nach
Anleitung aus dem Store neu installiert hat.

Erledigt seit 0.1.0: 60 fps, Moonlight-Discovery per mDNS, HEVC, HDR, 1440p und 4K, Modi unter 1080p, Rumble, Lichtleiste
und adaptive Trigger, mehrere Spieler und ein eigener Modus je Gerät über den zweiten Aufnahmekanal, Tastatur und Maus.

Weiter offen: weitere Firmware-Versionen (Checkliste `docs/firmware-porting.md`; ResourceArbitrator per NID, Rest live zu
prüfen).

Bewusst nicht geplant: Haptik (Moonlight hat keinen Kanal dafür); harte Trigger nach dem Stream lassen sich nur in Moonlight
beheben. AV1, Mikrofon und Internetzugriff durch den Host selbst (Moonlight über das Internet geht mit der eigenen
Portfreigabe oder Tailscale).

## Risiken

| Risiko | Wirkung | Gegenmaßnahme |
| --- | --- | --- |
| Encoder-Last über 50 Mbit/s | Am Testclient mit Rauschbild bis rund 151 Mbit/s sauber, in einem Spiel oder mit echtem Moonlight nicht gemessen | Klemme auf 150 Mbit/s nur auf 13.00, sonst 50; das Nutzer-Setting von Moonlight entscheidet, wie viel ein Client anfragt |
| Firmware-Update ändert Offsets oder Strukturen | Capture fällt aus | ResourceArbitrator per NID mit Tabellenabgleich, Ablehnung bei Fehlschlag; Avcap2-Datensatz und Pad je Version live prüfen (`docs/firmware-porting.md`, `firmware_probe.elf`) |
| Avcap2 im Spiel verweigert | Manche Titel bekommen kein 1440p oder 4K | Rückfall auf 1080p im selben Stream, Meldung statt Absturz |
| Upstream-Drift bei Sunshine | Aufwändige Updates | Fester Commit, kleine dokumentierte Patches, Updates nur gezielt |
