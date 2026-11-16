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
und der S0ix-Zähler blieb bei null. S3/deep und Hibernation waren auf dem
Testgerät beim Aufwachen unzuverlässig.

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

Globale Optionen wie `pcie_aspm=force`, erzwungenes NVMe-D3cold, S3/deep oder
Hibernation gehören nicht zu diesem Stand.

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
