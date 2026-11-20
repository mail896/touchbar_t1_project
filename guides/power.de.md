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
sudo install -D -m 0644 systemd/t1bridge-keybag-after-import.conf \
  /etc/systemd/system/t1bridge-keybag.service.d/10-after-import.conf
sudo systemctl daemon-reload
sudo systemctl enable --now macbook-tb-poweroff.service macbook-tb-awake.service
```

Die Schalter `THUNDERBOLT_POWEROFF`, `THUNDERBOLT_AWAKE_POWEROFF` und `T1_SOCW`
stehen in `/etc/macbook-s2idle-powersave.conf`. Der Keybag-Drop-in sorgt
dafür, dass `t1bridge-keybag` erst nach `t1bridge-import` startet. Beide
sprechen über USB mit dem SEP der T1, und ohne Reihenfolge scheiterte der erste
Start.

## Ruhezustand (Hibernate)

Nach S4 blieben Touch Bar und Touch ID dunkel, und das WLAN stürzte ab. Der
Hook `macbook-hibernate` behebt beides: Vor dem Ruhezustand meldet er WLAN ab,
setzt das Gerät auf D3hot und schickt der T1 `SOCW(0)`. Danach holt er WLAN
zurück, schickt `SOCW(1)` und startet die T1Bridge-Dienste neu. Mit
`HibernateMode=shutdown` schaltet der Rechner nach dem Speichern ganz aus.
Gemessen: höchstens etwa 0,4 W im Ruhezustand (vorher 1,3 W ohne diese
Schritte).

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
