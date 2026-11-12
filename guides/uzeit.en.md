# UZeit on the Touch Bar

[Deutsch](uzeit.de.md)

UZeit shows the current lesson or break and its remaining time. The Touch Bar
can display that countdown and launch the installed UZeit app when tapped.
[Web app and downloads](https://apps.bbs-einbeck.de/uzeit/) ·
[UZeit source code](https://github.com/mail896/bbs_unterrichtszeit_public)

Follow the [Touch Bar setup](setup.en.md) and install the UZeit Debian package.
Run this command from the root of the Touch Bar repository.

```sh
sudo install -D -m 0755 libexec/t1bridge-uzeit-status /usr/local/libexec/t1bridge-uzeit-status
systemctl --user edit t1-touchbar.service
```

Add these two variables to the service drop-in.

```ini
[Service]
Environment=T1BRIDGE_STATUS_COMMAND=/usr/local/libexec/t1bridge-uzeit-status
Environment=T1BRIDGE_STATUS_DESKTOP_FILE=/usr/share/applications/de.bbs-einbeck.unterrichtszeit.desktop
```

Restart the service in the graphical session.

```sh
systemctl --user restart t1-touchbar.service
```

The adapter reads the timetable from the locally installed UZeit package. When
the package or its calendar is unavailable, the renderer receives no countdown.
The project-page preview uses a sample lesson and countdown.
