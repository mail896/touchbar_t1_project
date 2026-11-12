# UZeit auf der Touch Bar

[English](uzeit.en.md)

UZeit zeigt die aktuelle Unterrichtsstunde oder Pause und ihre Restzeit.
Die Touch Bar kann den Countdown anzeigen und beim Antippen die installierte
UZeit-App öffnen. [Web-App und Downloads](https://apps.bbs-einbeck.de/uzeit/) ·
[UZeit-Quellcode](https://github.com/mail896/bbs_unterrichtszeit_public)

Richte zuerst die [Touch Bar](setup.de.md) ein und installiere das
UZeit-Debian-Paket. Führe den folgenden Befehl im Hauptverzeichnis des
Touch-Bar-Repositories aus.

```sh
sudo install -D -m 0755 libexec/t1bridge-uzeit-status /usr/local/libexec/t1bridge-uzeit-status
systemctl --user edit t1-touchbar.service
```

Ergänze diese beiden Variablen im Drop-in des Benutzerdienstes.

```ini
[Service]
Environment=T1BRIDGE_STATUS_COMMAND=/usr/local/libexec/t1bridge-uzeit-status
Environment=T1BRIDGE_STATUS_DESKTOP_FILE=/usr/share/applications/de.bbs-einbeck.unterrichtszeit.desktop
```

Starte den Dienst in der grafischen Sitzung neu.

```sh
systemctl --user restart t1-touchbar.service
```

Der Adapter liest den Stundenplan aus dem lokal installierten UZeit-Paket.
Fehlt das Paket oder sein Kalender, erhält der Renderer keinen Countdown.
Die Vorschau auf der Projektseite zeigt eine Beispielstunde mit Restzeit.
