# Touch Bar T1 for Debian and KDE

[English](#english) · [Deutsch](#deutsch) · [Project site](https://mail896.github.io/touchbar_t1_project/)

![Custom T1 Touch Bar renderer with BBS Einbeck and LaberPapa logos](docs/assets/touchbar-branded.png)
![Tools with the UZeit lesson countdown, rendered with sample values](docs/assets/touchbar-branded-tools.png)

## English

A KDE Plasma renderer and desktop-controls provider for the Apple T1 Touch Bar. Tested on **MacBookPro14,2** with **Debian 13**, **Plasma 6.3.6 on Wayland**, and **6.12.116+deb13-amd64**. The renderer uses [T1Bridge v0.1.12](https://github.com/standardagents/t1bridge/tree/v0.1.12) for the device connection.

- Adjust display brightness, keyboard backlight and volume with touch sliders and KDE on-screen feedback; see the actual charge relative to the battery's learned full capacity inside a colour-coded icon, then tap it to open KDE Power Management.
- Use media and microphone controls, an Fn layer, a terminal shortcut, and CPU status.
- Add logo shortcuts to BBS Einbeck and LaberPapa, show the current UZeit lesson and time remaining, or copy a one-time code from the local keyring.

[UZeit web app](https://apps.bbs-einbeck.de/uzeit/) · [UZeit source](https://github.com/mail896/bbs_unterrichtszeit_public) · [UZeit setup](guides/uzeit.en.md) · [2FA setup](guides/2fa.en.md)

[Setup and configuration](guides/setup.en.md) · [Power and touchpad](guides/power.en.md) · [Kernel compatibility sources](kernel/) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The [project site](https://mail896.github.io/touchbar_t1_project/) shows the renderer, the tested Debian session, and the optional rEFInd layout. See [LICENSE](LICENSE) and the notices for component licenses.

## Deutsch

Ein Touch-Bar-Renderer und KDE-Bedienhelfer für Apple-MacBooks mit T1-Chip. Getestet auf **MacBookPro14,2** mit **Debian 13**, **Plasma 6.3.6 unter Wayland** und **6.12.116+deb13-amd64**. Den Gerätezugriff übernimmt [T1Bridge v0.1.12](https://github.com/standardagents/t1bridge/tree/v0.1.12).

- Bildschirmhelligkeit, Tastaturbeleuchtung und Lautstärke über Schieberegler mit KDE-Bildschirmanzeige ändern; die tatsächliche Ladung relativ zur angelernten Maximalkapazität samt Netzteil-Blitz im farbigen Batteriesymbol sehen und per Tipp die KDE-Energieverwaltung öffnen.
- Medientasten, Mikrofon-Stummschaltung, Fn-Tasten, Terminal und CPU-Status verwenden.
- Über die Logos BBS Einbeck und LaberPapa öffnen, mit UZeit die laufende Stunde und Restzeit anzeigen oder einen Einmalcode aus dem lokalen Schlüsselbund kopieren.

[UZeit-Web-App](https://apps.bbs-einbeck.de/uzeit/) · [UZeit-Quellcode](https://github.com/mail896/bbs_unterrichtszeit_public) · [UZeit einrichten](guides/uzeit.de.md) · [2FA einrichten](guides/2fa.de.md)

[Einrichtung und Konfiguration](guides/setup.de.md) · [Energie und Touchpad](guides/power.de.md) · [Kernel-Quellen zur Kompatibilität](kernel/) · [Drittanbieterhinweise](THIRD_PARTY_NOTICES.md)

Die [Projektseite](https://mail896.github.io/touchbar_t1_project/de.html) zeigt den Renderer, die getestete Debian-Sitzung und das optionale rEFInd-Menü. Die Komponentenlizenzen stehen in [LICENSE](LICENSE) und den Drittanbieterhinweisen.
