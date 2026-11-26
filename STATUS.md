# Arbeitsstand

Stand: 26. November 2026, Release 0.6.9 (Spiele auf der M.2-SSD stehen in der App-Liste; aus 0.6.8: Verlauf der letzten Streams auf der Webseite, Text im Hintergrund, `reference_frame_invalidation` als Versuch). Hier steht nur, was live geprüft ist, mit dem Messweg. Was ausgeliefert ist:
`CHANGELOG.md`. Plan und Reihenfolge: `ROADMAP.md`. Architektur: `docs/adr/001-sunshine-port.md`.

Die älteren Einträge (bis 4. November 2026) und die ersten Nachweise nach Roadmap stehen unverändert und mit denselben
Überschriften in [`docs/status-archive.md`](docs/status-archive.md). Verweise auf `STATUS.md` mit einer Überschrift, die
hier fehlt (etwa in Code-Kommentaren), meinen diese Datei. „Archiv“ unten heißt ebenfalls diese Datei.

## Kurzfassung

**Der Host streamt.** Moonlight paart sich mit der PS5, startet „PS5 Screen“ oder ein installiertes Spiel und bekommt Bild,
Spielton und Controller. Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei (Archiv, „Nachweise nach
Roadmap“). Jedes Release seit 0.3.2 lief auf der Konsole mit 13.00 je 30 s in H.264 und HEVC 1080p60 und dekodierte
fehlerfrei („Release 0.6.8, CI-Dateien auf der Konsole“ und die Einträge davor).

**Firmware:** 13.00 und 10.20 (PS5 Pro) sind geprüft. Von 9.60, 10.40, 11.40, 13.40 und 13.60 gibt es Berichte von
Nutzern und Mitwirkenden, gemessen ist dort hier nichts („Firmware-Sonde auf 9.60“, „Berichte nach 0.3.1“, „Belegter
Steuerport“; Archiv, „Berichte von Testern zu 0.1.7“).

**Clients:** Moonlight für Windows, Linux, iOS, Android und Android TV, Nintendo Switch, PS Vita und Moonlight Web haben
gestreamt, teils im LAN, teils über das Internet. Viele dieser Läufe sind Berichte des Besitzers oder eines Mitwirkenden
mit Hostlog, gemessen ist meist mit dem eigenen Testclient (`tools/moonlight/`).

**Bild:** H.264 und HEVC, HDR als HEVC Main 10 mit HDR10-Metadaten (seit 0.2.4). Auf 13.00 640x360, 960x540, 1280x720,
1080p, 1440p und 4K mit 30 oder 60 fps, bis 150 Mbit/s. Auf 10.20 bis 1080p; dort übernimmt der Encoder keine Bitrate, der
Host setzt einen Quantisierer nach der verlangten Bitrate (Archiv, „Modi unter 1080p (2026-11-02, 13.00)“, „Modi unter 1080p
auf Firmware 10.20“, „HDR“, „Quantisierer auf 10.x folgt der Bitrate“).

**Ton:** Stereo als Opus. 5.1 und 7.1 hinter dem Schalter „Surround“ („Raumklang folgt einem breiteren Stream“; Archiv,
„Für 0.3.1“).

**Eingabe:** ein virtuelles DualSense je Moonlight-Controller, mit Touch und Bewegung (nur am Testclient gemessen),
klassischem Rumble, Lichtleiste und adaptiven Triggern (Moonlight am PC). Die Haptik von PS5-Spielen gibt der Host mit
`haptic_mode = rumble` als Rumble an das Pad des Hosts, mit dem Testclient und mit Moonlight für Linux an einem virtuellen
Pad gemessen („Haptik als Rumble und Trigger-Knock“, „Verlust ohne Keyframe, Text im Hintergrund und echtes Moonlight“).
Tastatur und Maus gehen auf eine virtuelle Tastatur und Maus der Konsole. Ein Gerät ohne Controller spielt mit Tastatur und
Maus als eigenes Pad, mit Profilen je Spiel: auf 10.20 vom Besitzer in GTA III gespielt, auf 13.00 nur am Testclient, ohne
Spiel und Profile („Tastatur-Pad nach dem Review von PR 20“, „Release 0.3.3 auf der Konsole“; Archiv, „Tastatur und Maus“,
„Tastatur und Maus als Controller“). Für Controller ohne Touchpad sind View und Menu zusammen der Touchpad-Klick („Touchpad
ohne Touchpad und „Restart host“ im Stream“).

**Mehrere Spieler:** Weitere Geräte treten einem laufenden Stream mit eigenem Pad bei. Ein Gerät mit anderem Codec,
Dynamikbereich oder einer Auflösung bis 1080p bekommt einen zweiten Aufnahmekanal (13.00, Testclient). Ein Tester spielte
auf 10.20 mit vier Spielern, auch über das Internet. Nur das Gerät des Hosts hat PS und Create, das Pad eines Gasts erscheint
erst bei seiner ersten Eingabe („Host-Rolle am Gerät“, „Gast-Pad erst bei der ersten Eingabe“; Archiv, „Zweiter Spieler
von einem anderen Gerät“, „Zweiter Aufnahmekanal im Host“).

**Spiele:** Die installierten Titel stehen in Moonlights App-Liste und starten von dort. Mit `close_game_on_quit` schließt
„Beenden“ in Moonlight das Spiel (Archiv, „Spiele in Moonlight“, „Terminate App schließt das Spiel“).

**Bedienung:** Die App auf der Konsole (Status, PIN, Geräte, Einstellungen, Autoload, Diagnose) und seit 0.4.0 eine
Webseite auf Port 47990, die erst mit einem Login angeht („Webseite“, „Webseite nach der Testrunde auf 10.20“). Mit 0.5.0
kamen Konsolenprofile pro Gerät („Profile pro Gerät, PR 33“), versteckte Apps für nicht vertraute Geräte („Apps verstecken,
Issue 36“) und ein Debug-Build in jedem Release („Debug-Build (PR 29) zusammen mit Profilen und Apps auf main“, „Debug-Build
(Issue 28, …)“). Profile und versteckte Apps sind nur mit dem Testclient auf 13.00 gemessen, der Debug-Build auf 13.00 und
10.20.

## Offen und bekannte Grenzen

1. **Menüs je Profil (PR 38):** nach 0.5.0 auf `main` gemergt, in keinem Release. Mit dem Testclient auf 13.00 gemessen
   („Konsolenmenüs, PR 38“), mit echtem Moonlight 6.2.0 unter Linux der Controller-Weg (App-Liste, Ablehnung, PS); Tastatur und
   Moonlight auf anderen Systemen noch nicht.
2. **Echtes Moonlight fehlt** bei den Profilen pro Gerät, den versteckten Apps und bei Touch und Bewegung. Dort lief nur
   der Testclient. Zwei Moonlight zugleich (Beitritt, zweiter Kanal, Übernahme) liefen unter Linux („Verlust ohne Keyframe,
   Text im Hintergrund und echtes Moonlight“).
3. **Andere Firmware:** Nur 13.00 und 10.20 sind geprüft (Checkliste in `docs/firmware-porting.md`). 9.60 hat einen
   Datensatz aus der Sonde eines Mitwirkenden und bleibt ungeprüft; die App sieht dort `/data` nicht und geht über den Host
   („Firmware-Sonde auf 9.60“).
4. **10.x:** Der Encoder hält keine Bitrate, `SetVideoBitrate` beendet den Kanal. H.264 kommt in 34 Slices je 1080p-Bild,
   die Hardware-Decoder unter Windows nehmen höchstens 32 und zeigen kein Bild; HEVC geht („Berichte nach 0.3.1“). Modi über
   1080p bietet der Host dort nur mit dem Experimental-Schalter an.
5. **1440p und 4K im Spiel:** Bei manchen Titeln (Mario 64, Teardown) verweigert das System den großen Modus, der Stream
   läuft dann in 1080p. 120 fps gibt es nicht: libSceVenc hat Presets nur für 29,97 und 59,94 fps, `OpenVideo` lehnt das
   Timing-Feld ab (`docs/re/encoder-options.md`, Abschnitt 4; Archiv, „1440p und 4K, H.264“, „HDR-Metadaten, Slices, 120 fps,
   zweiter Kanal“). Timing 2 öffnet bei 60-Hz-Ausgabe, liefert aber 29,97 fps („Release 0.6.7 auf der Konsole, lokale
   Builds“).
6. **Raumklang:** Welcher der 12 Kanäle welcher Lautsprecher ist, ist ungeprüft. Die Konsole im Test gibt Stereo aus, dann
   tragen nur vorne links und rechts Ton. Dafür braucht es eine Konsole mit 5.1- oder 7.1-Ausgabe („Raumklang auf 13.60“).
7. **Ruhemodus:** Auf dem Startbildschirm ohne Spiel und Stream startet der Host seine Dienste danach neu (zwei Tests mit
   0.1.3). Mit offenem Spiel und laufendem Stream kam er nicht zurück, im ersten Test hing die Konsole. Moonlight kann die
   Konsole nicht wecken, Payloads stehen im Ruhemodus still (Archiv, „Stand 30. Oktober 2026“, „Wecken aus Moonlight“).
   Tests nur nach Rückfrage.
8. **Haptik und Trigger:** Die Haptik des DualSense kommt nicht an. Mit `haptic_mode = rumble` (ab Werk aus) gibt der Host ihren
   Pegel als Rumble an das Pad des Hosts, eine Annäherung („Haptik als Rumble und Trigger-Knock“). Adaptive Trigger gehen nur mit
   Moonlight am PC und bleiben nach dem Stream hart bis zum nächsten Verbinden (`docs/re/adaptive-triggers.md`).
9. **HDR:** Avcap2 liefert keine Metadaten, der Host schickt feste HDR10-Werte. Geprüft an zwei echten Aufbauten: Moonlight
   am HDR-Fernseher und Moonlight unter Windows mit HDR-Bildschirm („Berichte eines Mitwirkenden“; Archiv, „HDR“).
10. **Lange Spielsitzungen:** Ein Lauf über mehrere Stunden in einem Spiel mit ausgewertetem Log fehlt.
11. **Seltene Fehlstarts:** 1 in 86 Starts im Docker-Client, 0 in 300 im Windows-Client; die Ursache ist offen (Archiv,
    „Stand 30. Oktober 2026“).
12. **Webseite:** Hier nicht geprüft sind ein Browser am PC gegen die Konsole, PIN, „End stream“, Umbenennen und Entfernen
    über die Seite, eine Adresse außerhalb des LAN und die Seite nach echtem Ruhemodus. Die Bedienung mit Login prüfte ein
    Mitwirkender auf 10.20 und am iPhone („Release 0.4.0 auf der Konsole“).
13. **Echter DualSense an der Konsole:** Er trennt sich, wenn sich das Pad des Streams anmeldet; die Ursache liegt auf der
    Seite der Konsole (Archiv, „Stand 30. Oktober 2026“, Punkt 2 unter „Noch offen“).
14. **Store:** Die Installation aus dem ProsperoStore klappte laut Nutzer einmal (2026-10-31); spätere Versionen sind aus dem
    Store nicht eigens installiert worden.
15. **IPv6:** Mit `address_family = both` nur im Heimnetz über die link-lokale Adresse der Konsole gemessen. Die Testkonsole
    nimmt keine andere IPv6-Adresse an, auch nicht aus dem ULA-Präfix des Netzes; ein Stream von außen über IPv6 ist
    ungeprüft („Release 0.6.7 auf der Konsole, lokale Builds“). Moonlight für Linux streamt über die link-lokale Adresse,
    wenn sie in seiner Konfiguration steht („Verlust ohne Keyframe, Text im Hintergrund und echtes Moonlight“).
16. **Fehlerkorrektur über das Internet:** `internet_fec_percentage` ist nur mit dem Testclient und künstlichem Verlust im LAN
    gemessen (H.264, 10 Mbit/s). Welchen Verlust echte Wege haben und was der kleinere Bildanteil an Qualität kostet, ist offen.
17. **Verlust ohne Keyframe (`reference_frame_invalidation`, ab Werk aus, seit 0.6.8):** nur HEVC, nur mit dem
    Testclient und Moonlight für Linux mit Software-Decoder gemessen, im LAN. Ob die Hardware-Decoder der Geräte (Windows,
    Android, iOS, Fernseher) das Bild nach der Invalidierung annehmen, ist offen, ebenso 4K und ein Weg mit langer Laufzeit
    (ab etwa 70 ms hin und zurück fehlen mehr als 7 Bilder, dann kommt ein IDR wie bisher).

## Neuere Nachweise

### Spiele auf der M.2-SSD in der App-Liste (2026-11-26, 13.00, nach 0.6.8)

Zwei auf die M.2-SSD installierte Spiele fehlten in Moonlight und in der Apps-Liste der Webseite. Per FTP nachgesehen: Ihr
Ordner mit `app.pkg` liegt unter `/mnt/ext1/user/app/<ID>`, unter `/user/app` gibt es keinen; `param.json` und `icon0.png`
liegen wie bei jedem anderen Titel unter `/user/appmeta/<ID>`. Der Host las nur `/user/app`. Gegenprobe über die `param.json`
aller Titel: 40 Spiele auf dem internen Speicher, 21 davon ausgeblendet, bleiben mit „PS5 Screen“ die 20 Einträge, die
Moonlight zeigte; es fehlten also nur die beiden.

Build `c9b22e17` aus dem Speicher (`host_restart.elf` mit `host_restart.next`, der installierte Host 0.6.8 blieb), während
eines der beiden Spiele lief: Es lief weiter (`close_game_on_quit = 1` greift nur bei Moonlights „Quit“). `/applist` über
HTTPS mit dem gekoppelten Testclient: 22 Einträge, beide Titel dabei; `/appasset` liefert für beide ein PNG mit 512×512; der
Besitzer sieht beide mit Cover in Moonlight für PC. `keyboard_profiles.txt` bekam beim Start die zwei auskommentierten
Zeilen dazu. Nicht geprüft: ein Start von der M.2 aus Moonlight heraus und USB-Erweiterungsspeicher (`/mnt/ext0`, nach dem
Layout der PS4 angenommen, an der Konsole hing keiner).

### Release 0.6.8, CI-Dateien auf der Konsole (2026-11-26, 13.00)

Die Dateien, die CI aus `6f591f74` gebaut hat (`SHA256SUMS` stimmt, das ELF der App gleich dem des Archivs), installiert wie
ein Release; die App zeigt `v01.000.032`, Host-Datei, Kopie im Autoload-Slot und ELF der App sind gleich. Messweg wie im
Eintrag darunter.

- **Streams:** H.264 und HEVC 1080p60 mit 1796 und 1794 Bildern, HEVC 3840x2160 bei 60 fps mit 1778, je 0 fehlend,
  decodiert, kein `PS5 capture lost`, keine `Error`-Zeile.
- **Ab Werk:** keine Zeile zur Referenzbild-Invalidierung, je Stream eine `Stream ended:`-Zeile, die Webseite antwortet über
  HTTPS mit ihrer Anmeldeseite.
- **`reference_frame_invalidation = 1` (HEVC 1080p60, 6 % verworfene Pakete, 30 s):** 19 von 19 Verlusten gingen ab einem
  markierten P-Bild weiter, längste Lücke 68 ms, `rfi_verify.py` ohne Befund; die Verlust-Zeilen des Hosts ergeben die 19 des
  Clients. Ohne die Zeile danach wieder keine Ankündigung.
- **Debug-Datei aus dem Speicher:** `0.6.8-debug commit=6f591f74`, `selfcheck done … problems=0`, H.264 mit 1796 Bildern, 0
  fehlend.

### Release 0.6.8 auf der Konsole, lokale Builds (2026-11-26, 13.00)

Lokale Builds von `74d3742d`, der normale installiert wie ein Release (App `v01.000.032`, Host-Datei, Kopie im Autoload-Slot
und ELF der App gleich), danach ein Kaltstart der Konsole: Der Autoload startete `0.6.8 commit=74d3742d`. Messweg wie in den
Einträgen darunter.

- **Streams:** H.264 und HEVC 1080p60 mit 1795 und 1794 Bildern, HEVC 3840x2160 bei 60 fps mit 1784, je 0 fehlend,
  59,94 fps, decodiert, kein `PS5 capture lost`, keine `Error`-Zeile. Nach dem Kaltstart noch einmal 1796 und 1793, 0 fehlend.
- **Ab Werk:** keine Zeile zur Referenzbild-Invalidierung; ein HEVC-Stream, dessen Client sie könnte (`--rfi`), läuft wie
  jeder andere (1195 Bilder in 20 s, 0 fehlend). `HISTORY` nennt die Streams wie die `Stream ended:`-Zeilen im Log.
- **`reference_frame_invalidation = 1` am installierten Host (HEVC 1080p60, 6 % verworfene Pakete, zweimal 30 s):** 19 von 19
  und 17 von 17 Verlusten gingen ab einem markierten P-Bild weiter, längste Lücke 67 und 68 ms, 4 IDRs je Lauf,
  `rfi_verify.py` ohne Befund. Die Zeilen, die die Verluste zusammenfassen, ergeben zusammen genau die Zahl des Clients,
  auch mit der neuen Zeile am Stream-Ende. H.264 mit dem Schalter: 11 Verluste, 11 IDRs.
- **Text mit 600 Zeichen:** zweimal genau 512 getippt, je eine Info-Zeile dazu.
- **Debug-Build aus dem Speicher:** `0.6.8-debug commit=74d3742d`, `selfcheck done … problems=0`, H.264 und HEVC mit 1796 und
  1793 Bildern, 0 fehlend. Zwei `Error`-Zeilen `RTSP: handle_read_plaintext(): Couldn't read from tcp socket: End of file` am
  Stream-Anfang (ein Client schließt eine RTSP-Verbindung), ohne Folge für den Stream.
- **Nebenbefund:** Nach einem Kaltstart bleibt die Karte „Autoload Done“ des Payload Managers offen, bis man sie schließt;
  ein Stream vom Startbildschirm zeigt dann sie.

### Verlust ohne Keyframe, Text im Hintergrund und echtes Moonlight, aus dem Speicher (2026-11-25, 13.00)

Stand nach 0.6.7, vor dem Release 0.6.8 (Zweig `work/after-0.6.7`, zuletzt `654afd61`), Host aus dem Speicher gestartet,
installiert blieb 0.6.7. Drei Runden am selben Abend; das Mario-64-Demo lief für ein bewegtes Bild (es bewegt sich nur
einige Minuten nach dem Start, danach repariert die Fehlerkorrektur auf dem stehenden Titelbild jeden Verlust, also vor
jeder Messreihe neu gestartet). Verlust: `tools/moonlight/loss_test.sh` verwirft im Container des Clients einen Anteil der
ankommenden Videopakete.

- **Was der Encoder kann (`rfi_probe`, `docs/re/encoder-options.md` Abschnitt 5):** Nach `sceAvcap2SetInvalidFrame` hängt bei
  HEVC schon das nächste Bild am neuesten Bild vor dem Verlust (101→99, 201→197, 301→292), als P-Bild gewöhnlicher Größe.
  Bei H.264 ändert der Aufruf nichts (ein Referenzbild). Mehr als 7 fehlende Bilder am Stück dekodiert kein Decoder mehr
  richtig (4 Bit Bildzähler).
- **Verlust ohne Keyframe im Host (`reference_frame_invalidation = 1`, HEVC 1080p60, 30 s je Lauf, Client mit `--rfi`
  gegen denselben Host ohne, abwechselnd, vier Runden):** Mit `--rfi` gingen 106 von 107 Verlusten ab einem markierten
  P-Bild weiter, davor fehlten 2 Bilder (86-mal) oder 3 (20-mal); der eine andere fiel direkt vor ein IDR, das der Encoder
  ohnehin schickte. Bei 6 % verworfenen Paketen: 4 bis 5 IDRs je Lauf statt 17 bis 21 (die 4 bis 5 kommen auch ohne
  Verlust), längste Lücke 67 bis 69 ms statt 97 bis 104 ms, je Verlust rund 2,2 fehlende Bilder statt 4. Bei 3 %: längste
  Lücke 28 bis 67 ms statt 89 bis 95 ms. `tools/moonlight/rfi_verify.py` fand in allen 51 HEVC-Mitschnitten der Runde kein
  behaltenes Bild ohne seine Referenz. Moonlights Grenze von 120 verworfenen Bildern wurde nie erreicht. Der Host zählt
  die Verluste jetzt (`missing_frames` 2 bis 73, etwa die fehlenden Bildnummern am Client; ohne `--rfi` weiter 0).
- **Ohne die Zeile** kündigt der Host nichts an, der Client verlangt IDRs wie bisher, keine der neuen Logzeilen.
- **H.264 mit dem Schalter:** Jeder Verlust bekommt ein IDR, längste Lücke 83 und 100 ms, nichts hängt.
- **Zwei Geräte auf einem Kanal:** Das Gerät ohne Verlust bekam wegen der Verluste des anderen kein IDR mehr (5 bis 6 je
  Lauf gegen 6 bis 7, wenn beide sauber sind) und keine Lücke; beide Mitschnitte jedes Laufs geprüft. Auch mit 6 % auf
  beiden (14 bis 21 Verluste je Stream) wurde nie ein Stream wegen des anderen angehalten: Der Encoder hat nie den Verlust
  des einen aus einem Bild beantwortet, das dem anderen fehlte.
- **Andere Modi:** 720p60, 1080p30 (längste Lücke 137 ms), HDR (Main 10), ein Stream auf dem zweiten Kanal und 1440p
  (kam bei 120-Hz-Ausgabe als 1080p): Überall las der Host die Parametersätze, und jeder Lauf mit Verlust ging ab P-Bildern
  weiter. 4K ist nicht gemessen.
- **Übernahme und Titelwechsel:** Ein beigetretenes Gerät übernahm den offenen Kanal ohne Neuöffnen und ging danach 15-mal
  ab P-Bildern weiter. Über einen Titelwechsel (drei Neuöffnungen, längste Lücke 4,6 s) ging der Stream ab dem IDR der
  Öffnung weiter, spätere Verluste wieder ab P-Bildern.
- **Echtes Moonlight 6.2.0 (Linux, FFmpegs Software-Decoder, 3 % Verlust, 60 s):** fünfmal `Next post-invalidation frame
  is: … (P-frame)`, das Bild in drei Bildschirmfotos seines Fensters heil, kein Abbruch. Hardware-Decoder sind nicht
  geprüft.
- **Text von der Bildschirmtastatur eines Geräts:** Während ein Gerät 100 Zeichen tippte, kamen die 15 Steuerkreuz-Drücke
  eines zweiten alle 0,5 s an (0.6.7: alle erst nach 6 s auf einmal), und ein Druck kurz vor dem Text blieb 150 ms unten
  statt 6,2 s. Die 100 Zeichen kamen vollständig und in Reihenfolge an, auch wenn erst der Text die Tastatur anlegt (0.6.7
  verlor dort die ersten 8 bis 11). Von 600 Zeichen kamen die ersten 512. Endet der Stream mitten im Text, bleibt keine
  Taste unten.
- **Ein Controller, dessen Profil dem Gerät genommen wird:** verlässt das Profil, der Stream läuft weiter, beim nächsten
  Druck tritt er im anderen Profil bei; der Host meldet das von ihm angemeldete Profil 45 s nach dem letzten Pad wieder ab.
  Ob eine vorher gehaltene Taste hängen blieb, war am Bild nicht zu sehen.
- **Modi eines Steam Deck (Testclient):** 1280x800@60, 1280x800@90 und 1280x720@90 starten in H.264 und HEVC und bekommen
  1280x720@60; darauf weist nur eine Info-Zeile hin.
- **Echtes Moonlight, Release 0.6.7:** Rumble kommt am Controller an (virtuelles Xbox-Pad mit Force Feedback): in GTA III
  3 von 3 Motorwerten, mit `haptic_mode = rumble` in Astro's Playroom 927 von 929 in Reihenfolge, jeweils mal 257, am
  Stream-Ende Null, Stopp und Löschen. Zwei Moonlight zugleich: gleicher Modus tritt bei (`role=joined`), 720p H.264 neben
  1080p HEVC bekommt Kanal 1, beide je rund 60 fps ohne Verlust; die Frage „Who's using this controller?“ für das zweite Pad
  mit User1 beantwortet, danach steuern beide Pads den Startbildschirm; endet das erste, läuft das zweite weiter (Übernahme
  ohne Neuöffnen). IPv6 mit `address_family = both`: Das Pairing gilt weiter und der Stream läuft (60 fps), aber Moonlights
  Kommandozeile nimmt die IPv6-Adresse erst, wenn sie in seiner eigenen Konfiguration steht; mit `[fe80::…%if]` bricht es
  nach 30 s ab, ohne Klammern streamt es über IPv4. Trigger-Effekte und Lichtleiste nicht geprüft (brauchen einen DualSense
  am Client).
- **Debug-Build:** `0.6.7-debug commit=654afd61`, `selfcheck done … problems=0`, H.264 und HEVC mit 1796 und 1794 Bildern
  ohne Lücke, mit Verlust wie der normale Build.

### Stream-Verlauf, Zahleneditor und aufgeteilte Dateien, aus dem Speicher (2026-11-25, 13.00)

Unveröffentlichter Stand nach 0.6.7 (`c167da10`: Verlauf der beendeten Streams, Schritte im Zahleneditor der App,
`platform_ps5.cpp` und `screen.cpp` als reine Verschiebung aufgeteilt), Host aus dem Speicher gestartet, installiert blieb
0.6.7. Messweg wie in den Einträgen darunter; der Text der Verlaufskarte auf der Webseite ist nach dieser Runde noch
geändert worden (sie zählt jetzt die Anforderungen neuer Bilder, siehe unten).

- **Verlauf (`HISTORY`, `stream_history.txt`):** vor dem ersten Stream leer und ohne Datei. Je Stream eine Zeile, die neueste
  zuerst: H.264 1080p60 30 s (`secs=29`, `kbps=4148` wie die Aufnahme, `internet=1`), HEVC mit 1392-Byte-Paketen
  (`internet=0`), `ENDSTREAM` nach 12,5 s (`secs=12`), `QUIT` mitten im Stream (`secs=10`, nach dem Neustart des Hosts samt
  allen früheren Zeilen noch da), zwei Geräte zugleich (das beigetretene mit dem Modus der Aufnahme) und HEVC 3840x2160
  (`mode=3840x2160@60`, `kbps=62988`). `sunshine.log` hat je Stream `Stream ended:` mit denselben Zahlen ohne Gerät.
  `sent_kbps` zählt die Fehlerkorrektur mit (1556 gegen 540 kbit/s Nutzdaten am Client auf dem ruhigen Home-Bildschirm).
  Die Uhr der Konsole ging 68 s vor.
- **Was der Host vom Verlust sieht:** Bei 10 % zufälligem Verlust (H.264 10 Mbit/s, 30 s) fehlten dem Client 29 Bilder in 7
  Lücken, er verlangte 8-mal ein neues Bild; der Host zählte `recoveries=9` (eines verlangt jeder Stream beim Start) und
  `missing_frames=0`. Der Host bietet keine Referenzbild-Invalidierung an, also schickt moonlight-common-c nur die Bitte
  um ein ganzes Bild und nennt keine Bilder. Bei 3 % fehlte auf dem Home-Bildschirm nichts. Gleich mit dem Stand vor dem
  Aufteilen.
- **Webseite ohne Anmeldung:** `/`, `app.js` und `style.css` kommen und enthalten die Karte „Recent streams“,
  `/api/overview` antwortet 401. Die angemeldete Ansicht ist nicht geprüft (das Login des Besitzers blieb unangetastet).
- **Eingabe, Ton, Vibration nach dem Aufteilen:** Steuerkreuz 14 von 14 Drücken im Bild sichtbar (93 ms), der Klick im Ton 9
  von 14 (106 ms), 5800 Audio-Pakete decodiert; Pfeiltasten der Tastatur bewegen den Fokus; mit `haptic_mode = rumble` öffnet
  und schließt der Haptik-Kanal sauber, kein `haptic_blocked`. Mausbewegungen ohne sichtbare Wirkung auf dem
  Home-Bildschirm. Profile und Plätze nicht geprüft (brauchen eine zweite Person).
- **Debug-Build:** `0.6.7-debug commit=c167da10`, `selfcheck done … problems=0`, H.264 und HEVC mit 1795 und 1791 Bildern, 0
  fehlend.
- **App:** die drei Seiten wie zuvor; im Zahleneditor „At home“ mit R1 von 20 auf 40, 60, 80 und dort Schluss, hoch/runter 5,
  links/rechts 1; „Rumble level“ 50 / 10 / 5, „Trigger knock“ mit L1 von 100 auf 75 und 50, „Base port“ mit R1 plus 100.
  Jedes Blatt mit Kreis verlassen, `sunshine.conf` byte-gleich. Die Blätter Devices, Streams und Web page öffnen und
  schließen.
- **Nebenbefund:** Die App installiert beim Start ihren mitgelieferten Host in die Host-Datei und den Autoload-Slot, wenn
  er abweicht. Die Test-App brachte so den Kandidaten für rund 5 Minuten dorthin; die Release-App danach wieder 0.6.7.
- **kstuff:** Die Konsole startete mit einem Test-Build von kstuff („Kstuff Lite 1.15-dr-test2a“) als erstem
  Autoload-Eintrag; alle 8 Einträge luden, der Host war oben, in 49 Minuten Klog kein Absturz.

### Release 0.6.7, CI-Dateien auf der Konsole (2026-11-25, 13.00)

Die Dateien, die CI aus `d6b40488` gebaut hat (`SHA256SUMS` stimmt, das ELF der App gleich dem des Archivs), installiert wie
ein Release; die App zeigt `v01.000.031`, Host-Datei, Kopie im Autoload-Slot und ELF der App sind gleich. Messweg wie im
Eintrag darunter.

- **Streams:** H.264 und HEVC 1080p60 mit 1795 und 1793 Bildern, HEVC 3840x2160 bei 60 fps mit 1784, je 0 fehlend,
  59,94 fps, decodiert, kein `PS5 capture lost`.
- **Ab Werk:** `vibration off at 100%`, kein Haptik-Kanal, jeder Stream meldet `error correction 20% (fec_percentage)`.
- **`address_family = both`:** H.264 je 20 s über IPv4 und über die link-lokale IPv6-Adresse mit 1195 und 1197 Bildern, 0
  fehlend. Ohne die Zeile danach lehnt die Konsole IPv6 wieder ab.
- **Debug-Datei aus dem Speicher:** `0.6.7-debug commit=d6b40488`, `selfcheck done … problems=0`, H.264 mit 1796 Bildern, 0
  fehlend.
- **Nebenbefund:** ShadowMountPlus lehnte das Abmelden der App dreimal mit `runtime_busy` ab; sein Log stand seit den
  Neustarts der Mario-64-Demo in der Runde davor auf „awaiting sandbox removal“. Die App einmal starten und schließen löste
  es, danach liefen Abmelden und Scan. Titelstarts waren nicht betroffen.

### Release 0.6.7 auf der Konsole, lokale Builds (2026-11-25, 13.00)

Zwei Runden mit lokalen Builds, installiert wie ein Release (App bei ShadowMountPlus neu registriert, „Restart host“ in der
App; Host-Datei, Kopie im Autoload-Slot und ELF der App gleich): `20ed60b9` und danach `908b45f0`, der Stand des Releases
bis auf Doku. Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`), je 30 s auf dem Home-Bildschirm, wo nicht
anders genannt.

- **Streams mit `908b45f0`:** normal H.264 und HEVC 1080p60 mit 1795 und 1793 Bildern, HEVC 3840x2160 bei 60 fps mit 1784,
  Debug-Build aus dem Speicher H.264 und HEVC mit 1797 und 1792 Bildern. Je 0 fehlend, 59,94 fps, decodiert;
  `selfcheck done … problems=0`. Die App zeigt `v01.000.031` ohne „debug“.
- **Controller-Einstellungen in der App** (Gruppe „Controller“ rechts neben „Network“): Screenshot ohne Überlappung, das
  Steuerkreuz erreicht die drei Kacheln von „Network“ und „Experimental“ aus und zurück. Vibration auf Rumble, Rumble level
  150, Trigger knock 70 und „Save“ schrieben genau `haptic_mode = rumble`, `haptic_strength = 150` und
  `trigger_knock_strength = 70`; der laufende Host meldete ohne Neustart `vibration rumble at 150%` und `trigger knock 70%`,
  und im Stream danach öffnete der Haptik-Kanal. Quadrat auf jeder Kachel und „Save“: Datei wieder byte-gleich. Die Texte
  bei „Save“ nennen die Ausnahme (`908b45f0`). Die Webseite ist dabei nicht geprüft (auf der Konsole ist ein Login gesetzt).
- **Fehlerkorrektur für Geräte über das Internet** (Gruppe „Error correction“ mit „At home“ und „Internet“): „Internet“
  auf 40 schrieb nur `internet_fec_percentage = 40`. Nach „Restart host“ meldete ein Stream mit 1024-Byte-Paketen (so fragt
  Moonlight über das Internet) `error correction 40% (internet_fec_percentage)`, und der Encoder bekam 5308 statt 7308 kbit/s
  von 10000 verlangten; mit 1392-Byte-Paketen (LAN) fehlt die Zeile. Ohne den Schlüssel: `error correction 20%
  (fec_percentage)`.
- **`address_family = both`:** Auf 0.6.7-beta.3 antworteten 47984, 47989, 47990 und 48010 damit nur noch über IPv6, über IPv4
  kam „refused“. Mit `20ed60b9` antworteten sie auf beiden, aber jeder Stream endete bei ANNOUNCE mit `ENet couldn't open UDP
  port 47999: Invalid argument (errno 22)`. Mit `908b45f0`: H.264 und HEVC über IPv4 mit 1796 und 1793 Bildern, H.264 20 s
  über die link-lokale IPv6-Adresse der Konsole mit 1197 Bildern, je 0 fehlend; die bestehende Kopplung galt über IPv6.
  `STREAMS` nennt das IPv6-Gerät mit `%eth0`, `sessionUrl0` trägt denselben Zusatz. Ohne die Zeile danach wieder nur IPv4.
  Die Konsole hat nur die link-lokale Adresse, obwohl das Netz ein ULA-Präfix verteilt.
- **Timing 2 bei 60-Hz-Ausgabe** (`slice_rate_probe.elf`, Ausgabe `0x138`): öffnet für H.264 und HEVC, 89 bis 90 Bilder in
  3 s, TSC-Abstand 33,37 ms, also 29,97 fps; Timing 1 davor und danach 181 Bilder. Timing 3: `0x81950001`
  (`docs/re/encoder-options.md`, Abschnitt 4).

**Fehlerkorrektur bei Verlust** (`908b45f0`, `loss_matrix.sh`, H.264 10 Mbit/s bei 60 fps, 1024-Byte-Pakete, Mario-64-Demo vor
jedem Durchgang neu gestartet, je 30 s, drei Durchgänge je Anteil im Wechsel, 0 % ein Durchgang). Fehlende Bilder von rund
1800, Median (die drei Werte):

| Verlust | Anteil 10 % | 20 % | 40 % |
| --- | --- | --- | --- |
| zufällig 1 % | 0 (8 / 0 / 0) | 0 (0 / 0 / 0) | 0 (0 / 0 / 0) |
| zufällig 3 % | 36 (32 / 40 / –) | 8 (8 / 8 / 12) | 0 (4 / 0 / 0) |
| zufällig 5 % | 246 (264 / – / 227) | 40 (40 / 40 / 60) | 4 (4 / 4 / 4) |
| zufällig 10 % | 937 (937 / 953 / 904) | 359 (344 / 421 / 359) | 32 (48 / 32 / 28) |
| Bündel 1 % | 106 (106 / 80 / 136) | 76 (66 / 76 / 91) | 68 (77 / 68 / 59) |
| Bündel 3 % | 309 (259 / 320 / 309) | 232 (175 / 232 / 244) | 189 (189 / 211 / 166) |
| Bündel 5 % | 433 (439 / 433 / 431) | 407 (449 / 407 / 372) | 322 (340 / 322 / 307) |
| Bündel 10 % | 847 (821 / 847 / 946) | 688 (688 / 636 / 732) | 572 (604 / 568 / 572) |

Bündel heißt Verlust in Läufen von im Mittel 8 Paketen. Ohne Fehlerkorrektur (0 %) fehlten schon bei 1 % zufälligem Verlust
680 Bilder. Ohne Verlust bekam das Bild 5293, 4795 und 3769 kbit/s bei 10, 20 und 40 % (Encoder-Budget 8308, 7308 und 5308).
Zwei Läufe fehlen, weil ein gekoppeltes Gerät des Besitzers kurz beitrat. Drei frühere Durchgänge liefen auf einem Standbild
der Demo und zählen nicht. Nicht gemessen: echte Wege, andere Bitraten und Codecs, die Bildqualität je Bit.

### Beta 0.6.7-beta.3 auf der Konsole (2026-11-23, 13.00)

Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`), je 30 s auf dem Home-Bildschirm, wo nicht anders genannt.

- **Lokale Builds von `27b572ab` aus dem Speicher**, normal und Debug: H.264 und HEVC 1080p60, mit dem normalen Build dazu HEVC
  3840x2160 bei 60 fps. 1784 bis 1797 Bilder, je 0 fehlend, 59,94 fps, decodiert. Der Debug-Build schrieb je Öffnen ein Paar
  Descriptor-Zeilen (`fallback 0`, Schritte 4004), der normale keine.
- **Rumble und Knock mit dem normalen Build** in Astro's Playroom (Controller-Demo, Trigger gehalten): Jedes Halten von 1,5 s
  blieb ein Stück, 1 und 3 Wechsel im Abstand von höchstens 10 ms je Lauf, kein Aus und wieder An. Knock bei 70 % 5, 4, 3, 3,
  2, 1, 0, nach dem Entfernen des Schlüssels im laufenden Host 7 bis 1. `QUIT` mit offenem Kanal: `haptic Stop` und `Close`
  mit `rc=0`, kein `haptic_blocked`.
- **CI-Dateien installiert** (`SHA256SUMS` geprüft, App bei ShadowMountPlus neu registriert, „Restart host“ in der App):
  `host version=0.6.7-beta.3 commit=27b572ab`, Host-Datei, Kopie im Autoload-Slot und ELF der App gleich (`d672d31b…`), die App
  zeigt `v01.000.030` ohne „debug“. H.264 und HEVC 1080p60 und HEVC 4K60: 1795, 1794 und 1784 Bilder, je 0 fehlend, decodiert.
  Ohne `haptic_mode` öffnet sich kein Haptik-Kanal (`vibration off at 100%`).

### Haptik als Rumble und Trigger-Knock (2026-11-22 und 23, 13.00, Debug-Builds `haptic01` bis `haptic03` aus dem Speicher, nicht veröffentlicht)

Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`) in einer Variante, die R2 und L2 drücken und ein Pad wählen
kann, H.264 1080p60, wo nicht anders genannt. `sunshine.conf` per FTP geändert, auch im laufenden Stream. Gezählt aus den
`rumble pad=…`- und `adaptive triggers`-Zeilen des Clients. Spiele: Teardown und Astro's Playroom (vom Dump des Besitzers
installiert). `haptic03` ist `haptic02` mit dem Pegel, der je Paket fällt. Einzelheiten in `docs/re/haptics.md` und
`docs/re/adaptive-triggers.md`.

- **4K HEVC mit offenem Kanal:** 3840x2160 bei 80 Mbit/s vor einer Rauschseite im Browser (Ausgabe 60 Hz), je 90 s, zweimal mit
  offenem und dreimal mit geschlossenem Kanal: 0 fehlende Bilder, kein `PS5 capture lost`.
- **Nur das Pad des Hosts:** Teardown mit zwei Pads, der Feuerlöscher abwechselnd von beiden bedient: Rumble nur an Pad 0, an Pad 1
  keine Zeile, auch während Teardown Pad 1 folgte. `haptic_strength` 50 und 200 galten im laufenden Stream. Klassische Motoren
  setzt weder Teardown noch Astro (Astro mit `haptic_mode` aus: kein Rumble, keine `motors`-Zeile).
- **Beenden mit offenem Kanal:** `QUIT` mit `rumble` und mit `haptic_mode` aus: Ton, Kanal und Aufnahme schlossen mit `rc=0`,
  `exit_value=0`, kein `haptic_blocked`; der nächste Host streamte normal.
- **Flackern:** Ein schwaches, anhaltendes Signal schaltete den Motor in `haptic02` alle 10 ms zwischen 0 und 16 (Teardown nach dem
  ersten Sprühen). In Astro, je zwei Läufe je Build: gehaltener Trigger 2299 und 2287 Wechsel im Abstand von höchstens 10 ms in
  `haptic02`, 1 und 1 in `haptic03`; Laufen 704 und 783 gegen 0 und 0. Schritte blieben Pulse von 41 ms, lange Klicks 273 bis 290 ms in
  beiden Builds.
- **Trigger-Knock:** Teardown schickt kein `0x26` (nur `0x05` und `0x21`). Astro schickt es auf der Seite „Adaptive Triggers“ der
  Controller-Demo: bei 100 % unverändert (Werte 7 bis 1), bei 70 % 5, 4, 3, 3, 2, 1, 0, bei 0 % mit leerer Zonenmaske. Die neue
  Einstellung galt im Stream nach etwa 1 s.
- **Wächter für `sunshine.conf`** (Schreibzeit und Größe): 0 verpasste in 48 Speicherungen, darunter 8 mit einem Blick des Hosts
  auf die halb geschriebene Datei. Verpasst werden weiter zwei gleich große Speicherungen in derselben Sekunde (3 von 3).
- Alle 35 Streams mit diesen Builds ohne fehlende Bilder, `selfcheck … problems=0` in jedem Build.
- **Nebenbefund:** Solange das virtuelle Pad eines Testwerkzeugs (RemoteEye) an der Konsole hing, fragte sie bei jedem Streamstart
  „Who's using this controller?“ für das Pad des Hosts, und das Spiel bekam bis zur Antwort keine Eingabe. Ohne dieses Pad ordnete
  sie das Pad des Hosts ohne Frage dem Nutzer zu.

### Beta 0.6.7-beta.2 auf der Konsole, CI-Dateien (2026-11-22, 13.00)

Messweg: `PPSA47989.zip` und das Tarball aus der Release (`SHA256SUMS` geprüft), App nach `/data/homebrew/` kopiert und bei
ShadowMountPlus neu registriert, in der App „Restart host“; Testclient unter Linux (`run_smoke.sh`, `--network host`), je 30 s.
- Kernel-Log `host version=0.6.7-beta.2 commit=a1f00d90`. Host-Datei, Kopie im Autoload-Slot und ELF der App haben dieselbe
  SHA256 (`72e3b1ac…`), die App zieht den Slot selbst nach. Die App zeigt `v01.000.029`, ohne „debug“.
- H.264 1080p60 und HEVC 1080p60 mit der Mario-64-Demo vorne (Ausgabe 120 Hz, `0x140`): 1797 und 1795 Bilder, HEVC 3840x2160
  bei 60 fps auf dem Home-Bildschirm (`0x13c`, 62988 kbit/s): 1783 Bilder. Jeweils 0 fehlend, 59,94 fps, decodiert, keine
  Fehler im Log.

### Debug-Zeile zum Frame-Descriptor (2026-11-22, 13.00, Debug-Build `b1346e6d` mit `descriptor_clock.hpp` aus dem Speicher, nicht veröffentlicht)

Messweg: Testclient unter Linux, je 60 s H.264 1080p60 und HEVC 1080p60 auf dem Home-Bildschirm, HEVC 1080p60 mit der Demo vorne
(Ausgabe 120 Hz), 30 s HEVC 3840x2160; die Zeilen `descriptor over 300 frames` des Hosts. Alle Streams 0 fehlende Bilder.
- Der 240-kHz-Takt liegt bei H.264 an `0x08`/`0x10`, bei HEVC an `0x18`/`0x20` und steigt je Bild um genau 4004, auch bei 4K
  und bei 120 Hz Ausgabe. Der TSC-Stempel liegt an `0x48` bzw. `0x58`. Der Descriptor von `ReadVideo2` ist ein Kopf aus zwei
  Wörtern vor dem von `ReadVideo` (`docs/avcap2-descriptor.md`).
- Kein Zeitstempel fiel auf die Lesezeit zurück. Gelesen wurde 9,8 bis 23,4 ms auseinander, die weitergegebenen
  Zeitstempel lagen 16,43 bis 16,95 ms auseinander, im Mittel 16,68 ms (59,94 fps, wie beim Testclient).
- Je Öffnen genau ein Zeilenpaar, kein Absturz, kein Einbruch der Bildrate. Die Wiederholung nach 5 Minuten kam in einem
  Stream über 330 s (2026-11-23, H.264 1080p60, Home-Bildschirm, 0 fehlend): das zweite Paar 305 s nach dem ersten, mit
  Schritten von 4004 wie im ersten.

### Der Ausgabegröße folgen (2026-11-22, 13.00, Debug-Builds `follow01` bis `follow03` aus dem Speicher, nicht veröffentlicht)

Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`), zweite Identität `out/moonlight-client2` für zwei Geräte
auf einem Kanal, Einstellung über `SETFOLLOWSIZE`. Die Mario-64-Demo stellt die Ausgabe auf 120 Hz (`0x140`); „120 Hz
Output“ blieb auf Automatic. Die Bildgrößen je Bild per `ffprobe` aus dem Bitstrom des Clients, dazu `ffmpeg -xerror` über
die ganze Datei. Am Ende jeder Runde wieder der installierte CI-Host 0.6.7-beta.1, alle Dateien byte-gleich.

- **Ein Gerät, das folgt (`follow02`):** HEVC 10 Bit HDR 3840x2160, 120 s, Demo gestartet und geschlossen: 4K → 1080p → 4K,
  886, 2496 und 3076 Bilder, 0 fehlend, alles dekodierbar, `STREAMS` 62988 → 15747 → 62988 kbit/s, nicht beendet. Lücke beim
  Start der Demo 5,8 s (Wiederöffnung 5787 ms). Beim Schließen öffnete der Host erst in 1080p neu (5019 ms), las `0x13c` und
  öffnete gleich danach in 4K (726 ms); der Client sah eine Lücke von 5,75 s. H.264 3840x2160 über 90 s ebenso: 888, 2152 und
  1722 Bilder, 0 fehlend, Lücken 3,5 und 5,5 s.
- **`follow01`** ging nicht zurück: Er versuchte 4K beim ersten Öffnen nach dem Schließen der Demo, der ResourceArbitrator
  lehnte bei `RUNNING` sofort ab (noch im Zustand der 120-Hz-Demo), und der Stream blieb bis zum Ende bei 1080p.
  Deshalb wartet `follow02` auf eine Öffnung, die einen Modus mit 4K gelesen hat.
- **Einstellung aus:** Der Stream endet wie bisher, die Fehlerzeile nennt 120 Hz und die Einstellung. 1080p-Stream mit
  Einstellung an: keine Zeile zur Größe, 1080p durchgehend, 0 fehlend.
- **Zwei Geräte auf einem Kanal (`follow02`):** Folgt der Halter und das andere nicht, läuft der Halter in 1080p weiter und
  wieder in 4K (5274 Bilder, 0 fehlend), das andere endet mit der neuen Zeile „ending the stream of a device that joined
  channel 0“ und bekam nur 4K-Bilder. Folgt nur das beigetretene Gerät, endet der Halter wie bisher; das andere übernimmt
  (Wiederöffnung 183 ms), läuft in 1080p weiter und wieder in 4K (3478 Bilder, 0 fehlend). Mit `follow01` war es umgekehrt:
  Der Halter, der folgte, endete, und das andere wurde über die alte Übernahme (Öffnen wie ein neuer Stream) still auf 1080p
  gestellt; nach dem Code gilt das für jeden Host bis 0.6.7-beta.1.
- **Keines folgt (`follow02`):** Der Halter endet wie bisher; das übernehmende Gerät bekam kein 1080p-Bild. Es endete aber
  nicht mit der Fehlerzeile zur Größe: Der ResourceArbitrator führte 182 nach `STOP` und `unregister` des Hosts (beide `rc=0`)
  weiter als aktiv, die Übernahme scheiterte 20-mal bei `resource list` (21 s stehendes Bild) und endete mit „could not be
  reopened“. Danach scheiterte jeder Stream, auch 1080p H.264 und mit dem installierten Host, bei `RUNNING` mit
  `outcome=timeout`; im Kernel-Log wuchs die Warteschlange des Arbitrators nur noch (`size[11]` bis `size[20]`). Erst ein
  Neustart der Konsole half. Dieselben Schritte des Hosts liefen vorher und nachher ohne Hänger (beide Runden mit zwei Geräten
  oben, die Runde mit `follow01`). Bei der ersten Wiederholung mit Kernel-Log nahm die Ausgabe nach dem Neustart die
  120 Hz der Demo nicht an (`119.88 Hz accepted, but a vblank comes every 16.682 ms; 59.94 Hz restored`), solange der
  Bildschirm an der Konsole aus war; vor dem Neustart bekam die Demo 120 Hz auch bei ausgeschaltetem Bildschirm. Mit
  eingeschaltetem Bildschirm (`2160P_11988`) lief der Fall dreimal ohne Hänger: Der Arbitrator lehnte 4K jedes Mal schon bei
  `RUNNING` ab (`ScanIn Is4KRunnable: 4K NG(4K120)`), Halter und übernehmendes Gerät endeten mit der Fehlerzeile zur Größe
  (das zweite rund 70 ms nach dem ersten), beide nur mit 4K-Bildern, 0 fehlend, und die Warteschlange des Arbitrators lief
  jedes Mal leer. Den Weg aus dem Hänger (`RUNNING` gewährt, `OpenVideo` bei `0x140` abgelehnt, danach 182 weiter gelistet)
  nahm keiner der drei Läufe; was ihn auslöste, ist offen.
- **Nach dem Neustart (`follow02`, ohne 120 Hz):** Übernahme durch ein beigetretenes Gerät bei 1080p und 4K über die offene
  Sitzung („took the open channel 0 over, no reopen“), 2398 Bilder je beigetretenem Stream, 0 fehlend, Größe unverändert.
  GTA III nahm die Aufnahme beim Start und Ende nicht weg, das Wiederöffnen bei 60 Hz lief dort also nicht; beim Start der
  Demo öffnete der Host noch bei `0x138` in 4K neu (1523 ms), bevor sie auf 120 Hz stellte.
- **Echtes Moonlight (`follow02`, Geräte des Besitzers, Hardware-Decoding):** Moonlight für Android auf einem Fire TV (4K60
  HEVC HDR, 117964 kbit/s) und Moonlight für iOS auf einem iPhone (4K verlangt, 3840x2160@60 geliefert, 78988 kbit/s), je mit
  Start und Ende der Demo: Der Host ging bei beiden auf 1080p und nach dem Schließen wieder auf 4K (`back to it`), ohne
  `Error`. Laut Besitzer lief das Bild bei beiden weiter, kam nach dem Schließen scharf zurück, und der Ton lief. Die Clients
  baten bei keinem Wechsel um ein IDR. Ein Stream, der mit der Demo vorne startete, bekam 1080p und blieb nach ihrem Ende bei
  1080p: `follow02` kannte nur den Weg zurück für einen Stream, der vorher größer lief.
- **Start unter 120 Hz (`follow03`, Testclient):** Mit der Demo vorne startete ein 4K-Stream in 1080p, mit der neuen Zeile
  „the console's output takes no 3840x2160 now (120 Hz), starting at 1920x1080 …“; nach dem Schließen der Demo ging er auf
  4K (`back to it`). HEVC 10 Bit HDR über 90 s: 1862 Bilder 1080p, dann 3205 4K, 0 fehlend, eine Lücke von 5,4 s für beide
  Öffnungen, `STREAMS` 15747 → 62988 kbit/s. H.264 über 70 s: 1939 und 1971 Bilder, 0 fehlend, Lücke 4,75 s. Mit der
  Einstellung aus blieb derselbe Stream bei 1080p (5122 Bilder, 0 fehlend). Der Ablauf aus dem 4K-Start (4K → 1080p → 4K)
  lief mit `follow03` unverändert (992, 1735 und 2100 Bilder, 0 fehlend).
- Nicht geprüft: Moonlight für Windows bei einem Größenwechsel, die Übernahme nach „the client left during a reopen“.
- Keine `stuck`-Zeile, kein `OpenVideo rc=0xffffffff`.

### Startverweigerung mit Namen (2026-11-22, 13.00, Debug-Build `launch01` aus dem Speicher, nicht veröffentlicht)

Messweg: `tools/moonlight/pair.py launch` mit der Identität des Testclients, `run_smoke.sh` für den Stream. Mit GTA III
vorne antwortete `/launch` der Mario-64-Demo mit 409 und „Grand Theft Auto III – The Definitive Edition is open on the
console, and starting Mario 64 would close it. Close it first, or pick PS5 Screen to go on with it.“ GTA III blieb offen.
Vom Home-Bildschirm startete dieselbe Anfrage die Demo (200). Mit GTA III vorne lief „PS5 Screen“ (10 s H.264 1080p60, 597
Bilder, 0 fehlend). Nicht geprüft: der Text für einen vor dem Gerät versteckten Titel, ein Spiel hinter der App, die Anzeige
im echten Moonlight.

### Fixes zu Issue 62 auf der Konsole (2026-11-22, 13.00, Builds `i62fix02`, nicht veröffentlicht)

Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`). Erst der Debug-Build aus dem Speicher, dann der normale
Build und die App installiert, am Ende wieder die CI-Dateien von 0.6.7-beta.1. Dazu die Logs eines Streams des Besitzers vom
selben Tag mit 0.6.7-beta.1.

- **Debug-Build:** Selbsttest `problems=0`. H.264 und HEVC 1080p60 mit der Demo je 30 s: 1796 und 1793 Bilder, 0 fehlend,
  Ton vollständig. HEVC 10 Bit HDR 3840x2160 mit GTA III vorne: 0 fehlend, das erste Bild kam aber erst nach 1,34 s (1722
  Bilder in 30 s).
- **Titelwechsel bei 60 Hz:** HDR 3840x2160 über 180 s mit dreimal Start und Ende von GTA III, H.264 2560x1440 über 80 s mit
  zweimal: 10763 und 4791 Bilder, 0 fehlend, Rate unverändert (62988 und 30988 kbit/s). Die Aufnahme blieb bei jedem Wechsel
  offen, das Wiederöffnen und die Schonfrist von 3 s liefen also nicht. Dass eine Ablehnung bei unverändertem Modus innerhalb
  von 3 s vergeht (Issue 62), ist weiter nicht gemessen.
- **Ausgabe auf 120 Hz:** Die Mario-64-Demo stellt die Ausgabe auf 120 Hz (`0x140`). Startete sie während eines 4K-Streams,
  öffnete der Host einmal bei `0x138` neu (nach 1927 ms) und verlor die Aufnahme gleich wieder. Danach lehnte der
  ResourceArbitrator 4K schon bei `RUNNING` ab, nach 1910 ms Warten. Der Stream endete 7,0 s nach dem ersten Verlust mit
  `output refuses resolution 6 now`. Die Fehlerzeile nennt 120 Hz dabei nicht, weil der Host den Modus in dieser Öffnung
  nicht mehr las. Ein neuer 4K-Stream mit der Demo vorne bekam 1080p (Ablehnung bei `RUNNING` ohne Warten), mit 120 Hz in
  der Warnung, 1790 Bilder, 0 fehlend. Im Stream des Besitzers (RetroArch, `0x140`, 0.6.7-beta.1) lehnte dagegen `OpenVideo`
  4K bei der Wiederöffnung ab, fünf Wiederholungen lang `0x81950008`, und der nächste Stream bekam 4K schon bei `RUNNING`
  nicht. Den Fall bei `OpenVideo` beendet `can't hold resolution` jetzt ohne Schonfrist; auf der Konsole lief dieser Zweig
  noch nicht.
- **„120 Hz Output“ auf Off** (Settings > Screen and Video > Video Output, danach zurück auf Automatic): Die Demo läuft dann
  bei `0x138`. Ein 4K-Stream öffnete beim Start der Demo nach 1951 ms in 3840x2160 neu und lief weiter (3452 Bilder, 0
  fehlend). Ein neuer 4K-Stream mit der Demo vorne bekam 4K.
- **Installiert** (normaler Build): H.264 1080p60 30 s, 1795 Bilder, 0 fehlend. Bei HEVC 1080p60 wechselte die Ausgabe mitten
  im Stream, weil die Konsole gerade benutzt wurde: Wiederöffnung bei 1080p über zwei Öffnungen ohne Modus nach 3475 ms, 0
  fehlend.
- **App** (`v01.000.028`): Auf dieser Konsole darf die App `autoload.txt` lesen (`0666`), der Zustand „unbekannt“ kann hier
  also nicht erscheinen. Der Chip zeigt mit laufendem und gestopptem Host „Starts at boot“. Der Start der App gleicht die
  Autoload-Kopie selbst an. Mit absichtlich älterer Kopie zeigt die Diagnose die neue Zeile („Start at boot runs another host
  build“, 14897592 gegen 14898176 Byte). Ein gekoppeltes Gerät heißt „switch“: „Experimental switches“ bleibt jetzt lesbar,
  Adressen, UUIDs und Gerätenamen sind maskiert. „Start host“ in der App startet den Host, danach gleicht die Kopie dem
  installierten Host.
- Keine `stuck`-Zeile, kein `OpenVideo rc=0xffffffff`.

### Beta 0.6.7-beta.1 auf der Konsole (2026-11-19, 13.00)

Messweg: Testclient unter Linux (`run_smoke.sh`, `--network host`, zweite Identität für zwei Geräte), Mario-64-Demo vorne,
wo nicht anders genannt. Ausgabe der Konsole 60 Hz (`0x138`). Die Runden 1 bis 3 liefen mit dem Debug-Build aus dem
Speicher, Runde 4 mit dem installierten normalen Build. Bis Runde 2 war das Stand `b390190a`, ab Runde 3 Stand `a60af88c`
(ein Keyframe beim Start und beim Beitritt). Vergleiche gegen 0.6.6 am selben Abend, mit demselben Client.

- **Modi** (je 15 bis 30 s): Home-Bildschirm H.264 und HDR 3840x2160 bei 40 Mbit/s, HEVC 2560x1440; Demo H.264, HEVC und
  HDR 1080p60, H.264 720p30, HEVC 1080p30. Alle ohne fehlende Bilder, Ton vollständig. H.264 4K über 120 s mit Start und Ende
  von GTA III während des Streams: 7186 Bilder, 0 fehlend, keine verlorene Aufnahme. Alle Öffnungen nach der ersten seit dem
  Hoststart: `grid=aligned`, Bildalter 23,5 bis 23,7 ms (niedrige Stufe).
- **Längster Abstand zwischen zwei Bildern:** In sechs Zeilen über der Grenze der Matrix: 1440p HEVC 57–58 ms, 1080p60
  H.264 43–52 ms, Verlust in Bündeln Median 536 ms (ein Lauf 1000 ms, fünf angeforderte IDRs nacheinander im Bündel
  verloren). Wo verglichen, lag 0.6.6 gleich oder darüber (1440p 66–67 ms, Bündelverlust 596/561 ms). Die langen Abstände
  liegen an IDRs.
- **Zwei Geräte:** Ein beitretendes Gerät beginnt bei `IDR frame #1`, ohne Warten (dreimal; 0.6.6 wartete auf das nächste
  IDR). Zweiter Kanal (H.264 und HEVC gleichzeitig), `ENDSTREAM` eines Geräts und Übernahme des Kanals durch das
  beigetretene Gerät: jeweils 0 fehlende Bilder.
- **Keyframes beim Start** (`a60af88c`, sechs frische Streams): jeder beginnt mit genau einem IDR. Mit `b390190a` kam in
  jedem Stream ein zweites 1 bis 5 Bilder danach. Beim Beitritt bekommt das erste Gerät ein IDR statt drei.
- **Doppelte IDRs unter Verlust:** 0 von 245 IDRs ohne Anfrage seit dem vorigen, 0.6.6 1 von 86.
- **Verlust:** Puffer mit 6x und 3x der Rate bei 3,4 und 20 Mbit/s: 0 fehlend (ein Lauf bei 20 Mbit/s verlor ein Bild, die
  Wiederholung keines). 1 % Zufallsverlust bei 20 Mbit/s: 0 fehlend, längster Abstand 32 ms.
- **Wiederverbinden und Starts:** 10 von 10 Wiederverbindungen, 20 von 20 Starts im Wechsel H.264/HEVC. Nach dem Beenden
  des Clients mitten in der Eingabe war das Pad nach 2,27 s neutral. UDP 1,5 s weg: Stream läuft weiter; 3 s weg: Stream
  endet mit `Ping Timeout`.
- **Dauerlauf** (H.264 1080p60, 20 Mbit/s, 30 Minuten): 107.888 Bilder, 0 fehlend, 359.797 Tonpakete, eine Öffnung ohne
  Verlust. Host-CPU im Mittel 6,31 % (über 7 % nur in den ersten 3,6 Minuten), Speicher 30,5 MB vom ersten bis zum letzten
  Wert. Leerlauf 0,2 % CPU, 122–134 Wakeups/s. Zwei Abstände von 43 und 47 ms in 30 Minuten (kleine P-Bilder, das nächste
  kam gleich danach), während die Last auf dem Testrechner auf 4,6 stieg.
- **Tastendruck bis Bild** (Home-Bildschirm, 30 Drücke): Median 94,2 ms, Ton 10,2 ms nach dem Bild, Drift +0,28 ms/min.
- **Installiert** (Runde 4, normaler Build `a60af88c`): Host-Datei, Autoload-Kopie und die ELF in der App haben dieselbe
  Prüfsumme, die App ist neu registriert. H.264, HEVC und HDR 1080p60 sowie H.264 720p30: 0 fehlend, längster Abstand 28
  bis 43 ms (die über 38 ms an den IDRs, die der Testclient anfordert). Mit Log-Level `debug`: 5800 Tonpakete in 30 s, keine
  Debug-Zeile im Kernel-Log. 5 von 5 Wiederverbindungen, Beitritt wie oben.
- **App** (`v01.000.028`, ohne „debug“): „Ready to stream“ ohne Hinweis. Speichern schreibt nur die geänderte Einstellung, eine
  von außen geänderte (`close_game_on_quit`) bleibt beim nächsten Speichern erhalten. „Restart host“ meldet den Neustart,
  `host stopped` kommt vor der neuen Versionszeile, der Stream danach läuft. Diagnose: „last open ok“, Abschnitt aus
  `sunshine.log` mit Zeilen, Adressen maskiert.
- **Release-Dateien der CI** (Tag `v0.6.7-beta.1`, `656742e3`, mit `SHA256SUMS` geprüft): Debug aus dem Speicher mit
  Selbsttest `problems=0` und H.264 30 s (1795 Bilder, 0 fehlend); dann normal installiert (Host-Datei, Autoload-Kopie und
  die ELF in der App mit derselben Prüfsumme, App-Ordner gleich dem Zip, mit den Lizenztexten). H.264 und HEVC 1080p60 je
  30 s und 10 s mit der App vorne: 0 fehlend, Ton vollständig. Die Ausgabe stand dabei auf 120 Hz (`0x140`), also
  `grid=not 60 Hz`, ohne Wartezeit.
- Nicht ausgelöst: eine verlorene Aufnahme beim Start oder Ende eines Titels (in dieser Nacht blieb die Aufnahme bei jedem
  Wechsel offen). Der Fall „Client geht während des Wiederöffnens“ ist deshalb nicht live geprüft.
- Keine `stuck`-Zeile, kein `OpenVideo rc=0xffffffff` in allen vier Runden.

### Start der Aufnahme gegen den Bildwechsel (2026-11-18 und 19, 13.00, Debug-Builds aus dem Speicher)

Messweg: Testhosts aus dem Speicher, Testclient unter Linux (`--network host`), Mario-64-Demo vorne, Ausgabe der Konsole in
allen Öffnungen 60 Hz (`video out mode 0x138`). Gemessen wurde das Alter jedes Bildes beim Lesen: Zeitstempel im
Avcap2-Deskriptor (0x48 bei H.264, 0x58 bei HEVC, TSC) bis zum Lesen, im Debug-Log als `Frame processing latency` und beim
Client als `hostLatency` der IDR-Bilder.
- **Ohne Abstimmung** (Testbuild mit zufälliger Wartezeit 0–50 ms vor `sceAvcap2Start`, Bildzeilen als CSV, 21 Öffnungen):
  Das Alter steht pro Öffnung fest (Streuung p5–p95 ±0,8 ms, Drift höchstens 0,32 ms über einen Stream) und liegt bei der
  Demo auf zwei Stufen: 60 fps 23,45–23,62 ms oder 40,07–40,29 ms, 30 fps 40,31 oder 56,92 ms; Home-Bildschirm
  18,04–18,09 ms. Welche Stufe, entscheidet allein der Zeitpunkt, zu dem `start()` zurückkommt: weniger als etwa 3,6–4,2 ms
  vor dem nächsten Bildwechsel gibt die hohe Stufe (6 von 17 Öffnungen mit der Demo, H.264 und HEVC, 60 und 30 fps,
  Trennung ohne Ausnahme). Die Bildwechsel sind die Zeitstempel der Bilder: Abstand 16,683585 ms, Phase über 16 Minuten und
  alle Öffnungen auf 0,04 ms gleich. Der Host hält nichts zurück (Lesepausen unter 2 ms in 20 von 21 Öffnungen nie; das
  Konsolenwort 0x50 bei H.264 bzw. 0x60 bei HEVC ist das Alter minus 0,55–0,64 ms).
- **Mit Abstimmung** (`start_phase`, Stand `abf3c97` als Debug-Build `ec4b9b7p`, 22 Öffnungen): 0 in der hohen Stufe. Nach
  einer Wartezeit kam `start()` 9,97–10,03 ms vor dem Bildwechsel zurück (Hostschätzung, `capture start phase` in
  `host_diag.log`); Stufen 23,57–23,68 ms bei 60 fps und 40,2 ms bei 30 fps, Home-Bildschirm 17,5–18,4 ms, alles ohne
  fehlende Bilder. Zweiter Kanal (H.264 und HEVC gleichzeitig): `capture2 start phase … grid=aligned`, beide niedrige Stufe.
  Mit `start_phase = 0`: `grid=off`, keine Wartezeit. Öffnen dauerte 65–126 ms (vorher 73–132 ms).
- Nicht geprüft: 120-Hz-Ausgabe (dort lagen frühere Stufen 8,34 ms auseinander, die Regel ist unbekannt; der Host wartet dort
  nicht), 10.20, der erste Stream nach einem Hoststart (noch kein Takt bekannt, also weiter etwa ein Drittel in der hohen
  Stufe), ob die eingesparten 16,7 ms beim Tastendruck bis zum Bild ankommen (Messung mit dem Kontrollzentrum zu ungenau).

### Release 0.6.6 auf der Konsole (2026-11-13, 13.00)

Testclient (`run_smoke.sh`, Linux, `--network host`), Mario-64-Demo vorne, je 30 s 1080p60. Dazu lief bei jedem H.264-Lauf
`icmp_watch.py` mit, das jedes ICMP „port unreachable“ der Konsole an den Rechner zählt. Erst der lokale Build (`f922cd8f`),
dann die Release-Dateien der CI aus dem Tag (`6da55c0c`, mit SHA256SUMS geprüft):

| Variante | H.264: Bilder, fehlend, Tonpakete | HEVC: Bilder, fehlend, Tonpakete | ICMP (H.264) |
| --- | --- | --- | --- |
| Lokal, Debug aus dem Speicher (Selbsttest `problems=0`) | 1794, 0, 5800 | – | keins |
| Lokal, normal installiert | 1794, 0, 5805 | 1794, 0, 5800 | keins |
| CI, Debug aus dem Speicher (Selbsttest `problems=0`) | 1794, 0, 5800 | – | keins |
| CI, normal installiert | 1793, 0, 5800 | 1795, 0, 5805 | keins |

Alle fehlerfrei dekodiert, Ton in jedem Lauf. Die lokal gebaute App zeigte `v01.000.027 debug`: Ein normaler App-Build nach
einem Debug-Build hatte die Debug-Objekte behalten (behoben in `app/build.sh`, vor dem Release-Commit). Die App der CI zeigt
`v01.000.027` ohne „debug“ und schließt über `app.quit`; die Webseite antwortet. Installiert sind die CI-Dateien: Host-Datei,
Autoload-Kopie und die ELF in der App haben dieselbe Prüfsumme, `host version=0.6.6 commit=6da55c0c`.

Nicht gemessen: der echte Pfad über das Internet und ps5-tailscale. Den hat der Melder von Issue 37 mit demselben Code
(Test-Build `test-0.6.5-i37-3`) über 5G und Tailscale geprüft: Ton da, Bild flüssig.

### Stream-Ports ab RTSP SETUP, Issue 37 (2026-11-12, 13.00, Patch 0035, Debug-Build aus dem Speicher)

Moonlight pingt den Audio-Port (UDP 48000) ab dem RTSP SETUP, der Host öffnete ihn erst bei ANNOUNCE. Gemessen mit
`icmp_watch.py` neben `run_smoke.sh` (Linux, `--network host`, Mario-64-Demo):

- Vorher (installiertes 0.6.5, H.264 1080p60, 3 Läufe à 10 s): in jedem Lauf genau ein „port unreachable“ für UDP 48000,
  2,4 bis 2,6 s nach dem Start der Messung.
- Mit Patch 0035 (`c719170a` plus 0035): in 9 Streams keins, darunter dreimal H.264, einmal HEVC 30 s und zwei Paare direkt
  hintereinander. 0 fehlende Frames, Ton in jedem Stream, `Stream ports open from SETUP` einmal je Stream im Log.
- 5 s nach dem Streamende antworten 48000, 47998 und 47999 wieder mit ECONNREFUSED, der Halt am Port bleibt also nicht
  hängen.

Im Log nur die bekannte RTSP-Zeile `End of file`, die auch 0.6.0 schreibt.

### Release 0.6.5 auf der Konsole (2026-11-11, 13.00)

Testclient (`run_smoke.sh`, Linux, `--network host`), Mario-64-Demo vorne, je 30 s 1080p60. Erst der lokale Build, dann
die Release-Dateien der CI aus dem Tag (beide `bcb3defd`):

| Variante | H.264: Bilder, fehlend | HEVC: Bilder, fehlend |
| --- | --- | --- |
| Lokal, Debug aus dem Speicher (Selbsttest `problems=0`) | 1795, 0 | 1796, 0 |
| Lokal, normal installiert | 1797, 0 | 1796, 0 |
| CI, Debug aus dem Speicher (Selbsttest `problems=0`) | 1794, 0 | – |
| CI, normal installiert | 1794, 0 | 1794, 0 |

Alle fehlerfrei dekodiert, mit Ton. Mit dem lokalen Build lief ein frischer Start der Demo über `/launch` vom
Home-Bildschirm, die App öffnete (`v01.000.026`), die Webseite antwortete, und 10 % Verlust in Bursts bei 3,4 Mbit/s und
30 fps ergab als längste Lücke 501 und 534 ms. Danach installiert: die CI-Dateien, `host version=0.6.5 commit=bcb3defd`.

### Stand von `work/integration` auf der Konsole (2026-11-11, 13.00, lokal, nicht veröffentlicht)

Gemessen von einem Linux-Rechner im LAN mit dem Testclient (`run_smoke.sh`, `loss_matrix.sh` im SDK-Container, 30 s je
Lauf, Mario-64-Demo vorne), in sechs Runden mit den Builds 7694c0c7, f497e759 und 8e3611c7; Vergleichs-Hosts (0.6.0 und
Varianten) liefen aus dem Speicher über den ELF-Loader, ohne die installierten Dateien zu ändern.

**Issue 37, Stream über das Internet**

- Der Fall aus dem Log des Melders, nachgebaut: 3,4 Mbit/s bei 30 fps, 1024-Byte-Pakete, davor ein Engpass mit 20 Mbit/s
  und 39 Paketen Puffer (`buffer 0 3400 30`). 0.6.0: 0 Frames, 126 IDR-Anfragen, 11,6 Mbit/s gesendet. 7694c0c7: 897
  Frames, 0 fehlend, 1 Anfrage, 2,7 Mbit/s. Mit f497e759 auch bei halb so schnellem Abfluss (`BUFFER_KBIT=3x`) alle
  Frames und 1 Anfrage; die Demo sendete dabei nur etwa 1 Mbit/s, das ist ein leichterer Test.
- LAN-Zeilen (`PACKET_SIZE=1392`, 10/20/50 Mbit/s, 0 und 1 % Verlust): wie 0.6.0, 0 fehlend, längste Lücke 23 bis 38 ms.
- 10 % Verlust in Bursts von 8 Paketen (`KINDS=burst LOSSES=10`, 3,4 Mbit/s, 30 fps), längste Lücke je Lauf, Median aus
  sechs Läufen, die Hosts reihum:

  | Host | Median | Spanne |
  |---|---|---|
  | 0.6.0 | 619 ms | 567 bis 699 |
  | f497e759 (Hold-off bis 500 ms, fünf Stufen) | 1025 ms | 636 bis 1770 |
  | ohne Hold-off | 561 und 558 ms (zwei Runden) | 401 bis 793 |
  | ohne Hold-off, `blind_hold` 0 | 700 ms | bis 863 |
  | eine Hold-off-Stufe ohne Verdopplung | 1113 ms | 868 bis 1186 |

  Mit Hold-off bis 2 s (7694c0c7) stand das Bild in einzelnen Läufen bis 4,5 s. Der Hold-off ist deshalb seit 8e3611c
  entfernt; das Pacing kostet nicht messbar (f497e759 mit 1392-Byte-Paketen, also ohne Pacing: 800 ms).
- Engpass unter der Bitrate (2500 kbit/s für 3400): kein Host bringt ein Bild durch. Gesendet: 0.6.0 5,8 bis 6,4 Mbit/s,
  ohne Hold-off 4,8 bis 4,9, mit Hold-off 3,6 bis 4,1.
- Ton: 20 Sitzungen zu 8 s und 15 schwierige Fälle (direkt nacheinander, zwei Geräte mit Übergabe, abgebrochener Client)
  alle mit Ton; die Übergabe kostet 5 Pakete. Der vom Melder beschriebene fehlende Ton ist im LAN nicht nachgestellt.

**Zwei Geräte, Last, Logs**

- Zwei Geräte auf einem Kanal: 10 + 10 Zyklen (zweites Gerät endet zuerst, Halter endet zuerst) ohne Absturz. Ein Gerät
  mit 10 % Verlust kostet das saubere keine Frames (0 fehlend, längste Lücke 55 bis 59 ms).
- `Host load:` (Debug-Variante, alle 5 s, ganzer Prozess; den Thread-sysctl `kern.proc.pid_td` lehnt 13.00 ab): Leerlauf
  0,2 % CPU und etwa 125 Wakeups/s; ein Stream 1080p60 H.264 oder HEVC 6,1 bis 6,3 % und etwa 2430 Wakeups/s; 720p30
  5,0 %; RSS 19 bis 30 MB.
- Maskierung im Debug-Log: zwei Kennungen (`<id-1>`, `<id-2>`) nach etwa 40 Streams von zwei Geräten; vorher war die
  Tabelle nach 25 Minuten bei `<id-254>` voll.
- Abgeschnittene chunked-Anfrage und Anfrage mit Body an 47989, 47984 und 47990: ohne Antwort geschlossen, der Host
  antwortet weiter. Gerätename über 1 MB: Verbindung geschlossen; 300 Byte werden 128 Zeichen; dritte offene Kopplung
  von einer Adresse: 503. Die chunked-Anfrage mit Chunk-Größe `ffffffffffffffff` per TLS an 47990 beendet die Webseite
  nicht (200 direkt danach); ob der alte Build über TLS daran gescheitert wäre, ist nicht gemessen.
- Moonlight-qt 6.2.0 (headless) 30 s H.264 1080p60: 59,98 fps empfangen und dekodiert; mit `lan_encryption_mode = 2`
  läuft der Stream.
- App-Einstellungsseite (normal): Layout, Steuerkreuz-Wege, Schalter, Fehlerkorrektur-Blatt (0 bis 80, schreibt
  `fec_percentage`) wie vorgesehen.

**PS-Taste und Pull Requests 54 und 55 (Test-Host aus `main` + beiden PRs, Debug)**

- PS halten auf 13.00 (virtueller Controller, je zwei Durchgänge): 300 und 350 ms öffnen das Kontrollzentrum, 400 ms tut
  nichts, ab 450 ms geht es zum Home-Bildschirm. Das sind die Schwellen, die der Autor von PR 55 auf 10.20 gemessen hat.
- PR 55: Windows+Esc drückt PS so lange wie Esc (51 ms, Kontrollzentrum), Windows+Shift+Esc hält PS 649 ms
  (Home-Bildschirm); Esc, Windows und Shift+Esc allein bleiben Tasten. Der Hinweis für Moonlight auf iPhone und iPad ist
  nicht geprüft (der Testclient sendet dessen User-Agent nicht).
- PR 54 (Gerät mit einem Profil, niemand angemeldet): alle Zeilen des Autors in gleicher Reihenfolge, `Started` nach
  0,57 s ohne `8094000A`, X 1,02 s nach dem Pad; ohne Quit wartet das Profil auf sein Spiel, nach Quit Abmeldung 45,2 s
  nach dem Pad. `ps_title_open` ist auf 13.00 erlaubt (`kern.proc.proc`, 0,74 bis 1,00 ms je Aufruf bei 88 Prozessen).
  Die Begrüßung erscheint auch beim Wiederverbinden über dem laufenden Spiel.
- Aufnahme-Start hinter dem Willkommensbildschirm: 2 von 3 solchen Starts endeten mit `PS5 capture failed to start`
  (die Ausgabe hat 1,1 bis 1,2 s keinen Modus, das Öffnen gab nach 1 s auf). Mit a8b7950 auf demselben Test-Host: 6 von 6
  Starts liefen, längstes Öffnen 1,37 s, erstes Bild nach 1,3 bis 1,5 s.

**Der letzte Build 8e3611c7 (ohne Hold-off, mit dem Aufnahme-Fix), beide Varianten**

- Smoke-Test H.264 und HEVC 1080p60 je 30 s: 0 fehlende Frames in Debug (aus dem Speicher) und normal (installiert).
- 10 % Verlust in Bursts, längste Lücke je Lauf: Debug Median 470 ms (4 Läufe, 406 bis 531), normal Median 470 ms
  (6 Läufe, 377 bis 642). Das liegt auf dem Niveau des Hosts ohne Hold-off und unter 0.6.0 (619 ms).
- Issue 37 bleibt behoben: `buffer 0 3400 30` 896 Frames, 0 fehlend, 1 Anfrage, `buffer 0 20000 60` 1795 / 0 / 1, auch
  mit `BUFFER_KBIT=3x`; der Host sendete dabei 2,7 bis 11,2 Mbit/s. LAN-Zeile: 0 fehlend, längste Lücke 21 bis 22 ms.
- Zwei Starts der Demo über `/launch` vom Home-Bildschirm liefen; beim zweiten ging die Ausgabe von 0x13c über 0 auf
  0x144, und die Aufnahme wartete darauf.
- Die normale Variante ist installiert (Host-Datei, Autoload-Slot, App), `host version=0.6.0 commit=8e3611c7`.

Nicht gemessen: der echte Pfad des Melders von Issue 37 (ein Test-Build für ihn steht aus), der Hinweis von PR 55 für
iPhone und iPad, PR 54 mit zwei angemeldeten Profilen, die Webseite hinter ihrem Login, UPnP.

### Profil nach 45 s zurück, Issue 52 (2026-11-10, 13.00 und 10.20, in `main`, nicht veröffentlicht)

Testgerät mit einem Profil (ein zweites Konsolenprofil, vorher abgemeldet), `--no-script`, Home-Bildschirm:

- PS 1,5 s gehalten: Der Host meldete das Profil an und setzte das Pad hinein, das Profil kam nach vorne (beim ersten
  von zwei Versuchen nicht, dann wurde es nach 45 s abgemeldet). 45 s nach dem Streamende: „stays signed in, it is in
  front on the console; its device may join it again“.
- 55 s nach dem Streamende neu verbunden, ein Steuerkreuz-Druck: „attached in its device's profile“ ohne Hinweis. Mit 0.6.0
  blieb das Pad hier nur Zuschauer (Issue 52, auf 10.20 gemessen).
- Danach gilt wieder die normale Regel: das Profil des Besitzers nach vorne geholt, 45 s nach dem Ende abgemeldet (`rc 0`).
- Ein kurzer PS-Druck (Kontrollzentrum) brachte das Profil auf 13.00 nicht nach vorne; es wurde nach 45 s abgemeldet.

Unit-Tests (`profile_rules_test`) und Host-Build ohne Warnung. Danach wieder 0.6.0 über `host_restart.elf`.

Auf 10.20 vom Melder bestätigt (PS5 Pro, Test-Build `test-0.6.0-i52-1`, Host `f463b8cb`, Gerät mit einem Profil, anfangs
niemand angemeldet):

- 55 s nach dem Streamende neu verbunden: nach 45 s „stays signed in, it is in front on the console“, das Pad saß sofort
  wieder im Profil („bound after 0 ms“).
- Der echte Fall: GTA III auf dem Profil gestartet, Stream ohne Quit beendet, 3 Minuten später neu verbunden. Das Spiel war
  noch offen und das Pad saß sofort im Profil.
- Nach Quit blieb das Profil wieder angemeldet. Im Konsolenmenü abgemeldet: „a profile the host kept signed in was signed
  out on the console“.
- Die Zeile „signed in on the console by someone else“ kam nirgends vor.

### Release 0.6.0 auf der Konsole (2026-11-09, 13.00)

App nach `/data/homebrew/PPSA47989` kopiert (`contentVersion` `01.000.025`) und neu registriert, sie öffnet mit „Ready to
stream“. Host-ELF nach `/data/prosperosunshine/` und in den Autoload-Slot, mit `host_restart.elf` neu gestartet: `host 0.6.0
(commit db0ac58b)`, Steuerport nur lokal. Testclient, Home-Bildschirm, je 30 s 1080p60:

| Variante | H.264: Bilder, längste Lücke | HEVC: Bilder, längste Lücke |
| --- | --- | --- |
| Normal | 1791, 19 ms | 1792, 19 ms |
| Debug (`0.6.0-debug`, Selbsttest `problems=0`) | 1795, 20 ms | 1790, 38 ms |

Alle fehlerfrei dekodiert, ohne Tonfehler. Danach wieder die normale Variante über `host_restart.elf`. Beide Testsuiten,
Host normal und debug, App normal und debug ohne Warnung; 25 Prüfsummen im Archiv stimmen, keine privaten Strings im ELF.

### Konsolenmenüs mit echtem Moonlight, PR 38 (2026-11-09, 13.00, Stand `db5b258`)

Moonlight 6.2.0 für Linux (offizielles AppImage, SHA256 wie auf GitHub) headless in Docker unter Xvfb, Software-Dekodierung,
ein virtueller Xbox-360-Controller über uinput, den Moonlight als Gamepad an den Host gibt (`pads=1`). Per PIN gekoppelt
(`devicename=roth`), das Gerät auf ein Profil gesetzt (`SETPROFILES`):
- Menüs nur für ein anderes Profil: Die App-Liste des Geräts hat kein „PS5 Screen“. Moonlight startete es aus seiner
  zwischengespeicherten Liste trotzdem; der Host lehnte mit 403 „This app is hidden on this device“ ab (Log: `PS5 Screen
  refused: none of the device's profiles reaches the console's menus`), Moonlight zeigte den Fehler, kein Stream.
- Dasselbe Gerät startet Mario 64 aus Moonlight: Der Controller sitzt im Profil des Geräts, PS zweimal gedrückt, beide Male
  `PS or Create ignored, its profile doesn't reach the console's menus`; Mario 64 blieb vorne (Bild nach dem Stream).
- Menüs für das Profil des Geräts: „PS5 Screen“ steht in der Liste und startet (200). PS auf dem Home-Bildschirm öffnete das
  Kontrollzentrum. In Mario 64 brachte langes PS den Home-Bildschirm, das Spiel lief dahinter weiter.
- Danach `SETMENUS unset`, Profil des Geräts zurückgesetzt, Release-Host 0.5.0 wieder gestartet.
Nicht geprüft: Moonlight auf Windows, Android oder iOS, Tastatur (F1/F2, Druck, Pause), mehrere Geräte zugleich.

### HEVC 8 bit bei hoher Rate mit Bewegung, Issue 50 (2026-11-09, 13.00, Host 0.5.0, Testclient)

Gegenprobe zu Issue 50 (auf 10.20 verliert HEVC 8 bit auf der QP-Stufe 21/25 mit Bewegung die Capture). Bewegte Seite in
der Browser-Karte (Rauschen auf der halben Fläche, bewegte Formen, rund 25 Bilder/s), Testclient per `--network host`,
je 15 s, Rate laut Host-Log: HEVC 1080p60 bei 22988 kbit/s zweimal (geliefert 19,1 und 19,3 Mbit/s) und bei 62988
(49,5); HEVC HDR 1080p 22988 (15,6); H.264 1080p 22988 (19,4); HEVC 3840x2160 bei 62988 (51,9); HEVC 2560x1440 bei
30988 (25,4). Überall 59,94–59,96 fps, längste Lücke 21–26 ms, ffmpeg fehlerfrei, kein `capture lost` und kein Reopen
im Host-Log. Auf 13.00 setzt der Host keinen QP (Ratenfelder), der Fehler hängt also am QP-Pfad von 10.x oder an 10.x
selbst. Nicht geprüft: echtes Moonlight, ein Spiel mit 60 fps Bewegung.

Zweite Gegenprobe am selben Tag mit der Seite des Melders (480x270 Rauschen auf die Karte gestreckt, zwölf bewegte Kreise),
mit der 10.20 auch bei QP 24/28 in 1080p abbrach: HEVC 8 bit 1080p60 bei 14988, 22988 (zweimal) und 62988 kbit/s, 2560x1440
bei 30988, 3840x2160 bei 62988, je 12 s. Überall 59,96–59,99 fps, längste Lücke 19–25 ms, ffmpeg fehlerfrei, kein
`capture lost` im Host-Log. Branch `fix-50-hevc8-qp` (db5b258, Obergrenze 24/28 und eine Stufe gröber nach zwei schnellen
Leseverlusten): beide Testsuiten, Normal- und Debug-Build ohne Warnung, als Testhost auf 13.00 HEVC 1080p, H.264 1080p
und HEVC HDR 4K sauber (dort ohne Wirkung, Ratenfelder).

Auf 10.20 vom Melder bestätigt (PS5 Pro, Moonlight 6.2 für Windows, je etwa 12 s): Mit der Rauschseite in 1080p bei 30 und
80 Mbit/s verlor der Stand `db5b258` den Read zweimal etwa 20 ms nach dem Öffnen, dann lief die dritte Öffnung mit 27/31 ohne
weitere Verluste (101–108 Mbit/s, auch über 30 s); jeder neue Stream begann wieder bei 24/28. Die Seite in 4K und ein
bewegter Clip in 1080p blieben bei 24/28 ohne Verlust (166–170 und 32–36 Mbit/s). Offen: Bei 27/31 kamen mit 30 Mbit/s
angefragt rund 104 Mbit/s, mit 12 Mbit/s am Morgen rund 49; die angefragte Rate wirkt also noch auf etwas anderes als den QP.

### Browser als Client über moonlight-web-stream (2026-11-08, 13.00, Host 0.5.0)

moonlight-web-stream v2.10.0 in Docker auf einem Linux-Rechner im LAN, als Moonlight-Client per PIN über `PIN` gepairt,
„PS5 Screen“ in headless Chrome geöffnet und `getStats()` der WebRTC-Verbindung alle 5 s gelesen: 30 s H.264 in
1920x1080 mit 59 bis 60 fps im Browser, 1913 Bilder dekodiert, 0 verworfen, 0 Pakete verloren, Opus-Ton dabei. Der Host
sah einen normalen Client (`STREAMS`: H.264, 1080p60, 7308 kbit/s). Nach dem Schließen des Tabs beendete moonlight-web den
Stream nach rund 25 s selbst. Am Host ist dafür nichts nötig. Nicht geprüft: ein TV-Browser, Eingabe, HEVC.

### UPnP, PR 43 (2026-11-08, 13.00)

Testhost mit PR 43 am Router des Besitzers (UPnP dafür kurz eingeschaltet), Mappings vom PC aus per SSDP und
`GetSpecificPortMappingEntry` abgefragt:
- Stand `d2c2778`: 5 von 6 Ports weitergeleitet, UDP 48000 lehnte der Router mit 729 (`ConflictWithOtherMechanisms`) ab.
  QUIT entfernte alle Mappings, der Host war nach rund 1 s beendet. Nach Ruhemodus fand der erste Durchlauf keinen Router
  („no router answered“), der nächste zwei Minuten später leitete wieder weiter.
- Stand `51b0082` (Entfernen mit 3 s Obergrenze, schnelle Wiederholung in der ersten Minute), Router-UPnP wieder aus:
  UPnP aus im Host: 20 s H.264 in 1080p60 fehlerfrei dekodiert, keine UPnP-Zeilen, QUIT rund 1 s. `upnp = 1`: QUIT 3 s
  und 85 s nach dem Start, beide Male `host stopped` in `host_diag.log` nach 0,5 s. Durchläufe etwa alle 17 s, „no router
  answered“ nach 70 s, im ersten Durchlauf nach der ersten Minute.
- Nicht geprüft: Mappings und Ruhemodus mit Stand `51b0082` (vom Mitwirkenden gegen einen nachgebauten Router und auf 10.20
  gemessen), zwei Router hintereinander.

### Konsolenmenüs, PR 38 (2026-11-08, 13.00)

Testhost mit PR 38, zwei Konsolenprofile, ein Testclient auf das zweite Profil beschränkt (`SETPROFILES`), die Menüs nur für
das erste (`SETMENUS`). Abfragen mit dem Zertifikat des Testclients:
- Stand `a4c38d16`: In `/applist` fehlte „PS5 Screen“, `/launch` von App 1 gab 403 „This app is hidden on this device“; ein
  Gerät mit freier Wahl sah es weiter. Mit den Menüs auch für das zweite Profil war es wieder da. `SETMENUS` lehnte ein
  unbekanntes Profil, eine doppelte ID und ein fehlendes Argument ab. Menüs mitten im Stream von „PS5 Screen“ entzogen: Das
  Pad wartete nach weniger als 0,4 s („its input waits“), der Stream lief weiter und endete sauber.
- Stand `959b67a` (das Flag `SceShellCoreUtilAppFocus` für die App mit dem Bildschirm): Es ließ sich auf 13.00 öffnen, die
  Warnung für den Rückfall kam nicht. GTA III vom beschränkten Gerät gestartet, 200 s, `--no-script` mit Steuerkreuz-Ticks:
  Mit dem Spiel vorne ging die Eingabe durch. PS lang auf einem zweiten Controller (über ps5-mcp): Home-
  Bildschirm, GTA lief weiter als großes Programm, das Pad wartete. ✕ auf der Kachel: Spiel wieder vorne, Eingabe ging
  durch. PS kurz (Kontrollzentrum über dem Spiel): Das Spiel blieb vorne, nichts geloggt. Stream fehlerfrei dekodiert.
- Nicht geprüft: echtes Moonlight, Tastatur und Maus eines beschränkten Geräts.

### Issue 39 auf 13.00 (2026-11-08, Testclient, nicht veröffentlicht)

Die Änderungen für 10.x (größere Modi bleiben nach einer Ablehnung am Ausgang auf Probe, der Rückfall auf 1080p bekommt den
Anteil der Bitrate, ein laufender Stream behält seine Größe) auf 13.00 nachgeprüft, wo 1440p und 4K ohne Schalter gehen:
- Stand `f98f318`: H.264 in 4K vom Home-Bildschirm, 3840x2160, fehlerfrei dekodiert, wie vor der Änderung.
- Stand `d1d70f8`: 4K vom Home-Bildschirm, 120 s, GTA III mitten im Stream gestartet und wieder geschlossen. Auf 13.00 nimmt
  das der Aufnahme nichts: kein `capture lost`, kein Wiederöffnen, 7183 Bilder in 3840x2160, fehlerfrei dekodiert. Der Weg,
  der den Stream beendet, statt die Größe zu wechseln, ist hier also nicht erreichbar; auf 10.20 prüft ihn ein Mitwirkender.

### Release 0.5.0 auf der Konsole (2026-11-07, 13.00)

App nach `/data/homebrew/PPSA47989` kopiert (`contentVersion` `01.000.024`) und neu registriert, Host-ELF nach
`/data/prosperosunshine/` und mit `host_restart.elf` neu gestartet: Kernel-Log `version=0.5.0`, Steuerport nur lokal.
Testclient, Home-Bildschirm, je 30 s 1080p60:

| Variante | H.264: Bilder, Lücken | HEVC: Bilder, Lücken |
| --- | --- | --- |
| Normal | 1793, 0 | 1792, 0 |
| Debug (`0.5.0-debug`, Selbsttest `problems=0`) | 1794, 0 | 1792, 0 |

Alle fehlerfrei dekodiert, ohne Tonfehler. Danach wieder die normale Variante über `host_restart.elf`.

### Debug-Build (PR 29) zusammen mit Profilen und Apps auf main (2026-11-07, 13.00)

Host `d19bce53` (PR 29 mit den Review-Änderungen auf main gemergt, Patches 0001 bis 0025), Testclient, Home-Bildschirm:
- Normal: `version=0.4.2`, H.264 und HEVC 1080p je 30 s mit 1792 und 1787 Bildern, keine Lücken, fehlerfrei dekodiert,
  ohne Tonfehler.
- Debug: `version=0.4.2-debug`, Selbsttest in 179 ms mit `ra=resolved by_nid=4`, `ports=free`, `mdns=ok`,
  `capture=opens … step=open only rc=0`, `problems=0`; das Kernel-Log zeigt jetzt „capture open only ok, closed again“
  statt „open failed“. `->`-Zeilen vor jedem Capture-Schritt und jedem Bind.
- Host-, Debug- und App-Build mit beiden Testsuiten grün. Nicht geprüft: die Release-Job-Änderung (nur beim nächsten Tag
  in CI sichtbar).

### Apps verstecken, Issue 36 (2026-11-07, 13.00)

Testhost auf `da426c3` mit den Änderungen für Issue 36 (Patch 0023), zwei gekoppelte Testclients, beide mit freier
Profilwahl, einer in `web_trusted.txt` eingetragen. Abfragen direkt mit dem Zertifikat des jeweiligen Clients:
- `SETAPPS PPSA99000 hidden` (ProsperoStore): `APPS` zeigt es `hidden`. Ohne vertrautes Gerät fehlte es in `/applist` bei
  beiden (36 statt 37 Einträge); mit einem vertrauten Gerät sah nur dieses es wieder.
- `/launch` mit der ID des Stores vom nicht vertrauten Client: HTTP-Status 403 „This app is hidden on this device“, nichts
  gestartet, Server blieb `SUNSHINE_SERVER_FREE`.
- Der vertraute Client startete den Store und streamte 40 s (H.264 1080p60, 2391 Bilder, fehlerfrei dekodiert). Währenddessen
  bekam der andere auf `/resume` 403 und auf `/launch` von PS5 Screen 400 („An app is already running“). Mit dem Ende
  schloss `/cancel` den Store wieder.
- `sunshine.log` nennt die Ablehnungen ohne Titel.
- Webseite: `/api/apps` listet 36 Titel; `/api/apps/hidden` versteckt den Store (danach fehlt er in `/applist` des nicht
  vertrauten Clients) und zeigt ihn wieder; eine ungültige Titel-ID und `"hidden":"yes"` gaben 400. Die Ansicht Apps vom
  Besitzer im Browser angesehen.
- Nicht geprüft: echtes Moonlight.

### Profile pro Gerät, PR 33 (2026-11-07, 13.00)

Testhost `1e8eab9d` (PR 33 auf `4a03c0b`), zwei Konsolenprofile (Besitzer und ein Testprofil), Testclient mit eigener
Kopplung (`stream_test`, ein Pad vom Typ PS ohne Touchpad, `--no-script` mit zwei Steuerkreuz-Ticks, teils Tasten):
- Beim ersten Start behielten die 12 gekoppelten Geräte die freie Wahl (`free`); `USERS`, `PROFILES`, `SETPROFILES` antworten.
- Gerät auf das Testprofil beschränkt, das an der Konsole angemeldet war: Pad nicht hinzugefügt („signed in on the console by
  someone else“). Ebenso mit dem Profil des Besitzers, das vorne angemeldet ist.
- Gerät auf `none`: Hinweis als Zuschauer beim Streamstart, kein Pad, keine Tastatur.
- Freies Gerät: Pad wie bisher als Controller des Hosts, sauber entfernt.
- Testprofil vorher an der Konsole abgemeldet: Der Host meldete es an, das Pad war nach 2 ms im Profil, ohne die Frage der
  Konsole; die Tastatur des Geräts bekam ein eigenes Gerät im Profil (`0x3000000170100`).
- Neu verbunden 10 s nach dem Ende: gleiches Profil ohne neue Anmeldung. 45 s nach dem letzten Pad abgemeldet (`rc 0`),
  keine Fehler beim Zurücksetzen der Rechte. Alle Streams fehlerfrei dekodiert.
- Nicht geprüft: Profilwechsel (nur ein Profil frei) und ein echter Controller, der über die Benutzerauswahl der Konsole
  dazukommt (vom Autor als offene Grenze genannt).

### Release 0.4.2 auf der Konsole (2026-11-07, 13.00)

App nach `/data/homebrew/PPSA47989` kopiert, Host-ELF nach `/data/prosperosunshine/` und mit `host_restart.elf` neu gestartet
(vorne lief ein Spiel, daher nicht über die App): Kernel-Log `version=0.4.2 commit=d440adfe`, Steuerport nur lokal. Das
Neuregistrieren lehnte ShadowMountPlus ab, solange das Spiel läuft (`game_active`), die App zeigt ihre neue Version also erst
nach dem nächsten Neuregistrieren. Testclient, Spiel vorne: H.264 und HEVC 1080p je 30 s mit je 1794 Bildern bei 59,94 bzw.
59,93 fps, fehlerfrei dekodiert, ohne Tonfehler. Der Fix für „PS5 Screen“ nach einem Stream (Moonlight hing bei
„Quitting PS5 Screen...“, im Host-Log kam kein `/cancel` an) vorher als Testhost mit Moonlight am PC vom Besitzer geprüft.

### Touchpad ohne Touchpad und „Restart host“ im Stream (2026-11-07, 13.00)

Moonlight für Windows meldet einen Xbox-Controller mit `type 01`, `capabilities 0043` und `supportedButtonFlags 0000F7FF`
(Debug-Log der Ankunft): kein Touchpad, keine Share-Taste (`0x200000`), auch wenn sie gedrückt wird. View und Menu zusammen
sind seit `8e1467b` der Touchpad-Klick für solche Controller; vom Besitzer mit diesem Xbox-Controller in einem Spiel
bestätigt, View allein, Menu allein und die Xbox-Taste wie zuvor.

„Restart host“ während eines Streams (Bericht aus Issue 30: auf 9.60 ging die Konsole dabei aus, mit dem Haptik-Kanal des
Forks offen) wie die App nachgestellt: Testclient streamt, vorne ein Spiel, `QUIT` an den Steuerport, dann das 0.4.1-ELF an
den Loader. Der Host beendet sich in unter 2 s sauber (Aufnahme und Ton gestoppt und geschlossen, Pad abgemeldet,
`exit_value=0`), die Konsole bleibt an, der neue Host kommt normal hoch. Der Client hatte bis dahin 1018 Bilder fehlerfrei
dekodiert; ein neuer Stream danach 20 s mit 1196 Bildern, fehlerfrei dekodiert, ohne Tonfehler. Auf 13.00 ohne Haptik-Kanal
also nicht nachstellbar; mit Moonlight am PC statt Testclient und auf 9.60 ungeprüft.

### Release 0.4.1 auf der Konsole (2026-11-07, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.022`; nach „Restart host“ meldet das Kernel-Log
`version=0.4.1 commit=cd0d808b`, und der Prozess heißt dort und in der Prozessliste `ProsperoSunshine` statt `payload.elf`
(Issue #32). Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1793 und 1792 Bildern bei 59,94 fps, fehlerfrei
dekodiert, ohne Tonfehler. `http://` auf Port 47990 bekommt 302 auf `https://` (Issue #31; vorher leere Antwort, curl
Fehler 52), ohne Host-Kopfzeile 400. Vor dem Release mit `control_lan` gemessen: Ein POST mit `STATUS` als Inhalt an den
Steuerport führte auf 0.4.0 `STATUS` aus, nach der Änderung schließt der Host die Verbindung ohne Antwort, `STATUS` und
`DEVICES` ohne HTTP antworten wie zuvor. Der Datensatz für 9.60 ändert an 13.00 nichts (Golden-Datei, Stream wie oben); auf
9.60 selbst ist er hier nicht prüfbar, ebenso der Dateizugriff der App über den Host, den nur eine Konsole ohne `/data` in
der Sandbox eines Titels nutzt.

### Debug-Build (Issue 28, 2026-11-06, 10.20, PS5 Pro, Testclient, Home-Bildschirm, nicht veröffentlicht)

Host `commit=47dc059f` in beiden Varianten, nacheinander per ELF-Loader gestartet; der installierte Host blieb unverändert.
Für die Messung war `min_log_level` aus `sunshine.conf` entfernt, damit jede Variante mit ihrer Voreinstellung läuft.

- **Debug, `host_diag.log`:** beginnt mit `version=0.4.0-debug commit=47dc059f … started=2026-11-06T16:47:…`, jede Zeile mit
  den Millisekunden seit dem Start; die Datei des vorigen Laufs liegt als `host_diag.log.1` daneben. Selbsttest in 185 bis
  191 ms: `console=PS5-Pro`, `ra=resolved by_nid=4 by_table=0`, `ports=free`, `mdns=ok`, `capture=opens` (OpenVideo ohne
  Start), `problems=0`. `->`-Zeilen vor jedem Capture-Schritt (Nachschlagen, Register, RUNNING, Initialize, OpenVideo, Start,
  OpenAudio, Stop/Close, Terminate, STOP, Unregister), vor den Binds (47984, 47989, 47990, 47995, 47996, 48010, 5353 und je
  Stream 47998 bis 48000) und vor QUIT.
- **Debug, `sunshine.log`:** Stufe Debug. Bei `/pair` stehen `clientcert` und `salt` als `<hidden>`, das Zertifikat des
  Clients als `<certificate>`, bei `/launch` `rikey` und `rikeyid` als `<hidden>`; im ganzen Log keine IPv4-Adresse (die des
  Clients als `<ip>`) und keine 32-stellige Hex-Folge. `Stream stats:` alle 5 s, einmal `Held back 2 similar lines …`. Das
  Kernel-Log blieb bei Info.
- **Rauschen:** RTSPs Zeile „No pending session for incoming RTSP connection“ kam auf Debug alle 5 s, ausgelöst von der
  Portüberwachung des Hosts. Patch 0024 stuft sie auf Verbose herab; danach kam sie nicht mehr.
- **Normal:** `version=0.4.0`, Zeitstempel, keine `->`- und keine `selfcheck`-Zeilen, `sunshine.log` auf Info.
- **Testclient** (Windows-Build, `--no-script`), H.264 und HEVC 1080p60 je 30 s:

| Variante | H.264: Bilder, fps, fehlend, längste Pause | HEVC: Bilder, fps, fehlend, längste Pause |
| --- | --- | --- |
| Debug | 1791, 60,01, 0, 86 ms | 1793, 59,96, 0, 43 ms |
| Normal | 1792, 59,95, 0, 43 ms | 1795, 59,96, 0, 142 ms |

  Alle Tonpakete ohne Dekodierfehler.

Nicht geprüft: Spiele (der Home-Bildschirm fordert den Encoder kaum, rund 0,5 Mbit/s), 13.00, das Fernlog (`remote_log`) an
der Konsole, und die Debug-App (`PPSA47989-debug.zip`, in CI gebaut) auf der Konsole.

### Release 0.4.0 auf der Konsole (2026-11-06, 13.00)

`main` mit PR 27 (Webseite) und 0.3.4 zusammengeführt, App aus `build/app` kopiert und neu registriert, sie zeigt
`v01.000.021`; nach „Restart host“ meldet das Kernel-Log `version=0.4.0 commit=a2b4231a` und „Web page on port 47990“.
Die Seite liefert `index.html` mit dem eigenen Zertifikat (`CN=ProsperoSunshine web page`, Adressen der Konsole), ohne Login
antworten `/api/overview`, `/api/settings` und `/api/logs` mit 401, ein falsches Login ebenso. Testclient, Home-Bildschirm:
H.264 und HEVC 1080p je 30 s mit 1793 und 1792 Bildern, ohne Lücke, längste Pause 20 ms, fehlerfrei dekodiert, je 5805
Tonpakete ohne Fehler. Die Bedienung der Seite mit Login prüfte ein Mitwirkender auf 10.20 und am iPhone (PR 27).

### Release 0.3.4 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.020`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.4 commit=29a15422`. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1795 und 1793 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5805 Tonpakete ohne Fehler.

### Gast-Pad erst bei der ersten Eingabe (2026-11-06, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Issue #26 (13.60): Jedes Gerät meldete zwei Controller, der zweite drückte nie etwas, und die Frage „Wer benutzt diesen
Controller?“ für ihn blieb stehen. Host `commit=171a5874` mit der Änderung (vor dem Commit), per ELF-Loader gestartet:

- `--pads 2`, nur Pad 0 aktiv: „pad 0 attached as the host's controller“, „pad 1 announced, opens when it is used“; Pad 1
  öffnete nie, am Ende „pad 1 gone, never used“. Danach zeigte der Startbildschirm keinen Dialog.
- `--pads 2 --tick-pad 1`: Pad 1 öffnete beim ersten Druck („attached, the console asks who uses it“), am Ende abgemeldet.
- A mit Controller, B tritt ohne Controller bei und drückt W: „pad 1 announced“, 4 ms später „keyboard and mouse … play as
  virtual pad 1“ und „attached, the console asks who uses it“. Alle Streams ohne Lücke, fehlerfrei dekodiert.

Nicht geprüft: echtes Moonlight mit Steam Input oder iOS, und der Fall, in dem das Phantom-Pad als erstes ankommt und damit
das Pad des Hosts ist (dann bliebe der echte Controller ein Gast seines eigenen Geräts).

**Bestätigt vom Melder** (Issue #26, 13.60, Testbuild `v0.3.3-pads-1`, 2026-11-06): Im `sunshine.log` seines Streams „pad 0
attached as the host's controller“, „pad 1 announced, opens when it is used“, am Ende „pad 1 gone, never used“; er meldet,
dass es jetzt geht. Der erste Versuch davor endete mit „Couldn't bind RTSP server to port [48010], Address already in use“:
der vorige Host lief noch.

### Release 0.3.3 auf der Konsole (2026-11-06, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.019` und installierte beim Start das gebündelte ELF;
nach „Restart host“ meldet das Kernel-Log `version=0.3.3 commit=461bdc8c`. Der neue Hilfetext zu „PS for all players“ passt
in die Zeile unter den Settings. Testclient, Home-Bildschirm: H.264 und HEVC 1080p je 30 s mit 1793 und 1790 Bildern, ohne
Lücke, längste Pause 19 ms, fehlerfrei dekodiert, je 5800 Tonpakete ohne Fehler; danach Pad 0 abgemeldet, „the host role
is free“.

**Tastatur-Pad auf 13.00** (derselbe Host, per ELF-Loader gestartet, vor dem App-Build): Testclient ohne Controller
(`--pads 0`, neu) mit `--kbm-seq`: Maus rechts und links, ein Radschritt, dann W und S. Maus und Rad öffneten weder ein Pad
noch die Maus der Konsole; W öffnete „PS5 virtual pad 0 attached as the host's controller“ und „The keyboard and mouse of a
client without a controller play as virtual pad 0“, am Streamende abgemeldet. Mit Controller (ohne `--pads 0`) gingen
dieselben Eingaben wie bisher an die virtuelle Tastatur und Maus der Konsole. Beide Streams ohne Lücke, fehlerfrei
dekodiert. Nicht geprüft auf 13.00: ein Spiel, die Profile, ein Gast mit Tastatur.

### Raumklang folgt einem breiteren Stream (PR 16, 2026-11-06, 13.00, Testclient, Home-Bildschirm)

Host `commit=c15327c3` (der PR-Stand), Schalter „Surround“ an, Konsole mit Stereo-Ausgabe. A streamt Stereo (40 s), B tritt
nach 10 s per `/resume` mit 5.1 bei (`--surround 51`, 20 s), beide H.264 1080p60 mit `--no-script`.

- Hostlog: A öffnet Typ 0 („2 channels“). Mit B: „sharing the channel another stream reads“, „6 channels“, dann beim Leser
  von A „12 channels from the console for 2 speakers“, also der Wechsel auf Typ 3. Keine Warnung, kein verworfenes Paket.
- A: 2391 Bilder ohne Lücke, fehlerfrei dekodiert, 7796 Tonpakete ohne Dekodierfehler, RMS je Kanal 0,0073 / 0,0062, auch
  über den Wechsel hinweg.
- B: 1183 Bilder ohne Lücke, fehlerfrei dekodiert, 6 Kanäle, RMS 0,0059 / 0,0054 / 0 / 0 / 0 / 0 (die Konsole gibt nur
  Stereo aus).

Nicht geprüft: eine Konsole mit 5.1-Ausgabe und ein abgelehnter Typ 3 (dann bleibt es bei Stereo bis zur nächsten Sitzung).

### Webseite (2026-11-05, 13.00, Branch `webui`, nicht veröffentlicht)

Host und App aus dem Arbeitsstand des Branches, über die App installiert. Den Login hat für den Test `WEBLOGIN SET` vom PC
gesetzt (dafür lag kurz `control_lan` im Datenordner, danach gelöscht und der Host neu gestartet; der Steuer-Socket war
danach vom PC aus wieder geschlossen). Gemessen mit curl vom PC im LAN:

- Ohne Login war 47990 zu; nach `WEBLOGIN SET` lieferte die Seite 200, die API ohne Cookie 401.
- Anmelden: ohne den Header `X-Prospero` 403, mit ihm 200 und ein Cookie `Secure; HttpOnly; SameSite=Strict`, die
  Content-Security-Policy im Kopf. Fünf falsche Passwörter je 401, der sechste Versuch 429 mit 30 s Wartezeit. Im
  `sunshine.log` nur „login from“ mit der Adresse, kein Passwort; ein Login dauerte rund 0,1 s.
- Übersicht, Geräte, Einstellungen (ohne die Experimental-Schalter) und Logs lieferten die Daten der Konsole.
- „Restart host“ über die API: nach etwa 4 s war der Host wieder da, der Login blieb, die alte Sitzung bekam 401.
- `RELISTEN`: „Starting the services anew“, danach „Web page on port 47990“ und ein neuer Login ging.
- Der Diagnosebericht über die Seite kam in 0,03 s, ohne Adressen und Gerätenamen.
- In der App: der Eintrag „Web page“ zeigte die Adresse und „On for tester“; „Turn off“ schloss 47990 sofort. Später „Set login“ über die
  Bildschirmtastatur (Name, dann Passwort als Punkte mit Zeichenzahl): „The web page is on“, und ein Login vom PC mit diesen
  Daten bekam 200.
- Die Oberfläche selbst lief gegen einen Nachbau der API im Browser am PC, auch in Handybreite; den echten Host konnte der
  Browser des Tests wegen des selbst signierten Zertifikats nicht öffnen.

Der Diagnosebericht, die Sperre über alle Adressen (20 Fehlversuche) und das Lesen nur des Log-Endes kamen mit einem zweiten
aufgespielten Stand; auf ihm liefen Bericht, Logs, Übersicht, Login und „Restart host“ erneut, die Sperre über alle Adressen
nur im Unit-Test, das Log-Ende nur mit einem kurzen Log.

Danach hat der Besitzer die Seite im Browser seines Handys gegen die Konsole geöffnet, sich angemeldet und sie angesehen:
ohne Befund.

Nicht geprüft: ein Browser am PC gegen die Konsole, PIN, „End
stream“, Umbenennen und Entfernen über die Seite, eine Adresse außerhalb des LAN, echter Ruhemodus.

### Webseite nach der Testrunde auf 10.20 (2026-11-05, 13.00, Testbuild `v0.3.2-webui-2`)

Host `bc38f990` aus dem Testbuild, App-Ordner und ELF per FTP aufgespielt, der alte Host beendet und das ELF gesendet.

- Zertifikat der Seite, mit `openssl s_client` vom PC gelesen: CN „ProsperoSunshine web page“, `CA:FALSE`, Key Usage
  Digital Signature, EKU TLS Web Server Authentication, SAN `localhost`, die Adresse der Konsole und 127.0.0.1, gültig
  825 Tage. `web_cert.pem` und `web_key.pem` lagen danach im Datenordner.
- `sunshine_state.json` gesichert und durch 15.283 Null-Bytes ersetzt, Host neu gestartet: „Moved the unreadable ...
  to sunshine_state.json.corrupt“, `/serverinfo` mit einer neuen uniqueid, die neue Datei 112 Bytes. Ein frischer
  Testclient (`pair.py`) paarte sich (PairStatus=1), die Datei wuchs auf 1.378 Bytes. Danach die Sicherung zurückgespielt;
  nach dem nächsten Start war die alte uniqueid wieder da.
- Ablehnen einer Anfrage (dafür lag kurz `control_lan` im Datenordner, danach umbenannt): `PAIRINGS` zeigte die Anfrage,
  `CANCELPIN <id>` „OK cancelled“, ein zweites „ERR no such request“, ohne ID „ERR usage“. Der wartende Client bekam
  HTTP 400 „Pairing request cancelled by operator“; derselbe Client paarte sich gleich danach ohne Wartezeit.
- „Restart host“ hat der Besitzer am Handy auf der Seite gedrückt: im Kernel-Log „restart asked for“, „host stopped“,
  dann vom Helfer „relaunch after the old host was gone (1250 ms): sent the host to the ELF loader“ und der neue Host bis
  „Web page on port 47990“. Danach lief nur noch ein Host-Prozess, der Steuer-Socket war wieder nur lokal.

Nicht geprüft: der Neustart auf 10.20, Safari auf dem iPhone mit dem neuen Zertifikat, „Turn on“/„Forget login“ und
„Deny“ in der App, R2/R3 in der Tastatur der App, „End stream“ auf dem Geräte-Tab, das gedrosselte Zertifikats-Log.

### Testbuild für 13.40 (2026-11-05, 13.00)

Pre-Release `v0.3.1-fw1340-1` für Issue #14: 13.40 und alles bis 13.60 bekommt den Datensatz von 13.60. Gemessen nur auf
13.00, wo sich nichts ändert (die 288 Datensätze in `capture_records_test` gleich): Testclient, Mario-64-Demo, je 30 s H.264
mit 1795 Bildern bei 59,94 fps und HEVC mit 1794 Bildern bei 59,95 fps, fehlerfrei dekodiert, Ton ohne Fehler. Auf 13.40
ungetestet, der Bericht des Testers steht aus.

### Berichte eines Mitwirkenden (2026-11-04, Discord, PS5 Pro mit 10.20)

- **HDR-Farben unter Windows:** Moonlight unter Windows mit HDR-Bildschirm und Hardware-Decoding, HEVC 10 Bit HDR 1920x1080,
  GTA III (Titelmenü). Laut Bericht und Foto vom Bildschirm stimmen die Farben jetzt, mit dem Host ab 0.2.4 (HDR10-Metadaten
  statt Nullen). Moonlights Statistik im Foto: rund 58 fps, keine verlorenen Bilder. Windows-Screenshots desselben Streams
  wirken überstrahlt; das liegt an der Aufnahme von HDR-Inhalten, nicht am Stream. Hier nicht gemessen.
- **Tailscale:** Mit [ps5-tailscale](https://github.com/holdmysocks/ps5-tailscale) ab 0.7.3 (sein Beitrag, UDP 47998–48000 in
  den Standardports) erreichte Moonlight auf iOS den Host über die Tailnet-Adresse der Konsole. Hier nicht geprüft.

### Berichte nach 0.3.1 (2026-11-05)

Hier nicht nachgemessen; die Angaben stammen aus den Berichten.

- **Slices bei H.264 auf 10.20 (Mitwirkender, Discord, PS5 Pro):** Host 0.3.1, Moonlight unter Windows mit H.264 1920x1080@60
  und Hardware-Decoding (D3D11VA). `host_diag.log`: `firmware 10.x: no rate fields, QP 24/28`, `OpenVideo rc=0`,
  `Start rc=0`, `first frame has 34 slices, more than hardware decoders on Windows take`, `capture open ok`. Die Felder
  `cfg+0x40/0x44/0x48` werden also angenommen und wirken nicht; der Rückfall greift nicht, weil nichts abgelehnt wird.
  Moonlights Log zeigt wiederholt `avcodec_send_packet() failed: Invalid data found when processing input` und „Resetting
  decoder due to consistent failure“, auf dem Bildschirm kein Bild.
- **Tailscale mit einer Konsole in einem anderen Haushalt (derselbe Mitwirkende):** aktuelles ps5-tailscale und Host 0.3.1,
  Moonlight streamte über das Tailnet.
- **Firmware 13.40 (GitHub-Issue 14, PS5 Slim Digital):** Testbuild `0.3.1-fw1340-1` (13.40 bekommt den Datensatz von
  13.60): `capture OpenVideo rc=0`, `Start rc=0`, `capture open ok`, Ton über Avcap2; Moonlight auf Android zeigte H.264.
  Eingabe und lange Sitzungen sind dort noch nicht berichtet.

### Raumklang auf 13.60 (2026-11-04, Bericht in PR 16 von BuffMcBigHuge)

Ein Mitwirkender baute parallel zu 0.3.1 denselben Weg (Typ 3 bei 5.1/7.1) und maß auf 13.60 mit echten 5.1-Quellen. Hier
nicht nachgemessen; die Angaben stammen aus seinem PR.

- Konsole auf HDMI, Gerätetyp AV-Verstärker, 5.1, Linear PCM. Quelle: die App „Surround Sound Studio“ (6-Kanal-PCM, ihr
  Audioport im Modus 6) und 007 First Light im Spiel. In Typ 3 trugen nur Kanal 0 und 1 Ton, 2–11 waren exakt 0.
- Mit erzwungenem 5.1-LPCM auf dem HDMI-Kabel (AV-Control: `port:HDMI ... 48k 5.1 ... fmt:LPCM`) blieb die Aufnahme gleich:
  Center und Surrounds lagen mit etwa −3 dB auf links/rechts, LFE war still.
- Sein Fernseher meldet im EDID nur 2-Kanal-LPCM. Seine Deutung: Die Systemmischung der Konsole folgt dem EDID und bleibt bei
  2.0, das Kabelformat ändert sie nicht. Offen ist der Test mit einem Gerät, das 6 oder 8 Kanäle LPCM meldet (AV-Receiver
  oder EDID-Emulator).
- `cfg+0x10 = 2` (AudioSecond) ist ebenfalls stereo; `cfg+0x10 = 1` (AudioAout) öffnet mit `rc=0`, liefert aber keine Pakete.
- Für 7.1 nimmt er Sunshines Reihenfolge an (hinten vor seitlich), der Host seit 0.3.1 die von `sceAudioOut` (seitlich vor
  hinten). Für 5.1 ergeben beide dasselbe. Gemessen ist keine.

### Belegter Steuerport (2026-11-05, 13.00, GitHub-Issue 24)

Auf 11.40 stürzte der Host beim Stream-Start ab: `SIGSEGV` im Thread `rtsp::handler`, Lesezugriff auf 0. Die Rücksprünge
aus dem Kernel-Log des Nutzers, auf das ELF von 0.3.2 abgebildet: `cmd_announce` → `session::start` → `start_broadcast` →
`control_server_t::bind` → `net::host_create`. Dort liest Sunshine `host->socket`, auch wenn `enet_host_create` mit
`nullptr` gescheitert ist (UDP 47999 nicht zu binden). Patch 0019 fängt das ab und loggt Port und errno.

Nachgestellt mit `out/port_hold_probe.elf` (bindet UDP 47999 für 60 s) und dem Testbuild `v0.3.2-fw1140-1` (der Patch hieß
dort 0017): Der Testclient bekam auf `ANNOUNCE` HTTP 500, `sunshine.log` meldete `ENet couldn't open UDP port 47999: Address
already in use (errno 48)` und `Couldn't bind Control server to port [47999]`, der Host lief weiter
(`SUNSHINE_SERVER_FREE`). Nach Freigabe des Ports, Mario-64-Demo: H.264 und HEVC 1080p je 30 s mit 1796 Bildern, ohne
Lücke, längste Pause 21 ms, fehlerfrei dekodiert. Laut Nutzer hielt ps5-tailscale den Port: Er hatte die Konsole selbst
unter „Game streaming“ eingetragen; ohne den Eintrag streamt er mit Tailscale (Bericht, hier nicht gemessen).

Auf `main` übernommen als Patch 0019 (2026-11-06, 13.00, Host mit allem bis PR 16): Mit der Probe auf 47999 scheiterte der
Stream mit RTSP 500, `sunshine.log` wie oben, der Host blieb bei `SUNSHINE_SERVER_FREE`. Nach Freigabe des Ports,
Home-Bildschirm: H.264 und HEVC 1080p je 20 s mit 1193 Bildern, ohne Lücke, fehlerfrei dekodiert, Ton ohne Fehler.

### Release 0.3.2 auf der Konsole (2026-11-05, 13.00)

App aus `build/app` kopiert und neu registriert, sie zeigt `v01.000.018`; nach „Restart host“ meldet das Kernel-Log
`version=0.3.2 commit=86c5eb2d` und `only the host can press PS and Create`. Testclient, Mario-64-Demo: H.264 und HEVC 1080p
je 30 s mit 1795 und 1796 Bildern, ohne Lücke, längste Pause 23 und 21 ms, fehlerfrei dekodiert, keine Tonfehler. Die
Host-Rolle selbst ist im Abschnitt zu Issue 15 gemessen, nicht noch einmal auf diesem Build.

### Firmware-Sonde auf 10.20 und 10.40 (2026-11-05, Bericht eines Mitwirkenden)

`firmware_probe.elf` aus diesem Stand (`docs/firmware-probe.md`), je ein Lauf ohne Optionen und ohne Proben, auf dem
Startbildschirm. PS5 Pro mit 10.20: Host 0.3.1 lief, `QUIT` hielt ihn an, am Ende lief das installierte ELF wieder. PS5 mit
10.40: kein Host lief, keiner wurde gestartet. Beide Läufe 147 s, der Startbildschirm blieb im Vordergrund. Hier nicht
nachgemessen; die Angaben stammen aus den beiden `report.txt`.

| | 10.20 (Pro) | 10.40 |
| --- | --- | --- |
| Datensatz des Hosts | `0x2c8` (−0x18), als einzige Größe von −0x40 bis +0x20 angenommen; Baukasten = `configure()` | ebenso |
| H.264 1080p60, 3 s | 180 Bilder, 34 Slices je Bild | 180 Bilder, 34 Slices |
| HEVC 8/10 Bit nur mit `cfg+0x24` | 17 Slices | 17 Slices |
| QP 20/24 in `0xc4`/`0xc8` gegen ohne | 3,5-fache Bytes; 52 scheitert an `Start` (`0x80c78000`) | 4,8-fache Bytes; 52 ebenso |
| `SetVideoBitrate` von 6 auf 20 Mbit/s | danach 1 Bild, dann `0x80c78000` | 0 Bilder, dann `0x80c78000` |
| ResourceArbitrator | per NID, Firmware in der Tabelle | per NID (`0xde0`, `0xfe0`, `0x1220`, `0x1360`), 10.40 nicht in der Tabelle |

- Auf beiden lehnt der Validator dieselben Wörter ab (`0x80`, `0x188`, `0x1fc`, `0x2b0`), die Bitratenfelder von 13.00
  lassen `Start` scheitern, ebenso H.264 10 Bit (`0x80c70004`) und HEVC mit den Remote-Play-Feldern von 13.00 (`0x81950001`).
- Alle zwölf Modi liefern Bilder (360p60, 540p60, 720p60, 1080p30, 1440p60, 2160p60 in H.264 und HEVC).
- **H.264-Slices:** Die Slice-Felder von 0.3.1 (`0x40`/`0x44`/`0x48`) nimmt die Firmware an, die Zahl der Slices ändern
  sie nicht: 360p 23, 540p und 1080p 34, 720p, 1440p und 2160p 45. HEVC: 6, 9, 12, 17, 23, 34. Über 32 Slices lehnen die
  Hardware-Decoder von FFmpeg unter Windows ab.

### Firmware-Sonde auf 9.60 (2026-11-06, PS5, nicht Pro, Bericht von IlushaKAI in Issue 30)

Hier nicht nachgemessen, es gibt keine Konsole mit 9.60. Die Angaben stammen aus dem Fork des Mitwirkenden (Branch
`firmware-9.60`, `STATUS.md` dort), `firmware_probe.elf` aus 0.3.4, ein Lauf ohne Optionen, 137 s.

- **Datensatz:** Von −`0x40` bis +`0x20` öffnet nur `0x2c0` (−`0x20`), ohne innere Köpfe. `0x2c8` (10.x), `0x2d0`, `0x2e0` (13.00)
  und `0x2e8` (13.60) lehnt der Validator mit `0x81950001` ab. Er schützt `0x80`, `0x168`, `0x1d8` und `0x2a8`.
- **Bilder:** H.264 und HEVC 8/10 Bit (nur `cfg+0x24`) von 360p bis 1440p. 2160p scheitert an `OpenVideo` mit `0x81950008`
  (`video out mode` `0x34`, ohne das 4K-Bit). Slices wie auf 10.x: H.264 1080p 34, HEVC 17.
- **QP:** `cfg+0xc4`/`+0xc8` ändern hier nichts. Die Phase `ratesearch` fand ihn in `cfg+0xac`/`+0xb0`, 0x18 tiefer als auf 10.x.
  Startbildschirm, 1 s, IDR alle 6 Bilder: ohne 4692 kbit/s, 24/28 6275, 18/22 11016, 12/16 19315, 8/12 28197.
  `SetVideoBitrate` mit 1 bis 12 Mbit/s antwortet 0 und ändert nichts, 16 und 20 Mbit/s beenden den Kanal (`0x80c78000`).
- **Systembibliotheken:** `libSceAvcap2` hat 5 Exporte weniger als 13.00. ResourceArbitrator per NID an den Offsets von 10.20.
- **Streams:** Mit dem Datensatz `0x2c0` auf 0.3.4 H.264 1080p60 mit Ton und Controller aus Moonlight für Android. Auf 0.4.0 mit
  denselben Änderungen 2 Minuten HEVC HDR in einem Spiel. 1440p im Spiel fiel auf 1080p zurück. Die Webseite lief dort nicht.
- **Die App sieht `/data` auf dieser Konsole nicht** (Sandbox eines Titels). Ohne die Brücke über den Host im Fork kann die App
  dort keine Einstellungen speichern.
- **Drei ungeklärte Abschaltungen** der Konsole während der Tests, die Logs enden davor. Die Konsole schaltet sich laut
  Mitwirkendem auch sonst gelegentlich ab.
- **Übernommen** (2026-11-07): der Datensatz `0x2c0`, der QP-Offset (`ps_qp_offset`) und die Probe-Phasen `ratesearch` und
  `qp`. 9.60 bleibt ungeprüft (`known=0`, Experimental-Schalter wie auf anderen ungeprüften Versionen). Alles unter 10 bekommt
  diesen Datensatz, für 8.40 (Issue 23) ist das geraten. Für 13.00 und 10.20 bleiben die Datensätze byte-gleich (`tests/capture_records.golden`).

### Tastatur-Pad nach dem Review von PR 20 (2026-11-05, 10.20, PS5 Pro)

Host aus `keyboard-mouse-pad` (0.3.2 mit den Patches 0017 und 0018 und den Korrekturen aus dem Review), vorübergehend per
ELF-Loader gestartet. Moonlight für Linux auf einem PC ohne Controller, dazu Hostlog und Kernel-Log der Konsole.

- **Maus und Rad allein öffnen kein Pad:** Im zweiten Stream (gut 1,5 Minuten) bewegte der Besitzer nur die Maus und drehte
  das Rad, dazu ein Klick, um das Fenster nach vorn zu holen. Kein „play as virtual pad“, im Kernel-Log kein `DEVICE_ADDED`.
  Im ersten Stream hatte ein Klick ins Bild das Pad geöffnet (die linke Maustaste ist in `standard` R2), wie vorgesehen.
- **Die erste Taste öffnet es:** im dritten Stream 3 s nach dem Verbinden „PS5 virtual pad 0 attached as the host's
  controller“ (die Rolle aus 0.3.2). Die Konsole wies es dem Benutzer zu, ohne „Wer benutzt diesen Controller?“.
- **Spiel und Menüs:** „menu keys, no game running“ auf dem Home-Bildschirm, `gta` für GTA III aus der eigenen Liste. Dort
  laut Besitzer Laufen, Kamera, Zielen und Schießen mit der Maus und der Waffen-Cheat (3 3 Q 3 und die Pfeiltasten)
  einwandfrei.
- **Streamende:** „pad 0 detached (0), the host role is free“; im nächsten Stream öffnete die erste Taste das Pad wieder.
- **Mausempfindlichkeit:** Strg+Alt mit dem Rad, rund 60 Schritte im Log, bei schon offenem Pad. `keyboard_sensitivity.txt`
  hielt danach den letzten Wert (750 % für `standard`), eine `.new`-Datei blieb nicht übrig.

- **Nach dem zweiten Review (das Pad entsteht beim Druck, bevor es ihn sieht):** gut 40 s Maus, Rad, Shift und M einzeln
  und Strg+Alt mit dem Rad (drei Schritte im Log; in `standard` ist Strg kein Knopf): kein Pad, kein `DEVICE_ADDED`. W
  öffnete es als Pad des Hosts, dem Benutzer zugewiesen; am Streamende „the host role is free“.

Nicht geprüft (nur im Unit-Test oder im Code): kein Pad frei (vier belegt) und der neue Versuch nach 10 s, Strg zuerst in
`shooter` oder `sports`, eine Taste, die nach dem Abziehen eines Controllers wiederholt wird.

### Host-Rolle am Gerät (2026-11-05, 13.00, Testclient, Home-Bildschirm, nicht veröffentlicht)

Host `commit=roles-1` (der Stand dieses Commits ohne die Doku), drei gepairte Identitäten des Linux-Testclients (A, B, C),
alle H.264 1080p60 auf „PS5 Screen“ mit `--no-script`; B, C und A beim zweiten Mal treten per `/resume` bei. Ablauf und Log:

- A allein: Pad 0 „attached as the host's controller“.
- B tritt bei: Pad 1 „the console asks who uses it“. Drei PS-Drücke von B (`--tick-kind ps`): dreimal „PS or Create
  ignored, only the host has them“.
- A geht (sein `/cancel` beendet nur seine Sitzung): „pad 0 detached (0), the host's device keeps the role“.
- C tritt bei und bekommt den freien Slot 0: „the console asks who uses it; the host is away and keeps the role“. Zwei
  Select-Drücke von C (`--tick-kind back`, Create): beide ignoriert.
- C geht, A tritt wieder bei: Pad 0 „attached as the host's controller“.
- A geht, dann B; danach sind alle Pads abgemeldet (`STREAMS` leer). Alle vier Streams fehlerfrei dekodiert (1795, 4479,
  883 und 704 Bilder).
- Ein Lauf davor ohne Beitritt (B, C und A rief `/launch` statt `/resume`): Jedes Gerät war allein, wurde Host, und nach
  seinem Abmelden war die Rolle frei („the host role is free“).

Nicht geprüft: echtes Moonlight, ein echter DualSense an der Konsole, und ob der Dialog „Wer benutzt diesen Controller?“
einem Gast den Nutzer des Hosts anbietet.

## Bauen und testen

Unter Linux mit Docker, aus dem Wurzelordner des Repos. Das Image `prosperosunshine-sdk:v0.45` entsteht einmal aus
`scripts/Dockerfile`:

```sh
docker build -t prosperosunshine-sdk:v0.45 -f scripts/Dockerfile .
docker run --rm -e PROSPERO_COMMIT=$(git rev-parse --short=8 HEAD) --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/build.sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Der Build baut zuerst fehlende Abhängigkeiten (`out/deps`, erster Lauf ~30 min, vor allem Boost), dann alles mit CMake nach
`out/`. Mit `-e PROSPERO_DEBUG=1` entsteht die Debug-Variante in `out/debug/`. `scripts/test.sh` führt die portablen Tests
unter ASan/UBSan aus. Unter Windows tut `scripts/build.ps1` (`-Debug`) dasselbe. App, Testclient und die übrigen Befehle:
`CLAUDE.md`, „Build and test“, und README, „Building from source“.

Host starten: `out/prospero_sunshine.elf` an den ELF-Loader (Port 9021) schicken, oder „Restart host“ in der App. Befehle des
Steuer-Sockets (TCP 47995): `CLAUDE.md` und README, „Development control socket“. Testclient, Pairing und Smoke-Test:
`tools/moonlight/README.md`.

## Wichtige ABI-Erkenntnisse (FW 13.00)

- Avcap2-Videokonfiguration 0x2E0 Bytes; `0x2A4 = 2` zusammengesetzter Bildschirm; RA-Profil 180 (keine Benutzer-ID); Details in `src/platform/ps5/capture.c`.
- Audio-Konfiguration 0x38 Bytes; `cfg+0x1C` ist dieselbe RA-Id 180.
- Descriptor-Wort 9 = Aufnahmezeit in TSC-Ticks (~1,596 GHz); Wort 3 Bildtyp; Wort 4 Framezähler (`docs/avcap2-descriptor.md`).
- `sceAvcap2*` werden per Name über `kernel_dynlib_dlsym` aufgelöst; Export-Tabelle mit 59 NIDs in `docs/re/avcap2-exports.md`.
- `__builtin_cpu_supports` und AVX-Typen in `immintrin.h` funktionieren auf dem `__SCE__`-Ziel nicht ohne Umweg (siehe `scripts/deps/nanors.sh`).
- `std::jthread` braucht `-fexperimental-library` (im CMake gesetzt).

## Verwandtes Repository ps5-mcp

Die direkte Avcap2-Screenshot-Methode für `ps5_screenshot` ist dort umgesetzt und live geprüft (59 Python-Tests, Selbsttest mit elf Prüfungen bestanden). Nicht veröffentlicht; Restpunkte: veralteter Kopfkommentar in `payload/avcap_probe/avcap_probe.c`, ungetesteter Release-Download des neuen ELF.

## Herkunft und Lizenz

Sunshine `a679b059` und alle Bibliotheken mit Pins in `deps.lock`; Lizenztexte nach dem Build in `out/deps/licenses/`. Eigene Änderungen an Sunshine nur als begründete Patches in `patches/sunshine/` (0001 bis 0025, jede mit ihrer Begründung im Kopf; die ersten: 0001 libvirtualhid, 0002 NVENC/VAAPI-Konstanten, 0003/0004 ohne libcurl). Analysen von Sony-Binärdateien enthalten keine Sony-Daten; die entschlüsselten Module liegen nur im ignorierten `.cache/re`.
