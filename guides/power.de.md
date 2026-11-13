# Energie und Touchpad auf dem MacBookPro14,2

Diese optionale Konfiguration ist für das **MacBookPro14,2** mit Debian 13,
KDE Plasma und Kernel `6.12.116+deb13-amd64` geprüft. Sie ist nicht als
allgemeine MacBook-Konfiguration gedacht.

## s2idle-Energiesparen

Der Hook nutzt ausschließlich `s2idle`. Vor dem Schlafen schaltet er die
Tastaturbeleuchtung aus, parkt WLAN sowie unbenutzte externe
USB-/Thunderbolt-Controller und stellt sie nach dem Aufwachen wieder her. Der
interne T1-/iBridge-Pfad für Touch Bar und Touch ID bleibt aktiv.

Ist an einem der verwalteten Controller ein externes USB-Gerät angeschlossen,
werden die Controller nicht geparkt. Dadurch bleibt das Gerät zuverlässig
erreichbar, die Einsparung fällt aber kleiner aus. WLAN wird unabhängig davon
wiederhergestellt.

Auf dem geprüften Gerät sank die Leistungsaufnahme im Schlaf gegenüber dem
ursprünglichen Zustand deutlich. Der genaue Wert hängt von angeschlossenen
Geräten, Funkverkehr und Akkuzustand ab. Die Hardware meldet unter Linux kein
brauchbares S0ix und bleibt beim Prozessorpaket in C3; S3/deep und Hibernation
waren nicht stabil und sind deshalb ausdrücklich nicht Bestandteil dieser
Konfiguration.

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
