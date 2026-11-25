# Haptik als Rumble

Spiele für die PS5 steuern den DualSense über einen Audio-ähnlichen Haptik-Kanal und setzen die klassischen Motoren vermutlich nicht
(`pad-output.md`). Moonlight hat dafür keinen Kanal, aber die zwei Motoren. Der Host liest deshalb mit `haptic_mode` in
`sunshine.conf` während eines Streams `sceAvcap2ReadHaptic` und gibt den Pegel als Rumble weiter
(`poll_haptics` in `src/sunshine/input_ps5.cpp`, `src/platform/ps5/haptic_envelope.hpp`).

## Gemessen auf 13.00

Vom Maintainer mit `haptic_mode = rumble` (Review von PR 35): `OpenHaptic 0 2 2 <RA-Id> flag=0 rc=0`, `haptic Start rc=0`, etwa 0,3 s nach
dem Pad. `ReadHaptic` lieferte wie auf 9.60 47 Pakete je Sekunde zu 2048 Byte, ohne Fehler und ohne `haptic_blocked`; beim Ende des Streams
kamen `haptic Stop` und `haptic Close` mit `rc=0` vor Video. Mit echtem Moonlight unter Windows und einem Xbox-Controller war Teardown
spürbar (Spitzen bis zum Deckel 112/128). GTA III DE blieb auf dem Kanal still und setzt stattdessen die klassischen Motoren (bis 40/255),
die `MotorChoice` durchreicht.

Mit dem Testclient (Debug-Builds aus dem Speicher, `rumble pad=…`-Zeilen des Clients, 2026-11-22 und 23):

- **4K HEVC bei 80 Mbit/s** (Rauschseite im Browser, Ausgabe 60 Hz), je 90 s mit offenem und geschlossenem Kanal: zweimal offen,
  dreimal zu, in allen fünf Läufen 0 fehlende Bilder und kein `PS5 capture lost`.
- **Nur das Pad des Hosts:** Teardown mit zwei Pads, der Feuerlöscher abwechselnd von Pad 0 und Pad 1 bedient. Pad 0 bekam den Rumble,
  Pad 1 keine einzige Zeile, auch während Teardown Pad 1 folgte. `haptic_strength` 50 und 200 galten mitten im Stream; die Spitzen
  48, 32 und 96 von 255 entsprechen der Rechnung, die Deckel wurden nicht erreicht.
- **Flackern:** Nach dem ersten Sprühen schaltete der große Motor bis zum Ende des Streams alle 10 ms zwischen 0 und 16, etwa 100
  Meldungen je Sekunde an den Client. Ein schwaches, anhaltendes Signal fiel zwischen zwei Paketen (21 ms) unter die Schwelle und kam
  mit dem nächsten wieder. Seitdem fällt der Pegel einmal je Paket und hält in den Ticks dazwischen (unten).
- **Astro's Playroom** setzt die klassischen Motoren nicht: Mit `haptic_mode` aus kam beim Laufen weder Rumble noch eine
  `motors`-Zeile, mit `rumble` 324 Schritte je 75 s. Alt (je Tick fallend) gegen neu (je Paket fallend), je zwei Läufe je Szene:

  | Szene | Wechsel ≤10 ms nacheinander | Aus und wieder an | Länge der Pulse |
  |---|---|---|---|
  | Laufen, je Tick fallend | 704, 783 | 0, 0 | 41, 41 ms (Median) |
  | Laufen, je Paket fallend | 0, 0 | 0, 0 | 41, 42 ms |
  | Sprung und Schlag, je Tick | 64, 83 | 1, 0 | lange Klicks 286, 290 ms |
  | Sprung und Schlag, je Paket | 0, 0 | 0, 0 | lange Klicks 275, 276 ms |
  | gehaltener Trigger (Textur 16 bis 48 von 255), je Tick | 2299, 2287 | 19, 16 | ein Halten von 1,5 s in Stücken von 752, 812 ms |
  | gehaltener Trigger, je Paket | 1, 1 | 0, 0 | ein Stück, 1511, 1517 ms |

  Die Textur kommt auf der Seite „Adaptive Triggers“ der Controller-Demo im Titelmenü, solange ein Trigger gehalten wird. Je Paket
  fallend schickt der Host 38 bis 54 % weniger Meldungen; Klicks bleiben so lang wie das Signal des Spiels.
- **Beenden mit offenem Kanal** (`QUIT`): `haptic Stop` und `haptic Close` mit `rc=0` nach dem Ton und vor der Aufnahme, `exit_value=0`,
  kein `haptic_blocked`. Der nächste Host und sein Stream liefen normal, mit `rumble` wieder mit offenem Kanal.

## Gemessen auf 9.60

`sceAvcap2OpenHaptic(h**, cfg*0x18, u8)` öffnet mit dem Aufbau des ersten Blocks der Audio-Konfiguration: Größe `0x18`, Art 0, Zweck 2,
Encoder 2, RA-Id, Flag 0. Encoder 0 und 1 geben `0x81950001`. Danach endete der Host dreimal Millisekunden später, ohne dass ein Paket
im Log stand, ohne die Stelle zu zeigen.

Seitdem laufen Öffnen und Lesen in `ps_haptic_guarded`, und `haptic_attempt` legt `haptic_blocked` an, wenn der Host mit offenem Kanal
stirbt; ein Fehler im Aufruf legt sie direkt an. Beide Dateien liegen unter `platf::appdata()`, also in `/data/prosperosunshine/`. Solange
`haptic_blocked` da ist, bleibt der Kanal zu; der Host sagt es einmal im Log, sobald er ihn öffnen würde. Wer die Datei löscht, lässt ihn es
wieder versuchen.

Der Fehler war die Signatur. Der Code zeigt `sceAvcap2ReadHaptic(h, buffer, header, flags)`: Die Funktion ruft den gemeinsamen Ring-Reader
mit `buffer + 8`, Kapazität `0x800`, `header`, Headergröße `0x18` und `flags`. Mit der Form von `ReadAudio` wurde die Kapazität `0x10000`
zum Header, und der Lesezugriff schlug bei Adresse `0x10000` fehl (Signal 11).

In 2 Minuten 40 Sekunden Uncharted mit `min_log_level = debug` standen Trigger-Blöcke, aber keine Motorwerte im Log.

Danach kamen Daten, auch mit einem virtuellen Pad und mit Encoder 2: 47 Pakete je Sekunde zu 2048 Byte, also Float Stereo, kein Opus
(`opus_decode` meldete ein ungültiges Paket). Nur die ersten 64 Frames eines Pakets sind Samples, der Rest ist Füllung: In allen 381 aktiven
Paketen eines Mitschnitts lag die ganze Energie im ersten Viertel. 64 Frames bei 47 Paketen je Sekunde sind 3 kHz, die Rate der
Bluetooth-Haptik des DualSense. Beide Kanäle sind fast gleich. Es gibt Klicks von 20 bis 40 ms und Abklingen über 1 bis 2 s mit Spitzen um 0,8.

Die ersten Fassungen hielten alle 256 Frames für Samples, also 12 kHz. Das zerhackte die Wellenform 47-mal je Sekunde und ließ sie auf einem
Viertel der Zeit leise wirken. Die erste Fassung nahm außerdem Pakete mit winzigen Werten für 16 Bit und meldete daraus einen Pegel von 0,13: ein ständiger Rumble von 80 bis 96
von 255. Laut Besitzer war die Vibration damit spürbar, aber nicht punktgenau.

Moonlight bekommt zwei Motorwerte, also bleibt es eine Annäherung an die Haptik eines DualSense. `HapticEnvelope` nimmt Spitzen statt
Mittelwerte und lässt nach 2 bis 4 Paketen los (40 bis 90 ms); der Anteil unter 120 Hz geht zum großen Motor, der Rest zum kleinen. Der
Pegel fällt einmal je Paket auf das 0,36-Fache und hält in den 10-ms-Ticks dazwischen. Kommen keine Pakete mehr, hält er drei Ticks und
fällt dann je Tick auf das 0,6-Fache. In einem Gefecht erreichten die Spitzen 0,77 und der tiefe Anteil 0,49. Mit Quadratwurzel und voller Motorstärke fand der Besitzer das viel zu stark, daher linear,
höchstens 128 von 255, und was länger als eine Viertelsekunde anhält, klingt aus.

`haptic_strength` (Prozent, Vorgabe 100, höchstens 200) skaliert die Vibration. Was keine ganze Zahl in diesem Bereich ist, gilt als 100,
und `sunshine.log` nennt den Schlüssel (`src/input/pad_settings.hpp`, wie `trigger_knock_strength`). Der Host prüft alle 2 s, ob sich
`sunshine.conf` geändert hat (ein Wächter in `poll_pad_output` für alle drei Schlüssel); das gilt also Sekunden nach dem Speichern, auch im
laufenden Stream, ohne Neustart von Host oder Konsole. Er vergleicht Schreibzeit und Größe. Die Konsole führt die Schreibzeit in ganzen
Sekunden, und FTP kürzt die Datei vor dem Schreiben: Mit der Zeit allein las der Host einmal in 17 Speicherungen die halb geschriebene Datei
und sah danach dieselbe Zeit. Mit der Größe fehlte auf 13.00 keine von 48 Speicherungen, darunter 8 absichtlich so getaktete.
Weiter unbemerkt bleiben zwei vollständige Speicherungen gleicher Größe in derselben Sekunde mit einem Blick dazwischen (3 von 3); die
zweite gilt dann erst mit der nächsten Änderung.

Der Kanal gehört der Konsole, nicht einem Pad. Deshalb bekommt nur das Pad des Hosts (`input.roles`) seinen Rumble, und der Host liest ihn
nur, solange dieses Pad da ist; ein zweiter Spieler spürt sonst die Schüsse des ersten.

Ein Spiel mit eigenem Rumble behält es: `MotorChoice` nimmt den Kanal erst, wenn die klassischen Motoren 5 s auf null standen. UFC 3 (PS4) setzt
die Motoren, Uncharted lässt sie auf null.

Beim Beenden schließt der Host den Kanal zuerst, und der Steuerport geht erst nach den Streams und ihrer Aufnahme zu, damit ein neuer Host nicht auf
die Sitzung des alten trifft. Einmal auf 9.60 im Stream mit offenem Kanal geprüft: Die Konsole blieb an, Kanal, Ton und Aufnahme schlossen mit
`rc=0` vor dem Start des neuen Hosts.
