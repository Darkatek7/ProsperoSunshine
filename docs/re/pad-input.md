# Eingabe eines virtuellen Pads: Touchpad und Bewegung (FW 13.00)

Stand: 1. November 2026. Statische Analyse der entschlüsselten `libScePad` und `SceRemotePlay` (Werkzeuge in
`tools/re/`, kein Sony-Code im Repository). **Live bestätigt sind Lage und Einheiten von Touch, Drehrate und Beschleunigung
samt Ruhewert (`STATUS.md`, Testschritt 3 unten); Motion kommt also auch ohne weitere Bedingung beim Lesen an.** Alles Übrige
ist weiter nur gelesen. Adressen sind Modul-Offsets. Der Kernel
(`/dev/hid`) liegt nicht vor; was er zwischen Einfügen und Lesen tut, ist nur von außen erschlossen.

## Der Zustandsblock

`scePadVirtualDeviceInsertData` (NID `IWOyO5jKuZg`, `libScePad` `0xda20`) übersetzt das öffentliche `ScePadData` in den
Block und ruft `0x8018482C`. Die Leseseite der Titel (`scePadRead`, `libScePad` `0x1e50`) liest dieselben Offsets aus
ihren 0xE0 großen Kernel-Datensätzen zurück (`0x25f7`–`0x27c8`, `0x28b6`–`0x2930`). Beide Seiten stimmen überein, also
ist die Lage aus dem Code **sicher**:

| Offset | Typ | Feld | Insert liest aus `ScePadData` |
| --- | --- | --- | --- |
| `0x00`–`0x0B` | | Kopf, von Insert immer 0. Die Leseseite nimmt `0x00` als u64-Zeitstempel | – |
| `0x0C` | u32 | Tasten | `+0x00` |
| `0x10` | 4 × u8 | lx ly rx ry | `+0x04` |
| `0x14`, `0x15` | u8 | L2, R2 | `+0x08`, `+0x09` |
| `0x17` | u8 | Nummer des gültigen Touch-Satzes, 1..4; Insert setzt fest 1 | – |
| `0x18` | u32 | Touch-Satz 1: unbekannt, von Insert 0 | – |
| `0x1C` | u8 | Touch-Satz 1: Anzahl Finger, auf 2 begrenzt | `+0x34` |
| `0x20`, `0x28` | u8 | Finger 0, 1: id | `+0x40`, `+0x48` |
| `0x24`, `0x2C` | u16 | Finger 0, 1: x | `+0x3C`, `+0x44` |
| `0x26`, `0x2E` | u16 | Finger 0, 1: y | `+0x3E`, `+0x46` |
| `0x30`–`0x77` | | Touch-Sätze 2 bis 4 (je 0x18 Bytes), von Insert nie geschrieben | – |
| `0x78` | 4 × float | Orientierung x, y, z, w | `+0x0C` |
| `0x88` | 3 × float | **Drehrate** x, y, z | `+0x28` |
| `0x94` | 3 × float | **Beschleunigung** x, y, z | `+0x1C` |
| `0xA0`–`0xA7` | | von Insert 0 | – |

Die Reihenfolge weicht von `ScePadData` ab: Im Block steht die Drehrate vor der Beschleunigung. Der zweite Insert-Export
`WH0f85fU998` (`0x12fc0`, Ziel `0x19cd0`) nimmt eine andere Eingabestruktur ohne Touch, schreibt die Bewegung aber an
dieselben Offsets und bestätigt die Lage ein weiteres Mal.

Sonst ändert Insert nichts: kein Zähler, kein Zeitstempel, kein Flag außer der festen 1 auf `0x17`. Das Einfügen gibt
keinen Finger-Druck weiter. Ein Finger hebt sich, indem er aus der Liste verschwindet: Die Anzahl sinkt und das Feld
wird nicht markiert. Insert kopiert nur `Anzahl` Einträge in einen genullten Block.

Der ruhende Zustand ist **nicht** alles null. Remote Play (`0x40790`) und die Leseseite (Vorgabe `0x1f3c0`, `0x2f1b`,
`0x5d7`) verwenden Orientierung `0,0,0,1` und Beschleunigung `0,1,0`, im Block also 1,0f auf `0x84` und `0x98`. Der Host
schickt dort heute Nullen, auch die Null-Quaternion.

## Wertebereiche und Einheiten

| Größe | Bereich | Beleg |
| --- | --- | --- |
| Touch y | 0..1079 | **aus dem Code:** Leseseite ersetzt 0x436 durch 0x437 (`0x2b32`), meldet y-Auflösung 0x438 (`0x69eb`); Faktoren 942/1080 und 1080/942 (`0x1f508`, `0x1f4b8`) passen sie für alte Titel und DS4 an |
| Touch x | 0..1919 | Annahme: kein Wert im Code; die Auflösung kommt vom Kernel (Info-Block `+0xA0`). DualShock 4 und DualSense haben 1920 |
| Touch id | 7 Bit | Remote Play maskiert `0x7f` (`0x182c91`) |
| Beschleunigung | g | **aus dem Code** für die Skala: Ruhewert y = 1,0. Netzformat ±5 (`0x523c00`) |
| Drehrate | rad/s | Schluss: Netzformat ±30 (`0x523c00`). Chiaki gibt SDLs rad/s unverändert ins selbe Format; 30 Grad/s wären als Bereich sinnlos |
| Orientierung | Einheitsquaternion, w zuletzt | **aus dem Code:** Ruhewert, kleinste-drei-Kodierung im Netzformat (`0x182940`) |

## Lesebedingungen in den Titeln

Touch braucht nur `0x17` in 1..4 (`0x2693`). Bei Bewegung gilt in `scePadRead` (`0x265d`, `0x2470`):

- Bit 3 auf Slot-Feld `+0x23C` gesetzt. Es ist beim Öffnen an (`0xddc`) und folgt `scePadSetMotionSensorState` (`0x4788`).
- Bit 0 der Fähigkeiten im Info-Block `+0x84`. `0xC0184852` liest den Block; der Hinzufüge-Aufruf setzt nur Klasse, Nutzer
  und Subtyp, also füllt der Kernel den Rest. Für Klasse 3, Subtyp 1 ist der Wert **unbekannt**.
- Zweites Ausgabefeld des Lese-ioctl `0xC0484851` (Zeiger auf `+0x28`). Ist es ungleich 0, kopiert `scePadRead` die Floats
  direkt (`0x28b6`). Ist es 0, gelten `0x78`ff. als rohe IMU-Daten und laufen durch Sensorfusion (`0x2f0`, `0x178b0`), die
  nur bei gesetztem Bit 7 von `0x87` arbeitet. Das Vorzeichen von w liegt dort. **Vermutung:** Virtuelle Pads melden
  ungleich 0, denn Insert schreibt Floats. Bis das gemessen ist, w ≥ 0 halten: Dann bleibt schlimmstenfalls der Ruhewert.

Fehlt eine Bedingung, sieht der Titel still den Ruhewert.

## Was Remote Play tut

Remote Play füttert alles. Die Kette für einen Eintrag im Netz bis zum Block:

1. Dekoder im Vtable `0x55cda0`: Analogsatz `0x182940` (Drehrate u16 @1, Beschleunigung u16 @7, Orientierung @0xD,
   Sticks @0x11), Touchsatz `0x182c70` (Pad-Index, gedrückt, id, x und y mit je 12 Bit). Der Bereich steht in `0x186353`:
   -30, 30, 65535/60, -5, 5, 65535/10.
2. Ereignisverteiler `0x248950` (Vtable `0x562ba8`), Typ 5 nach `0x249300`, Typ 6 nach `0x2497a0`, Typ 4 (Tasten) nach
   `0x248e80`. Die Floats gehen unverändert in das `ScePadData` des Pads.
3. Touch (`0x2497a0`): Eine Liste mit höchstens zwei Einträgen pro Pad. Gedrückt aktualisiert die id oder hängt an, ein
   dritter Finger wird verworfen. Nicht gedrückt entfernt die id und zieht den letzten Eintrag nach. Bei Client-Typ 2 wird y
   mit 1080/942 skaliert; **Vermutung:** Das ist ein DualShock 4. Der Block erhält also immer den Bereich 0..1079.
4. Nach jedem Ereignis `0x248e13`: Insert mit dem ganzen Zustand, ohne eigenen Takt. Nur ein Sonderplatz (`+0xb18`, zwei
   Handles) nimmt stattdessen `WH0f85fU998` (`0x248ba9`); **Vermutung:** ein Gerät aus zwei Teilen, für uns nicht nötig.

Alle drei Bewegungsgrößen schickt der Remote-Play-Client fertig; Remote Play selbst rechnet sie nicht aus. Auch die
Orientierung muss also von der Gegenseite kommen.

### Wie Remote Play sein Pad anlegt (Issue 41, 2026-11-08, statisch)

`libScePad` hat zwei gleiche Exporte zum Anlegen, `0MB5x-ieRGI` (`0xd780`) und `N7tpsjWQ87s` (`0xd8a0`). Beide nehmen
`{ u32 kennung; u8 typ @4 }` und den Zeiger für das Handle. Erlaubt sind die Typen 1 bis 12 außer 4 (Maske `0xff7`, sonst
`0x803b0001`). Zwei Tabellen (`0x1fab0`, `0x1fabc`) machen daraus Klasse (Byte `0x00` des Datensatzes) und Subtyp (`0x20`).
Die Kennung kommt unverändert nach `0x1C`, das Feld, das `pad.c` als Nutzer setzt. Dann folgt das ioctl `0xC018482A`, op 3.

| Typ | 1 | 2 | 3 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Klasse, Subtyp | 3, 1 | 2, 4 | 2, 5 | 2, 7 | 2, 8 | 2, 9 | 2, 12 | 2, 13 | 3, 14 | 3, 15 | 3, 16 |

Der Host legt Typ 1 an (Klasse 3, Subtyp 1) und setzt als Kennung den Nutzer oder `0xFFFFFFFF`. Remote Play ruft
`N7tpsjWQ87s` in `0x40600` (Vtable-Eintrag `0x553700`, den Aufrufer nicht verfolgt) mit **Kennung
`0x0B000000 | b0 << 8 | b1`** und Typ `b1`, wobei `b0` und `b1` die ersten beiden Bytes seines Arguments sind (vermutlich Platz
und Gerätetyp des Clients). Es gibt also keinen Nutzer an. **Vermutung:** Das Präfix `0x0B` kennzeichnet ein Pad von Remote
Play, und das System behandelt es anders als einen eingeschalteten Controller, etwa ohne One-Touch Play über HDMI-CEC
(Issue 41: Mit dem Pad des Hosts schaltet der Fernseher auf die Konsole um).

**Live widerlegt (2026-11-08, 13.00, am Fernseher des Besitzers, HDMI Device Link und One-Touch Play an):** Mit
`pad_user_probe` (Kennung `0b000101`) und `pad_type_probe` (Klasse und Subtyp frei) schaltete der Fernseher bei jedem Pad um.
Das Kernel-Log zeigt, warum: Der Login-Manager in SceShellCore prüft jedes neue Gerät mit `[IsRemotePlayDevice]` und schickt
bei `IsRemotePlay:0` über `sceSystemStateMgrSendCecOneTouchPlayCommand` One-Touch Play. Alle Varianten kamen mit
`IsRemotePlay:0` an:

| Angelegt (Klasse, Subtyp, Kennung) | Mbus-Ereignis (type, subType) | Bemerkung |
| --- | --- | --- |
| 3, 1, Nutzer (wie der Host) | 1, 22 | „DUALSENSE“, Profil des Nutzers |
| 3, 1, `0x0B000101` (wie Remote Play) | 1, 22 | `userId=0x0` im Ereignis, ohne Nachfrage dem Profil vorne zugewiesen |
| 3, 14, Nutzer | 8, 22 | die Konsole meldete einen „PS VR2 Sense“-Controller |
| 3, 15, Nutzer | 1, 23 | |
| 3, 16, Nutzer | 1, 24 | |

Da Remote Play denselben Export nimmt, hängt `IsRemotePlay` wohl am Prozess, der das Gerät anlegt, nicht an Typ oder Kennung.
Ein Payload kann das nicht nachstellen. Für Issue 41 bleibt die Einstellung der Konsole: One-Touch Play aus.

## Sunshine-Seite

In `.cache/deps/src/sunshine/src` und `moonlight-common-c`. `docs/input-mapping.md` gibt es noch nicht, nur als Eintrag in
der Roadmap. Der Host setzt das Folgende um (`pad.c`, `src/input/pad_touch.hpp`, `platform_ps5.cpp`), mit Unit-Tests und auf der
Konsole mit dem Testclient geprüft.

- `platf::get_capabilities()` muss `platform_caps::controller_touch` (2) liefern. `rtsp.cpp:900` meldet das als
  `x-ss-general.featureFlags`; ohne das Flag lehnt `LiSendControllerTouchEvent` beim Client ab (`InputStream.c:1531`). `pen_touch` bleibt aus.
- Der Host muss die Sensoren anfordern: `make_motion_event_state(client_index, LI_MOTION_TYPE_ACCEL, 100)` und `..._GYRO`
  in `alloc_gamepad` und erneut in `rebind_gamepad`, wie `virtualhid_input.cpp:731` und `:754`. Die Fähigkeiten stehen in
  `gamepad_arrival_t::capabilities` (`LI_CCAP_ACCEL`, `_GYRO`, `_TOUCHPAD`).
- Moonlight schickt Beschleunigung und Drehrate getrennt, Touch pro Zeiger. Da jedes Insert den ganzen Zustand ersetzt,
  braucht `pad.c` einen vollständigen Schattenblock statt des heutigen `ps_pad_state`, sonst löscht ein Tastenereignis Touch und Bewegung.

| Sunshine | Block |
| --- | --- |
| `gamepad_motion_t` ACCEL in m/s² | `0x94` = Wert / 9,80665; Achsen unverändert |
| `gamepad_motion_t` GYRO in Grad/s | `0x88` = Wert × π / 180; Achsen unverändert |
| keine Orientierung | `0x78`: bleibt `0,0,0,1` oder eigene Fusion im Host |
| `gamepad_touch_t` x, y in 0..1 | x × 1919, y × 1079, gerundet |
| `pointerId` u32 | Tabelle pro Pad, id rollierend mit 7 Bit (die Rollierung ist eine Annahme) |
| `LI_TOUCH_EVENT_DOWN`, `_MOVE` | Eintrag anhängen oder aktualisieren; `0x1C` = Anzahl |
| `_UP`, `_CANCEL` | Eintrag entfernen, letzten nachziehen |
| `_CANCEL_ALL` | `0x1C` = 0, dazu Bewegung auf Ruhewert: Sunshine schickt es am Sitzungsende (`reset_gamepads`), das Pad bleibt, und ein letzter Drehwert würde sonst weiterwirken |
| `_HOVER`, `_HOVER_LEAVE`, `_BUTTON_ONLY` | ignorieren |
| `pressure` | kein Feld |

Moonlight legt die Achsen auf SDLs Konvention fest (`Limelight.h:819`). Dass sie Sonys Achsen entsprechen, ist ein Schluss:
SDL und Sony haben beide +y nach oben (Ruhewert `0,1,0`), und Chiaki reicht die SDL-Werte ohne Tausch oder Vorzeichen weiter.
Für die Orientierung nutzt Chiaki einen Madgwick-Filter (`lib/src/orientation.c`); eine eigene Fassung müsste dessen
Lizenz beachten. Ohne Fusion sehen Titel, die nur die Orientierung lesen, keine Bewegung.

Der Touchpad-Klick bleibt die Taste `0x100000`; Remote Play setzt ihn ebenso über die Tastentabelle (`0x52bf40`).

## Offen

- Ob der Kernel den Kopf `0x00` stempelt. Remote Play lässt ihn 0; Titel, die die Drehrate über den Zeitstempel integrieren,
  hängen davon ab.
- Bedeutung von `0x18` (die Leseseite gibt es ab Titel-SDK 3.5 als `+0x38` weiter), Vorzeichen der Achsen, Bedeutung der
  Client-Typen, welchen Typ `b1` Remote Play für einen DualSense übergibt (oben, „Wie Remote Play sein Pad anlegt“).
- Die drei Zusatzbytes des Analogsatzes (`0x249300`, an `7exHuVMAMQM`) hängen nicht an Touch oder Bewegung; ihr Zweck ist unbekannt.

## Minimaler Live-Test

Vorher `/serverinfo` prüfen, nur bei `SUNSHINE_SERVER_FREE`, und das Pad danach entfernen. Kein Spielstand, keine
Screenshots im Stream.

1. **Info-Block ohne Eingabe:** Eine Sonde nach dem Muster von `pad_output_probe` liest das Handle aus `probe_handle` und ruft
   `0xC0184852 { u8 0; u32 handle @4; u64 info* @0x10 }` mit 0xA8 Bytes Puffer. Ins Log: `+0x10`, `+0x12` (VID, PID),
   `+0x84`, `+0x88`, `+0xA0`, `+0xA2`. Erwartet: Bit 0 von `+0x84` gesetzt, 1920 und 1080, keine DS4-PID `0x5C4` (sonst
   skaliert die Leseseite y mit 1080/942).
2. **Rückweg über `scePadReadState`:** Ein Payload kann `scePadInit` nicht rufen, die App aber schon. Liegt beim Start der App
   die leere Datei `/data/prosperosunshine/pad_log`, schreibt sie zehnmal pro Sekunde die Rohdaten nach `pad_log.txt`;
   `tools/moonlight/pad_log.py` entschlüsselt sie. Danach beide Dateien löschen. Dann über RemoteEye `BLOCK` nacheinander:
   - Finger 0: `0x1C`=1, `0x20`=0x11, `0x24`=960, `0x26`=540. Erwartet: ein Finger, id 0x11, 960/540.
   - Dazu Finger 1: `0x1C`=2, `0x28`=0x12, `0x2C`=100, `0x2E`=1079. Erwartet: zwei Finger, y unverändert.
   - `0x1C`=0. Erwartet: `touchNum` 0.
   - `0x78`=(0, 0, 0, 1), `0x88`=(1, 2, 3), `0x94`=(0,1, 0,9, 0,2). Erwartet: dieselben Werte in `ScePadData`, Drehrate
     an `+0x28`, Beschleunigung an `+0x1C`. Bleibt alles beim Ruhewert, liegt es am Fähigkeitsbit oder am Lese-Ausgabefeld.
   Alle Blöcke mit `0x17`=1 und Sticks 128. `ps5_touch` kennt nur einen Finger, für den zweiten also `BLOCK` nehmen.
3. **Ende zu Ende:** App offen, `STREAM_ARGS="--no-script --touch-motion"` an `run_smoke.sh` (zwei Finger ohne Klick, 3 s
   Bewegung; gesendet und erwartet stehen in `stream_test.c`), oder Moonlight für PC und DualSense, und dasselbe Log prüfen. Die
   Moonlight-Fenster-Regel beachten. Dabei auch prüfen, dass Tasten und Sticks mit dem neuen Ruhewert im Block unverändert
   funktionieren: Bisher blieben Orientierung und Beschleunigung dort null.
