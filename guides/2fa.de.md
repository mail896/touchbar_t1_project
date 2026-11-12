# 2FA auf der Touch Bar

[English](2fa.en.md)

Tippe auf **2FA**, um einen sechsstelligen TOTP kurz anzuzeigen und
einfügen zu können. Der optionale Helfer verwendet einen Eintrag aus deinem
lokalen Schlüsselbund. Er läuft in deiner entsperrten KDE-Plasma-Sitzung
unter Wayland mit einem Secret-Service-Anbieter wie KWallet.
[TOTP-Spezifikation](https://datatracker.ietf.org/doc/html/rfc6238)

## Konto auswählen

Richte den [KDE-Renderer](setup.de.md#optionaler-kde-renderer) ein und führe
dann diese Befehle im Projektverzeichnis aus.

```sh
sudo apt install python3-pyqt5 libsecret-tools spectacle zbar-tools
sudo install -D -m 0755 libexec/t1bridge-totp /usr/local/libexec/t1bridge-totp
sudo install -D -m 0644 libexec/t1bridge_clipboard.py /usr/local/libexec/t1bridge_clipboard.py
```

Zeige den Einrichtungs-QR-Code des gewünschten Kontos in deiner
Authenticator-App oder in den 2FA-Einstellungen des Dienstes an. Starte den
Helfer als normaler Benutzer in einem Terminal derselben Desktop-Sitzung.
Markiere nur diesen QR-Code in Spectacle und bestätige den angezeigten
Kontonamen mit `y`. Die Aufnahme liegt während der Auswahl in einem
temporären Ordner im privaten Laufzeitverzeichnis. Der Helfer speichert die
TOTP-Parameter über Secret Service in deinem Schlüsselbund.

```sh
/usr/local/libexec/t1bridge-totp setup
```

Ergänze mit `systemctl --user edit t1-touchbar.service` diese Einstellung.

```ini
[Service]
Environment=T1BRIDGE_TOTP_HELPER=/usr/local/libexec/t1bridge-totp
```

Starte den Renderer mit `systemctl --user restart t1-touchbar.service` neu.
Falls du einen eigenen Namen für den Schlüsselbundeintrag möchtest, setze
`T1BRIDGE_TOTP_ACCOUNT` beim Einrichten und im Dienst auf denselben Wert.
Der voreingestellte Name lautet `default`.

## Zwischenablage und Deaktivierung

Der Helfer gibt die Zwischenablage beim Ablauf des Codes, spätestens nach
15 Sekunden, beim Sperren des Desktops oder bei einem Wechsel des Inhalts frei.
Desktop-Anwendungen können den Code lesen, solange er zum Einfügen bereitsteht.
Der Einrichtungs-QR-Code enthält den langfristigen 2FA-Schlüssel des Kontos.
Bewahre ihn lokal auf.

Entferne zum Deaktivieren der Taste `T1BRIDGE_TOTP_HELPER` aus der
Dienstkonfiguration und starte den Dienst neu. Falls du auch den Eintrag im
Schlüsselbund entfernen möchtest, führe den folgenden Befehl in der
Desktop-Sitzung mit dem zugehörigen Namen aus.

```sh
secret-tool clear application touchbar-t1-project account default
```
