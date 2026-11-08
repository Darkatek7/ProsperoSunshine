# Virtuelle Tastatur und Maus (FW 13.00)

Stand: 3. November 2026. Statische Analyse der entschlüsselten `libSceKeyboard` und `libSceMouse` (Werkzeuge in
`tools/re/`, kein Sony-Code im Repository), dazu Messungen mit `src/probes/kbm_probe.cpp` und dem Host über den Testclient
(`STATUS.md`, „Tastatur und Maus“). Adressen sind Modul-Offsets. Der Kernel (`/dev/hid`) liegt nicht vor.

## Exporte

| Bibliothek | Export | Offset |
| --- | --- | --- |
| `libSceKeyboard` | `sceKeyboardVirtualDeviceAddDevice` | `0x17c0` |
| | `sceKeyboardVirtualDeviceInsertData` | `0x1910` |
| | Gerät entfernen | `0x18b0` |
| | `sceKeyboardInit`, `Open`, `Read` | `0xd0`, `0x1d0`, `0xe30` |
| `libSceMouse` | `sceMouseVirtualDeviceAddDevice` | `0x33a0` |
| | `sceMouseVirtualDeviceInsertData` | `0x34f0` |
| | Gerät entfernen | `0x3490` |
| | `sceMouseInit`, `Open`, `Read` | `0xd0`, `0x490`, `0x1580` |

Wie beim Pad (`pad-input.md`) läuft jeder dieser Aufrufe auf genau ein `ioctl` auf `/dev/hid` hinaus. Der Host baut die
Anfragen selbst (`src/platform/ps5/kbm.c`), die Bibliotheken lädt er nicht.

## Anlegen und Entfernen

Dieselben Aufrufe wie beim Pad: `0xC018482A` mit `{u32 op; u64 desc*; u64 int* handle}` und dem 0xA8 Byte großen
Deskriptor (Klasse auf `0x00`, Benutzer-ID auf `0x1C`, Untertyp auf `0x20`), Entfernen mit `0x80104850` und `op = 5`.

| Gerät | `op` | Klasse | Untertyp |
| --- | --- | --- | --- |
| Tastatur | 1 | 2 | `0xa` |
| Maus | 0 | 2 | `0xb` |
| (Pad zum Vergleich) | 3 | 3 | 1 |

Live: Beide Anlegen-Aufrufe liefern 0 und einen Handle. Das System meldet die Geräte auf seinem Bus als `deviceType=7`
(Tastatur) und `8` (Maus), ordnet sie ohne Rückfrage dem angemeldeten Benutzer zu und zeigt „Keyboard connected“ und
„Mouse connected“, beim Entfernen „… disconnected“.

## Eingabe

**Tastatur:** `0x80104874` mit `{u32 handle; u32 pad; u64 block*}`. Der Block ist 0x20 Bytes groß:

| Offset | Feld |
| --- | --- |
| `0x08` | u8, setzt Insert aus einem öffentlichen Feld (0x20, wenn es kleiner als 4 ist, sonst 0); Bedeutung offen, der Host schreibt 0 |
| `0x09` | Modifier, HID-Bits (`0x01` Strg links … `0x80` GUI rechts) |
| `0x0a` | LEDs, `& 7` |
| `0x0b` | Anzahl gehaltener Tasten, höchstens 16 |
| `0x0c` | verbunden, 1 |
| `0x10`–`0x1f` | gehaltene Tasten als HID-Usage (Keyboard-Page), je ein Byte |

Jeder Block ersetzt den ganzen Zustand, wie ein HID-Report.

**Maus:** `0x80104875` mit `{u32 handle; u32 pad; u64 block*}`. Der Block ist 0x50 Bytes groß:

| Offset | Feld |
| --- | --- |
| `0x10` | 1 |
| `0x14` | u16 0 |
| `0x16`, `0x18` | s16 Bewegung x, y seit dem letzten Block |
| `0x1a` | Tasten: `0x1` primär, `0x2` sekundär, `0x4` Mitte (wie `SceMouseData`); `0x8`, `0x10` für die Seitentasten sind angenommen |
| `0x1b` | Rad, s8 |
| `0x1c` | Neigung (horizontales Rad), s8 |
| `0x24` | u32 4 |

## Was live gemessen ist

- **Erste Eingabe:** Eine Taste, die im selben Moment wie das Anlegen kommt, geht verloren; ab 50 ms danach kam jede an,
  auch bei einer Tastatur nach 12 s Pause. Der Host wartet nach dem Anlegen 150 ms.
- **Kurze Tastendrücke:** Unten und oben 1 ms auseinander sieht das System nicht. Der Host hält Tasten und Maustasten
  mindestens 30 ms, auch beim Tippen von Text.
- **Startmenü:** Pfeiltasten bewegen den Fokus wie das Steuerkreuz.
- **Browser:** Tasten kommen auf einer Webseite erst an, wenn ein Textfeld angeklickt und die Bildschirmtastatur offen
  ist. Dann tippt die Tastatur in das Feld, mit Shift, und die Seite bekommt `keydown` mit `key` und `code`. Den Zeiger
  des Browsers bewegt die Maus nicht; er folgt nur dem Stick.
- **Spiel:** Planet Zoo zeigt einen Mauszeiger, der der Bewegung folgt (im Hauptmenü etwa halb so weit wie die gemeldeten
  Pixel), und ein Linksklick löste dort eine Schaltfläche aus. Die allererste Bewegung nach dem Anschließen kam im Spiel
  nicht an.
- **Mitlesen:** `sceKeyboardInit` aus einer Payload heraus aufgerufen brachte die Payload zum Absturz, bevor sie ein Gerät
  angelegt hatte. Zurückgelesen wird deshalb nicht, der Beweis ist das Verhalten auf dem Bildschirm.

Welche Titel eine Maus lesen, zeigt der Import von `libSceMouse` im entschlüsselten `eboot.bin`. Auf der Testkonsole:
Planet Zoo, der Mario-64-Port, PS5SX2 und Prospero Win.

## Offen

Rad und Neigung, rechte, mittlere und Seitentasten, andere Modifier als Shift, das LED-Feld, das Feld `0x08`, und jede
andere Firmware als 13.00.
