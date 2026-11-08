# Ausgabe eines virtuellen Pads: Rumble (FW 13.00)

Stand: 31. Oktober 2026. Statische Analyse der entschlüsselten `libScePad` und `SceRemotePlay` (Werkzeuge in
`tools/re/`, kein Sony-Code im Repository), dazu Live-Messung mit `src/probes/pad_output_probe.cpp` und dem Host.

## Woher die Werte kommen

`SceRemotePlay` liest für jedes seiner virtuellen Pads alle 10 ms den Ausgabezustand (`0x249ed0`) über
`scePadVirtualDeviceGetRemoteSetting` aus `libScePad` (NID `LKXfw7VJYqg`, Modul-Offset `0xdd70`). Der Export ist ein einziger ioctl auf
`/dev/hid`:

```text
0x80684830  { u32 handle; u64 Zeiger auf 12 Felder }
```

Der Kernel schreibt jedes Feld über seinen eigenen Zeiger. Der Aufruf gelingt mit jedem Handle aus jedem Prozess; ein
unbekanntes Handle liefert nur Nullen. Das Handle ist der Wert aus dem Hinzufügen des Pads (z. B. `1770253`), keine
kleine Zahl.

## Felder

| Nr. | Größe | Feld | Beleg |
| --- | --- | --- | --- |
| 1 | u32 | unbekannt: kurz 1 nach dem Verbinden, dann 0 | gemessen |
| 2 | 2 Bytes | unbekannt, immer 0 | gemessen |
| 3 | 2 Bytes | **Motoren: groß, klein** | **sicher:** Stärke 0,25 aus `ps5-homebrew-ui` ergibt `0,63` (groß 0, klein 0,25 × 255) |
| 4 | 3 Bytes | Lichtleiste RGB | sicher: Systemblau `0,0,255`, Farbe von Homebrew UI Lab `127,240,216` |
| 5 | 24 Bytes | Trigger-Effekte: Maske, dann zwei Blöcke aus Modus und 10 Bytes | **sicher:** Teardown setzt Modus `0x21`, am DualSense spürbar (`adaptive-triggers.md`) |
| 6 | u32 | Vibrationsmodus | wahrscheinlich: 1, nach `scePadSetVibrationMode(handle, 2)` dann 2 |
| 7–12 | je u32 | unbekannt | gemessen: `1,0,1,1,0` in Titeln, sonst 0 |

Remote Play leitet die Motoren nicht weiter, solange der Modus 1 ist (`0x24a475`). Der Host prüft das nicht und reicht
jede Änderung durch; im Modus 1 wurden bisher keine Motorwerte gesehen.

## Grenze: nur klassisches Rumble

Die Motorfelder füllt nur `scePadSetVibration`, also der klassische Zwei-Motor-Modus. `SceRemotePlay` enthält daneben
einen eigenen Haptik-Weg über AvCapture (Zeichenketten `AvCap audioThread haptic ...`, `hapticsDataReceived`). Titel, die
die Haptik des DualSense als Audio ansteuern, setzen die Motoren daher vermutlich nicht. Das ist mit keinem echten Spiel
gemessen.

## Weg zum Client

Der Host fragt die Motoren alle 10 ms ab (`poll_pad_output` in `src/sunshine/platform_ps5.cpp`) und stellt Änderungen in
Sunshines Feedback-Warteschlange. Sunshine sah dort nur zwischen zwei Steuer-Ereignissen nach, praktisch alle 100 ms
mit dem Ping des Clients, und `moonlight-common-c` behält von wartenden Rumble-Meldungen nur die neueste. Ein kürzerer Puls
kam daher nur als Stopp an. Patch `patches/sunshine/0008-feedback-latency.patch` senkt die Wartezeit der Schleife von
150 auf 5 ms.

Die Lichtleiste läuft denselben Weg: Der Host vergleicht die Farbe bei jeder Abfrage und schickt Änderungen als
`make_rgb_led` an den Client. Ein neues Pad im selben Platz meldet seine Farbe neu (`free_gamepad` setzt sie zurück).

## Messung nachstellen

Ein Titel mit bekanntem Rumble ist „Homebrew UI Lab“: Links am linken Rand wird abgewiesen, das löst einen Puls von 0,25
aus. Das Handle des RemoteEye-Pads steht in `ps5_status`; `pad_output_probe.elf` liest es aus
`/data/prosperosunshine/probe_handle` und schreibt Änderungen zehn Minuten lang ins Kernel-Log. Die Einstellungen der
ProsperoSunshine-App lösen kein Rumble aus.
