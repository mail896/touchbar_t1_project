# Touch Bar unter Debian 13 einrichten

[English](setup.en.md)

Der Hardwaretest lief auf einem **MacBookPro14,2** mit dem Debian-Kernel
`6.12.116+deb13-amd64`, Paketversion `6.12.116-1`.
Lass den funktionierenden Starteintrag verfügbar, während du den
T1Bridge-Eintrag ergänzt.

## Module bauen

Installiere Kernel und Header in der passenden Debian-Version. Falls der
normale Spiegel sie inzwischen ersetzt hat, findest du die genauen Quellen und
Binärpakete im [Debian-Snapshot][debian-linux]. Führe die Befehle mit
passenden `deb-src`-Einträgen im Hauptverzeichnis dieses Projekts aus.

```sh
sudo apt install git build-essential dpkg-dev patch kmod initramfs-tools \
  linux-image-6.12.116+deb13-amd64=6.12.116-1 \
  linux-headers-6.12.116+deb13-amd64=6.12.116-1

work=$(mktemp -d)
git clone --branch v0.1.12 --depth 1 \
  https://github.com/standardagents/t1bridge.git "$work/t1bridge"
(cd "$work" && apt-get source linux=6.12.116-1)

./kernel/build.sh "$work/t1bridge" "$work/linux-6.12.116" "$work/build"
./kernel/package.sh "$work/build" "$work"
```

Das Skript prüft den T1Bridge-Commit
`81cbdf81026a16e02f0bea74735c6b029a8ffae2`, die Debian-Quellversion und die
Modulkennung `vermagic`. Für die Anzeige sind `t1_cfgsel` und `appletbdrm`
erforderlich. Das lokale Paket enthält außerdem das private T1-Netzwerk und
den Kameratreiber aus dem geprüften Aufbau. Seine Installationsskripte
aktualisieren den Modulindex des angegebenen Kernels.
[Herkunft und Lizenzen der Patches](../kernel/NOTICE.md)

## T1Bridge-Dienste installieren

Lade `t1bridge-0.1.12-debian-13-packages.tar.gz` aus dem
[Debian-Port der Community, Release v2.0.1][deb-port], nach `$work`.
Prüfe das Archiv und das hier benötigte Paket.

```sh
(
  cd "$work"
  printf '%s  %s\n' \
    4f0b50c24371375e682505ab6e8a0a843a8656a2c974cd581de0cb442b0eb59e \
    t1bridge-0.1.12-debian-13-packages.tar.gz | sha256sum -c -
  tar -xf t1bridge-0.1.12-debian-13-packages.tar.gz
  cd t1bridge-0.1.12-debian-13/debian-13
  printf '%s  %s\n' \
    9b184c8a8217115f41164c305ef19bdff2dc55bd2b12fbfd94601d93743b8277 \
    t1bridge_0.1.12-2~debian13_amd64.deb | sha256sum -c -
)

sudo apt install --no-install-recommends \
  "$work/macbook-t1-modules-6.12.116_0.1.12-2~debian13+mbp612.1_amd64.deb" \
  "$work/t1bridge-0.1.12-debian-13/debian-13/t1bridge_0.1.12-2~debian13_amd64.deb"
sudo usermod -aG t1bridge "$(id -un)"
```

Führe den Gruppenbefehl als normaler Desktop-Benutzer aus. Die Mitgliedschaft
gilt ab der nächsten Anmeldung. Das lokale Paket erfüllt die genaue
Modulabhängigkeit von T1Bridge. Dessen Installationsskript aktiviert die
Standarddienste und liefert die DRM-Regel sowie die iBridge-Ausnahme für
`usbmuxd`. Falls bereits eine eigene
`/etc/udev/rules.d/39-usbmuxd.rules` existiert, prüfe ihre iBridge-Regel
vor dem Neustart. Das Installationsskript des Debian-Archivs ist für dessen
eigenes DKMS-Paket vorgesehen.

Touch ID, die Fingerabdruckpakete und die PAM-Einrichtung sind optionale
T1Bridge-Funktionen außerhalb dieser Anleitung. Die Anzeige funktioniert mit
dem Standard-Renderer und der vorhandenen Passwortkonfiguration.

## Separaten Starteintrag vorbereiten

Die iBridge braucht USB-Konfiguration 2, bevor die normalen USB-Schnittstellen
gebunden werden. Erzeuge mit dem beiliegenden Hook und Startskript ein eigenes
Initramfs. Wenn dein Kernel vertrauenswürdige Modulsignaturen verlangt, signiere
die lokalen Module vor diesem Schritt.

```sh
release=6.12.116+deb13-amd64
sudo install -m 0644 kernel/examples/90-macbook-t1bridge-select.conf \
  /etc/modprobe.d/90-macbook-t1bridge-select.conf

mkdir "$work/initramfs"
cp -a /etc/initramfs-tools/. "$work/initramfs/"
install -D -m 0755 kernel/initramfs/hooks/macbook-t1bridge \
  "$work/initramfs/hooks/macbook-t1bridge"
install -D -m 0755 kernel/initramfs/scripts/init-top/macbook-t1bridge \
  "$work/initramfs/scripts/init-top/macbook-t1bridge"
sudo mkinitramfs -d "$work/initramfs" \
  -o "$work/initrd.img-$release-macbook-t1" "$release"
sudo lsinitramfs "$work/initrd.img-$release-macbook-t1" |
  grep -E 'init-top/macbook-t1bridge|/t1_cfgsel\.ko'
```

Ergänze im vorhandenen Bootloader einen eigenen Eintrag mit dem erzeugten
Initramfs, dem passenden Kernel und den bisherigen Parametern für das
Root-Dateisystem. Ergänze die geprüften Optionen auf der Kernel-Kommandozeile
dieses Eintrags:

```text
macbook-t1bridge=1 module_blacklist=apple_ibridge,apple_ib_tb,apple_ib_als
```

Übernimm das Root-Argument aus dem vorhandenen Eintrag. Bei
`root=UUID=ROOT_FILESYSTEM_UUID` steht der Platzhalter für die UUID
des Debian-Root-Dateisystems. Die Bootloader-Pfade hängen von der
Installation ab. Das erzeugte Abbild liegt zur Prüfung in `$work`.

Wenn ein älterer Touch-Bar-Dienst `apple_ibridge` oder `apple_ib_tb` lädt,
beschränke ihn auf den Rückfalleintrag. Das
[Beispiel für ein systemd-Drop-in](../kernel/examples/90-macbook-t1bridge-legacy-service.conf)
zeigt die passende Bedingung. Die
[optionale iBridge-Weckregel](../kernel/examples/91-macbook-t1bridge-wake.rules)
setzt die Stromspar- und Weckoptionen für USB-ID `05ac:8600`.

Prüfe nach dem Start `uname -r` sowie in der grafischen Sitzung
`modprobe -n -v appletbdrm`, `ls -l /dev/dri/touchbar` und
`systemctl --user status t1-touchbar.service`. Prüfe die Tasten zuerst
mit dem Standard-Renderer.

## Optionaler KDE-Renderer

Baue ihn im Projektverzeichnis mit Rust/Cargo ab Version 1.88 und demselben
T1Bridge-Quellstand. Der KDE-Provider nutzt Plasma 6, PipeWire/WirePlumber
und den D-Bus der Desktop-Sitzung. Für Medientasten ist `playerctl`
erforderlich. Für die optionale akustische Rückmeldung installiere
`pulseaudio-utils`. KDE stellt `qdbus6` und `kreadconfig6` bereit.

```sh
./stage.sh "$work/t1bridge" "$work/renderer"
sudo install -m 0755 "$work/renderer/bin/t1-touchbar-kde" \
  "$work/renderer/bin/t1bridge-kde-provider" /usr/local/bin/

config=${XDG_CONFIG_HOME:-"$HOME/.config"}
mkdir -p "$config/t1bridge" "$config/systemd/user/t1-touchbar.service.d"
ln -s /usr/local/bin/t1-touchbar-kde "$config/t1bridge/renderer"
install -m 0644 "$work/renderer/systemd/10-kde-provider.conf" \
  "$work/renderer/systemd/20-graphical-session.conf" \
  "$config/systemd/user/t1-touchbar.service.d/"
sudo systemctl --global disable t1-touchbar.service
sudo systemctl --global add-wants graphical-session.target t1-touchbar.service
systemctl --user daemon-reload
systemctl --user restart t1-touchbar.service
```

Sichere einen eventuell vorhandenen Renderer-Link oder dessen Datei, bevor du
ihn ersetzt. Die beiden globalen Befehle verschieben den automatischen
Start für alle Benutzer in die grafische Sitzung. Dadurch erhält der
Renderer die KDE-Anzeigeumgebung. Für die Rückkehr zum Standard-Renderer
entferne den Link und beide Drop-ins. Für den Start wie im Paket vorgesehen
führe den globalen Disable-Befehl erneut aus und danach
`sudo systemctl --global enable t1-touchbar.service`. Lade anschließend
den Benutzerdienst neu und starte ihn erneut. Der ursprüngliche Starteintrag
führt zurück zur bisherigen Kernel- und USB-Startkonfiguration.

### Optionale Verknüpfungen und Statusanzeige

Öffne mit `systemctl --user edit t1-touchbar.service` das Drop-in des
Benutzerdienstes. Ohne gesetzte Variablen sind die optionalen Tasten verborgen.

```ini
[Service]
Environment=T1BRIDGE_SHORTCUT_1=https://www.debian.org/
# Environment=T1BRIDGE_SHORTCUT_2=https://kde.org/
# Environment=T1BRIDGE_STATUS_COMMAND=/usr/local/bin/touchbar-status
# Environment=T1BRIDGE_STATUS_DESKTOP_FILE=/usr/share/applications/example.desktop
```

Die Verknüpfungen akzeptieren HTTP und HTTPS. Der Statusbefehl muss unter
einem absoluten Pfad ausführbar sein. Der Renderer ruft ihn mit den beiden
Argumenten `v1 lesson-status` auf und liest eine Zeile wie `class 2 420`
für Stunde 2 mit 420 Sekunden Restzeit, `break 2 120`, `before 0 60`,
`after 0 -` oder `free 0 -`. Für den Start einer Anwendung beim Antippen
setzt du den absoluten Pfad zu ihrer Desktop-Datei. Starte anschließend den
Benutzerdienst neu.

Mit `t1-touchbar-kde --preview main touchbar.ppm` erzeugst du ein Bild aus
dem Renderer mit Beispielwerten. Das geht auch auf Rechnern ohne Touch Bar.
Verfügbar sind `main`, `tools`, `keyboard`, `volume` und `function`.

[debian-linux]: https://snapshot.debian.org/package/linux/6.12.116-1/
[deb-port]: https://github.com/vfontanela/macbookpro14-linux-support/releases/tag/v2.0.1
