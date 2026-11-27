# Energie und Touchpad auf dem MacBookPro14,2

Diese optionale Konfiguration ist für das **MacBookPro14,2** mit Debian 13,
KDE Plasma und Kernel `6.12.116+deb13-amd64` geprüft. Sie ist nicht als
allgemeine MacBook-Konfiguration gedacht.

## s2idle-Energiesparen

Der Hook gilt für `s2idle`. Er schaltet beim Einschlafen die
Tastaturbeleuchtung aus und stellt sie nach dem Aufwachen wieder her.
Mit `MODE=wifi` und `WIFI_KEEP_BOUND=1` bleibt der WLAN-Treiber eingebunden,
wenn `brcmfmac` mit aktivem `mbp142_s2idle_keep_d0` läuft. Die Firmware kann
dann schlafen, während das PCI-Gerät in D0 bleibt. Ohne diese Option oder
ohne aktiven D0-Schutz koppelt der Hook WLAN vorübergehend ab und bindet
es nach dem Aufwachen wieder ein. Im WLAN-Modus bleiben die USB-C- und
Thunderbolt-Treiber eingebunden, sodass ihre Controller in D3hot wechseln
können. Der interne T1-/iBridge-Pfad für Touch Bar und Touch ID bleibt verfügbar.

Auf einem MacBookPro14,2 lag die gemessene Leistungsaufnahme im Akkubetrieb
mit eingebundenen Treibern in zwei Vergleichen niedriger (1,85 gegenüber
2,06 W und 4,17 gegenüber 4,80 W). Das sind Messwerte eines Geräts bei
unterschiedlich langen Läufen. Die CPU blieb in den Tests bei Package C3,
und der S0ix-Zähler blieb bei null. S3/deep war auf dem Testgerät beim
Aufwachen unzuverlässig. Hibernation ist mit dem Abschnitt
[Ruhezustand](#ruhezustand-hibernate) nutzbar.

Mit `THUNDERBOLT_L1=1` aktiviert der Hook ASPM L1 auf beiden internen
Thunderbolt-Links und stellt die vorherigen Link-Control-Werte nach
dem Aufwachen wieder her. Bei externem Thunderbolt-Router oder Display
überspringt er die Änderung. Ein USB-C-Stick blieb an beiden Zweigen
nach dem Aufwachen lesbar. Der zweistündige Akkutest ergab 2,93 W
und 96,1 % Package C10. Mit `THUNDERBOLT_L1=0` lässt sich L1 separat abschalten.

Installation:

```bash
sudo install -m 0755 systemd/macbook-s2idle-powersave \
  /usr/lib/systemd/system-sleep/macbook-s2idle-powersave
sudo install -m 0644 systemd/macbook-s2idle-powersave.conf \
  /etc/macbook-s2idle-powersave.conf
sudo install -m 0755 bin/macbook-s2idle-recover \
  bin/macbook-s2idle-powersave-control /usr/local/sbin/
```

Der Hook gilt ab dem nächsten Zuklappen; ein Neustart ist nicht erforderlich.

```bash
sudo macbook-s2idle-powersave-control status
sudo macbook-s2idle-powersave-control disable
sudo macbook-s2idle-powersave-control enable
```

Sollte nach einem Aufwachen ein verwalteter Controller fehlen:

```bash
sudo macbook-s2idle-recover
```

Globale Optionen wie `pcie_aspm=force`, erzwungenes NVMe-D3cold oder S3/deep
gehören nicht zu diesem Stand.

### Schlaf-Bremse

Verweigert die WLAN-Firmware nach einem langen Schlaf die D3-Anfrage
(`brcmf_pcie_pm_enter_D3: Timeout`), scheitert jeder Schlafversuch mit -5,
und logind versucht es alle 25 s erneut, im beobachteten Fall die ganze
Nacht. Der Hook merkt sich ein Scheitern an `0000:02:00.0` in
`/run/macbook-s2idle-wifi-failed`. Beim nächsten Versuch löst er das WLAN für
diesen Schlaf vom Treiber, danach bindet er es mit frischer Firmware neu an.
Ein gelungener Schlaf hebt die Marke auf. Schalter: `WIFI_FAIL_RESET=1`.

## Thunderbolt-Controller abschalten

macOS schaltet beide Alpine-Ridge-Controller stromlos, solange an ihrer Seite
nichts angesteckt ist. Das Modul `mbp142_tb_poweroff` macht dasselbe: Beim
Einschlafen meldet es beide Controller ab (Go2Sx), setzt Link Disable und
schaltet sie über Apples ACPI-Methoden `SXFP`/`XRST` ab. Beim Aufwachen stellt
es sie über `TRPE` wieder her. Ist ein externes USB- oder Thunderbolt-Gerät
angesteckt, bleiben die Controller an. Schlägt eine Prüfung fehl, schläft das
System normal. Nach einer gescheiterten Wiederherstellung schaltet sich das
Modul bis zum Neuladen ab.

Mit `THUNDERBOLT_AWAKE_POWEROFF=1` schaltet `macbook-tb-awake` eine Seite auch
im Wachbetrieb ab, wenn dort 40 s lang nichts hängt. Bei abgeschaltetem
Controller führen die USB-C-Ports ihre USB-2-Leitungen zum Chipsatz, USB-Geräte
funktionieren also sofort mit USB-2-Tempo. Die udev-Regel schaltet den
Controller dann ein und startet den Chipsatz-Port kurz neu, sodass das Gerät
mit vollem Tempo ankommt. Ein USB-C-Monitor ohne USB-Gerät wird so nicht
erkannt: `sudo macbook-tb-awake on`.

Messung auf Akku (1 h): 1,36 W statt 2,56 W im Schlaf.

Wach im Leerlauf mit ausgeschaltetem Bildschirm braucht das Gerät auf Akku
3,7 W mit beiden Seiten aus und 7,2 W mit beiden Seiten an.

Mit `THUNDERBOLT_AWAKE_L1=1` setzt `macbook-tb-awake` ASPM L1 auf Root Port und
Alpine-Ridge-Bridge jeder eingeschalteten Seite (nach dem Anstecken, nach `on`
und nach jedem Zustandswechsel). Die Firmware lässt diese Links auf „ASPM
Disabled“, und Linux fasst ASPM nicht an, weil das FADT keine ASPM-Unterstützung
meldet. Ohne L1 bleibt das CPU-Paket bei eingeschaltetem Controller in PC3. Mit
beiden Seiten an: 5,5 W statt 7,1 W, Paket 0,78 W statt 2,17 W. Mit
angestecktem USB-Stick sinkt das Paket von etwa 1,9 W auf 0,93 W, bei gleicher
Datenrate. Bei externem Thunderbolt-Router oder Bildschirm wird L1
zurückgenommen. Schalter auf 0 nimmt es innerhalb von 20 s zurück.

## T1-Schlafmeldung

macOS schickt der T1 vor jedem Schlaf `ASOC.SOCW(0)` („Host schläft“) und
danach `SOCW(1)` über die EC-Mailbox. Linux tat das nie, die T1 lief im Schlaf
also voll weiter. Mit `T1_SOCW=1` schickt der Hook beide Meldungen über das
Modul `mbp142_t1_socw` und startet danach die T1Bridge-Dienste neu. Gemessen
(2 h auf Akku, zusammen mit der Thunderbolt-Abschaltung): etwa 0,6–0,9 W im
Schlaf, im Bereich von macOS auf demselben Gerät.

Module bauen und installieren (nach jedem Kernelupdate wiederholen):

```bash
for m in mbp142_tb_poweroff mbp142_t1_socw; do
  make -C /lib/modules/$(uname -r)/build M="$PWD/modules/$m" modules
  sudo install -D -m 0644 "modules/$m/$m.ko" \
    "/usr/local/lib/macbook/$(uname -r)/$m.ko"
done
sudo install -m 0755 bin/macbook-tb-poweroff bin/macbook-tb-awake /usr/local/sbin/
sudo install -m 0644 systemd/macbook-tb-poweroff.service \
  systemd/macbook-tb-awake.service /etc/systemd/system/
sudo install -m 0644 systemd/90-macbook-tb-awake.rules /etc/udev/rules.d/
sudo systemctl daemon-reload
sudo systemctl enable --now macbook-tb-poweroff.service macbook-tb-awake.service
```

Die Schalter `THUNDERBOLT_POWEROFF`, `THUNDERBOLT_AWAKE_POWEROFF`,
`THUNDERBOLT_AWAKE_L1` und `T1_SOCW`
stehen in `/etc/macbook-s2idle-powersave.conf`.

## Aufwachen beim Aufklappen

Das Aufklappen weckte das System nicht, der Bildschirm blieb dunkel, bis eine
Taste gedrückt wurde. Der Deckel meldet sich nur über den EC: Die Abfrage `_Q20`
löst `Notify(LID0, 0x80)` auf dem EC-GPE (0x07) aus. Der eigene Weck-GPE 0x6F
von LID0 löst unter Linux nie aus. Linux schaltet den EC-GPE im s2idle nur
scharf, wenn ein LPS0-Gerät existiert (`lps0_device_attach()` in
`drivers/acpi/x86/s2idle.c`), und dieses Gerät gibt es hier nicht. Im s2idle war
der EC-GPE also aus, und nur die SPI-Tastatur konnte wecken. Das
Deckel-Ereignis wurde erst nach diesem Aufwachen verarbeitet, deshalb stand im
Journal trotzdem „Lid opened“.

`mbp142_lid_wake` macht, was der Kernel auf LPS0-Geräten macht: Es markiert den
EC-GPE als Weckquelle und setzt seine Weckmaske zwischen `PM_SUSPEND_PREPARE`
und `PM_POST_SUSPEND` (nur wenn s2idle der Standard ist; Ruhezustand und
Ausschalten sehen die Maske nie). Im s2idle wird das EC-Ereignis dann
verarbeitet, das Deckel-Notify meldet einen Weckgrund, und das System wacht auf
(„Wakeup after ACPI Notify sync“). Gemessen: 45 min Schlaf auf Akku ohne einen
einzigen EC-Zwischenwecker, Verbrauch im selben Bereich wie ohne Modul.

```bash
make -C /lib/modules/$(uname -r)/build M="$PWD/modules/mbp142_lid_wake" modules
sudo install -D -m 0644 modules/mbp142_lid_wake/mbp142_lid_wake.ko \
  "/usr/local/lib/macbook/$(uname -r)/mbp142_lid_wake.ko"
sudo install -m 0755 bin/macbook-lid-wake /usr/local/sbin/
sudo install -m 0644 systemd/macbook-lid-wake.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now macbook-lid-wake.service
```

Der Schalter heißt `LID_WAKE` in `/etc/macbook-s2idle-powersave.conf`. Ab dem
nächsten Schlaf abschalten: `0` nach
`/sys/module/mbp142_lid_wake/parameters/enabled` schreiben oder das Modul
entladen.

## Keybag-Relay beim Start

`t1bridge-keybag` scheiterte bei jedem Start einmal („prepare shared keybags
failed“) und lief erst beim automatischen Neustart. Das Diagnoseprotokoll zeigt
den Grund: Nach dem Booten hat das SEP keine Geräte-Keybag. Der Relay legt sie
an, befördert sie und fragt sofort den Sperrstatus ab. Die erste Antwort meldet
noch „nicht entsperrt“ (Flags `0x14`, Sperrwert 2), 100 ms später ist sie
entsperrt. `keybag-lockstate.patch` (gegen T1Bridge v0.1.12) wiederholt nur
diese Leseabfrage bis zu 2 s. Mit dem Patch läuft der Relay beim ersten
Versuch durch (`NRestarts=0`).

```bash
# im T1Bridge-v0.1.12-Quellbaum, libsystemd-dev wird gebraucht
patch -p1 < /pfad/zu/touchbar_t1_project/keybag-lockstate.patch
cargo build --release --locked -p t1-daemons --features keybag-relay-service \
  --bin t1-keybag-relay
sudo install -D -m 0755 target/release/t1-keybag-relay \
  /usr/local/lib/macbook/t1-keybag-relay
sudo install -D -m 0644 /pfad/zu/touchbar_t1_project/systemd/t1bridge-keybag-lockstate.conf \
  /etc/systemd/system/t1bridge-keybag.service.d/20-lockstate-fix.conf
sudo systemctl daemon-reload
```

Rückweg: den Drop-in löschen und `systemctl daemon-reload`, dann läuft wieder
der Relay aus dem Paket.

## Ruhezustand (Hibernate)

Nach S4 blieben Touch Bar und Touch ID dunkel, und das WLAN stürzte ab. Der
Hook `macbook-hibernate` behebt beides: Vor dem Ruhezustand meldet er WLAN ab,
setzt das Gerät auf D3hot und schickt der T1 `SOCW(0)`. Danach holt er WLAN
zurück, schickt `SOCW(1)` und startet die T1Bridge-Dienste neu. Mit
`HibernateMode=shutdown` schaltet der Rechner nach dem Speichern ganz aus.
Gemessen: höchstens etwa 0,4 W im Ruhezustand (vorher 1,3 W ohne diese
Schritte).

Bluetooth: Nach S4 lehnte der UART-Chip (BCM20703, `hci_uart_bcm`) jeden
HCI-Befehl mit „Command Disallowed“ ab (Kernel: `Opcode … failed: -16`). Maus
und Telefon verbanden sich nicht mehr, und Aus- und Einschalten des Adapters
half nicht. Darum löst der Hook `serial1-0` vor dem Ruhezustand vom Treiber
und bindet es danach neu an. Anschließend setzt er die Gerätklasse von Tether
neu (`tether-btclass@hci0`). Schalter: `BT_HIBERNATE_RESET=1`.

Voraussetzungen sind eine Swapdatei und `resume=`/`resume_offset=` auf der
Kernel-Befehlszeile:

```bash
sudo fallocate -l 10G /swapfile && sudo chmod 600 /swapfile
sudo mkswap /swapfile && sudo swapon --priority 10 /swapfile
echo '/swapfile none swap sw,pri=10 0 0' | sudo tee -a /etc/fstab
sudo filefrag -v /swapfile | awk 'NR==4 {print $4}'   # Wert für resume_offset
```

Dann `resume=UUID=<UUID des Root-Dateisystems> resume_offset=<Wert>` in den
Starteintrag aufnehmen. Ein mit `grub-mkstandalone` gebautes Image braucht bei
Root auf LVM das vorab geladene Modul `lvm` und darf nicht mit
`--install-modules` beschränkt werden. Neue Start-Images vorher in QEMU/OVMF
mit der ganzen Platte prüfen (nur lesend).

```bash
sudo install -m 0755 systemd/macbook-hibernate \
  /usr/lib/systemd/system-sleep/macbook-hibernate
sudo install -D -m 0644 systemd/60-macbook-suspend-then-hibernate.conf \
  /etc/systemd/sleep.conf.d/60-macbook-suspend-then-hibernate.conf
```

In den KDE-Energieeinstellungen für Akku beim Zuklappen „Standby“ und als
Schlafart „Standby, dann Ruhezustand“ wählen. Nach 60 min auf Akku wechselt
der Rechner dann in den Ruhezustand, am Netzteil bleibt es beim Schlaf.

## Touchpad beim Tippen

Die Apple-SPI-Tastatur dieses Modells meldet sich unter Linux als
`0000:0000`. Daher erkennt libinput sie nicht immer als interne Tastatur und
„Touchpad beim Tippen deaktivieren“ greift dann unzuverlässig. Die mitgelieferte
Quirk-Datei stellt nur diese Zuordnung her; sie verändert nicht die
Palmenerkennung des Touchpads.

Falls `/etc/libinput/local-overrides.quirks` bereits existiert, den Abschnitt
aus der Projektdatei ergänzen und die Datei nicht blind überschreiben.
Andernfalls:

```bash
sudo install -D -m 0644 libinput/local-overrides.quirks \
  /etc/libinput/local-overrides.quirks
```

Danach einmal ab- und wieder anmelden. In KDE muss „Touchpad beim Tippen
deaktivieren“ eingeschaltet sein.

Die abschließenden Messwerte und der SSD-Wiederanlauf sind im [Schlafbefund](sleep-results.de.md) dokumentiert.

## Textstart ohne Fehlerzeilen

Apples ACPI-Tabellen erzeugen bei jedem Start Kernelfehler (`AE_ALREADY_EXISTS`
für die CPU-SSDTs, fehlendes `\_SB.OSCP`), dazu kommen harmlose Meldungen von
Bluetooth, DMAR und SGX. Mit `quiet loglevel=3` zeigt der Kernel sie nicht an.
Sie erschienen trotzdem türkis auf dem Bildschirm: Plymouth 24 startet aus der
initrd auch ohne `splash` (das initramfs-Skript nimmt `SPLASH=true` an), liest
die Kernelmeldungen selbst und gibt sie an `loglevel=3` vorbei aus.

Für einen Textstart mit den systemd-Zeilen `[  OK  ]` und ohne Fehlerzeilen
`splash` aus der Startzeile nehmen und ergänzen:

```
quiet loglevel=3 systemd.show_status=true plymouth.enable=0
```

Der Kernel filtert dann selbst. In rEFInd verhindert `graphics on` im
`menuentry` den Textbildschirm „Booting OS“ vor GRUB.

## Automatische Helligkeit nach dem Lichtsensor

Der Umgebungslichtsensor hängt als HID-Sensor (`05AC:8600`) an der T1.
Debian lädt `hid_sensor_als`, und `iio-sensor-proxy` meldet ihn über D-Bus.
Plasma 6.3 nutzt ihn nicht. `als-brightness` ist ein kleiner Benutzerdienst mit
zwei Modi, festgehalten in `~/.config/als-brightness.mode`:

- **AUTO**: Die Helligkeit folgt dem Licht über eine logarithmische Kennlinie
  (unter etwa 5 lux flach, dort rauscht der Sensor) plus einen festen Versatz,
  −5 Prozentpunkte, sofern `~/.config/als-brightness.json` kein `"offset"`
  setzt. Beim Einschalten von AUTO geht die Helligkeit sofort auf den zum Licht
  passenden Wert, egal was vorher von Hand eingestellt war; im dunklen Raum
  etwa 20 %. Gelernt wird nichts. Heller regelt er nach etwa 1 s, dunkler nach
  etwa 4 s, und nur bei deutlicher Lichtänderung.
- **Manuell**: Der Dienst tut nichts und gibt den Sensor frei.

Die Sonne links im Helligkeitsregler der Touch Bar ist der Schalter: blau
gefüllt bei AUTO, weiß bei manuell. Ziehen am Regler oder die Helligkeitstasten
schalten auf manuell. Das braucht den Renderer aus `renderer.patch`; ohne
Modusdatei bleibt die Sonne ein reines Symbol. Auch der KDE-Regler schaltet auf
manuell.

Der Dienst schreibt die Helligkeit über logind (`Session.SetBrightness`), auf
demselben Weg wie der Touch-Bar-Dienst. Über KDE (`org.kde.ScreenBrightness`)
zuckt der Bildschirm nach jeder Touch-Bar-Änderung nach: KDE bemerkt den
Touch-Bar-Wert nicht und animiert beim nächsten Setzen von seinem veralteten
Wert aus. Seinen alten Wert schreibt KDE trotzdem selbst zurück, beim
Profilwechsel zwischen Netzteil und Akku und beim Aufwachen aus dem Abdunkeln;
in AUTO stellt der Dienst dann binnen etwa 0,3 s den Kennlinienwert wieder her.
Während KDE abdunkelt (ein Schritt auf 0,3, Tastaturlicht aus), ruht er. Geht das
Panel ohne Abdunkeln aus, etwa mit Esc auf dem Sperrbildschirm, schaltet der
Dienst auch das Tastaturlicht aus und stellt es wieder her, sobald das Panel
zurückkommt, außer es wurde inzwischen anders geändert.

Benötigt `python3-dbus`, `python3-gi` und `iio-sensor-proxy`:

```bash
install -D -m 0755 bin/als-brightness ~/.local/bin/als-brightness
install -D -m 0644 systemd/als-brightness.service \
  ~/.config/systemd/user/als-brightness.service
systemctl --user daemon-reload
systemctl --user enable --now als-brightness.service
journalctl --user -u als-brightness -f
```

Ein Menüeintrag schaltet ebenfalls zwischen AUTO und manuell um, mit kurzer
Meldung:

```bash
install -D -m 0755 bin/als-brightness-toggle ~/.local/bin/als-brightness-toggle
install -D -m 0644 desktop/als-brightness-toggle.desktop \
  ~/.local/share/applications/als-brightness-toggle.desktop
```

Den Dienst abschalten mit
`systemctl --user disable --now als-brightness.service`.

Hinweis: Der KDE-Regler kann einen anderen Wert zeigen als die echte
Helligkeit, weil weder die Touch Bar noch dieser Dienst über KDE gehen.

## Schlafverbrauch auf Akku messen

`tools/battery-logger.py` liest `BAT0` alle 2 s und erkennt jede Schlafphase
am Sprung in CLOCK_BOOTTIME. Es endet eine wählbare Zeit nach dem letzten
Aufwachen und zeichnet so auch einen suspend-then-hibernate-Zyklus mit zwei
Phasen vollständig auf:

```bash
python3 -I tools/battery-logger.py schlaf.jsonl 900
```

Starten, 10 min warten, Deckel zu, nach dem Aufwachen das Gerät für den
Nachlauf in Ruhe lassen. Der Ladezähler springt auf diesem Gerät nach längerer
Ruhe (gesehen: +64 mAh in 3 min nach dem Wecken). Darum Fenster erst nach dem
Einschwingen vergleichen und mindestens 2 h messen.
