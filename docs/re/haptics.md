# Haptik als Rumble

Spiele für die PS5 steuern den DualSense über einen Audio-ähnlichen Haptik-Kanal und setzen die klassischen Motoren vermutlich nicht
(`pad-output.md`). Moonlight hat dafür keinen Kanal, aber die zwei Motoren. Der Host liest deshalb mit `haptic_mode` in
`sunshine.conf` während eines Streams `sceAvcap2ReadHaptic` und gibt den Pegel als Rumble weiter
(`poll_haptics` in `src/sunshine/platform_ps5.cpp`, `src/platform/ps5/haptic_envelope.hpp`).

## Gemessen auf 13.00

Vom Maintainer mit `haptic_mode = rumble` (Review von PR 35): `OpenHaptic 0 2 2 <RA-Id> flag=0 rc=0`, `haptic Start rc=0`, etwa 0,3 s nach
dem Pad. `ReadHaptic` lieferte wie auf 9.60 47 Pakete je Sekunde zu 2048 Byte, ohne Fehler und ohne `haptic_blocked`; beim Ende des Streams
kamen `haptic Stop` und `haptic Close` mit `rc=0` vor Video. Mit echtem Moonlight unter Windows und einem Xbox-Controller war Teardown
spürbar (Spitzen bis zum Deckel 112/128). GTA III DE blieb auf dem Kanal still und setzt stattdessen die klassischen Motoren (bis 40/255),
die `MotorChoice` durchreicht. Offen: ob der offene Kanal bei 4K HEVC mit hoher Bitrate zu `PS5 capture lost` beiträgt.

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
Mittelwerte und lässt in etwa 60 ms los; der Anteil unter 120 Hz geht zum großen Motor, der Rest zum kleinen. In einem Gefecht erreichten
die Spitzen 0,77 und der tiefe Anteil 0,49. Mit Quadratwurzel und voller Motorstärke fand der Besitzer das viel zu stark, daher linear,
höchstens 128 von 255, und was länger als eine Viertelsekunde anhält, klingt aus.

`haptic_strength` (Prozent, Vorgabe 100, höchstens 200) skaliert die Vibration. Was keine ganze Zahl in diesem Bereich ist, gilt als 100,
und `sunshine.log` nennt den Schlüssel (`src/input/pad_settings.hpp`, wie `trigger_knock_strength`). Der Host prüft alle 2 s, ob sich
`sunshine.conf` geändert hat (ein Wächter in `poll_pad_output` für alle drei Schlüssel); das gilt also Sekunden nach dem Speichern, auch im
laufenden Stream, ohne Neustart von Host oder Konsole.

Der Kanal gehört der Konsole, nicht einem Pad. Deshalb bekommt nur das Pad des Hosts (`input.roles`) seinen Rumble, und der Host liest ihn
nur, solange dieses Pad da ist; ein zweiter Spieler spürt sonst die Schüsse des ersten.

Ein Spiel mit eigenem Rumble behält es: `MotorChoice` nimmt den Kanal erst, wenn die klassischen Motoren 5 s auf null standen. UFC 3 (PS4) setzt
die Motoren, Uncharted lässt sie auf null.

Beim Beenden schließt der Host den Kanal zuerst, und der Steuerport geht erst nach den Streams und ihrer Aufnahme zu, damit ein neuer Host nicht auf
die Sitzung des alten trifft. Einmal auf 9.60 im Stream mit offenem Kanal geprüft: Die Konsole blieb an, Kanal, Ton und Aufnahme schlossen mit
`rc=0` vor dem Start des neuen Hosts.
