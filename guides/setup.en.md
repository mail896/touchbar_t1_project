# Touch Bar setup on Debian 13

[Deutsch](setup.de.md)

The hardware test used a **MacBookPro14,2** and Debian's
`6.12.116+deb13-amd64` kernel, package version `6.12.116-1`.
Keep a working boot entry available while adding the T1Bridge entry.

## Build the modules

Install the matching kernel and headers from Debian. The
[Debian Snapshot entry][debian-linux] lists the exact source package and build
artifacts if the running mirror has moved on. With matching `deb-src` entries
enabled, use the following commands from this project's root.

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

The build verifies T1Bridge commit
`81cbdf81026a16e02f0bea74735c6b029a8ffae2`, the Debian source version and module
`vermagic`. The display requires `t1_cfgsel` and `appletbdrm`. The local
package also builds and includes the private T1 network and the camera override
from the tested configuration. Its maintainer scripts regenerate the exact
kernel's module index. [Patch provenance and licences](../kernel/NOTICE.md)

## Install the T1Bridge userspace

Download `t1bridge-0.1.12-debian-13-packages.tar.gz` from the
[community Debian port release v2.0.1][deb-port] into `$work`. Verify the
archive and the one userspace package used here.

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

Run the group command from the regular desktop account. A new login picks up
the group membership. The local package satisfies the exact module-package
dependency of the userspace. The userspace postinstall enables the stock
system and user services and provides the DRM seat rule and the `usbmuxd`
iBridge exclusion. If an administrator already manages
`/etc/udev/rules.d/39-usbmuxd.rules`, review that file for the iBridge rule
before rebooting. The Debian bundle's installer targets its own DKMS package.

Touch ID, fingerprint packages and PAM integration are optional T1Bridge
functions outside this setup. The display works with the stock renderer and
the existing password configuration.

## Prepare a separate boot entry

The iBridge must enter configuration 2 before the ordinary USB interfaces
bind. Use the supplied initramfs hook and early script to make a separate
image. If the running kernel requires trusted module signatures, sign the
local modules with a trusted key before building the image.

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

Add a dedicated entry in the existing bootloader using the generated initramfs,
the matching kernel and the current root filesystem parameters. Append the
tested options to that entry's kernel command line:

```text
macbook-t1bridge=1 module_blacklist=apple_ibridge,apple_ib_tb,apple_ib_als
```

Keep the root argument from your existing entry. For example,
`root=UUID=ROOT_FILESYSTEM_UUID` refers to the filesystem that contains
Debian's root, using its actual UUID in place of the placeholder.
Bootloader paths depend on the installation. The generated image is in
`$work` for inspection before installing it for boot.

If an older Touch Bar service already loads `apple_ibridge` or `apple_ib_tb`,
restrict that service to the fallback entry. The
[example drop-in](../kernel/examples/90-macbook-t1bridge-legacy-service.conf)
does this for a systemd service. The
[optional iBridge wake rule](../kernel/examples/91-macbook-t1bridge-wake.rules)
sets the power and wake policy for USB ID `05ac:8600` only.

After choosing the new entry, verify `uname -r`, then run
`modprobe -n -v appletbdrm`, `ls -l /dev/dri/touchbar`, and
`systemctl --user status t1-touchbar.service` from the graphical session.
The stock renderer should show the controls before adding a custom renderer.

## Optional KDE renderer

Build from the project root with Rust/Cargo 1.88 or later, ImageMagick and the
same pinned T1Bridge checkout. The KDE provider uses KDE Plasma 6, PipeWire/WirePlumber
and the desktop's D-Bus session. Install `playerctl` for media controls and
`pulseaudio-utils` for the optional sound feedback. KDE provides
`qdbus6` and `kreadconfig6`.

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

If a renderer is already configured, save its link or file before replacing
it. The two global commands move automatic startup to the graphical
session for all users, so the renderer has the KDE display environment.
To return to the stock renderer, remove the renderer link and both drop-ins.
To restore the package's default startup, run the global disable command
again and then `sudo systemctl --global enable t1-touchbar.service`.
Reload and restart the user service. Use the original boot entry to return
to the previous kernel and early USB setup.

### Optional shortcuts and status

Run `systemctl --user edit t1-touchbar.service` and add the variables you
want to use. The optional controls are hidden when their variables are unset.

```ini
[Service]
Environment=T1BRIDGE_SHORTCUT_1=https://www.debian.org/
# Environment=T1BRIDGE_SHORTCUT_2=https://kde.org/
# Environment=T1BRIDGE_STATUS_COMMAND=/usr/local/bin/touchbar-status
# Environment=T1BRIDGE_STATUS_DESKTOP_FILE=/usr/share/applications/example.desktop
```

The URL shortcuts accept HTTP and HTTPS. The status command must be an
executable at an absolute path. The renderer calls it with the two arguments
`v1 lesson-status`. It reads one line, such as `class 2 420` for session 2
with 420 seconds left, `break 2 120`, `before 0 60`, `after 0 -` or
`free 0 -`. Set the absolute desktop-file path if tapping the status should
launch an application. Restart the user service after editing the drop-in.

To use the two logos shown on the project site, set these variables in place
of the example shortcuts.

```ini
[Service]
Environment=T1BRIDGE_BRANDED_SHORTCUTS=1
Environment=T1BRIDGE_SHORTCUT_1=https://bbs-einbeck.de
Environment=T1BRIDGE_SHORTCUT_2=https://github.com/mail896/
```

The optional [UZeit adapter](uzeit.en.md) and [2FA helper](2fa.en.md) have
their own setup instructions.

Generate a screenshot of the renderer with sample values on a machine without
a Touch Bar using `t1-touchbar-kde --preview main touchbar.ppm`. Available
views include `main`, `tools`, `keyboard`, `volume` and `function`.
Use `branded`, `branded-tools` or `branded-code` to see the logos,
the example lesson status and the 2FA placeholder.

[debian-linux]: https://snapshot.debian.org/package/linux/6.12.116-1/
[deb-port]: https://github.com/vfontanela/macbookpro14-linux-support/releases/tag/v2.0.1
