# Power and touchpad setup for MacBookPro14,2

This optional configuration was tested on **MacBookPro14,2** with Debian 13,
KDE Plasma and kernel `6.12.116+deb13-amd64`. It is not intended as a generic
MacBook configuration.

## s2idle power saving

The hook only handles `s2idle`. Before sleep it turns off the keyboard
backlight and parks Wi-Fi plus unused external USB/Thunderbolt controllers. It
restores them after resume. The internal T1/iBridge path used by Touch Bar and
Touch ID remains active.

If an external USB device is attached to a managed controller, controller
parking is skipped. This keeps the device reliable at the cost of some power
savings. Wi-Fi restoration still runs.

On the tested machine, sleep power consumption dropped significantly compared
with the original configuration. Exact results depend on attached devices,
radio traffic and battery condition. The hardware does not expose usable S0ix
under Linux and the CPU package remains in C3. S3/deep sleep and hibernation
were not reliable and are deliberately excluded from this setup.

Install the files:

```bash
sudo install -m 0755 systemd/macbook-s2idle-powersave \
  /usr/lib/systemd/system-sleep/macbook-s2idle-powersave
sudo install -m 0644 systemd/macbook-s2idle-powersave.conf \
  /etc/macbook-s2idle-powersave.conf
sudo install -m 0755 bin/macbook-s2idle-recover \
  bin/macbook-s2idle-powersave-control /usr/local/sbin/
```

The hook applies to the next suspend; no reboot is required.

```bash
sudo macbook-s2idle-powersave-control status
sudo macbook-s2idle-powersave-control disable
sudo macbook-s2idle-powersave-control enable
```

If a managed controller is missing after resume:

```bash
sudo macbook-s2idle-recover
```

Global options such as `pcie_aspm=force`, forced NVMe D3cold, S3/deep sleep or
hibernation are not part of this configuration.

## Touchpad while typing

The Apple SPI keyboard in this model identifies itself as `0000:0000` under
Linux. As a result, libinput may not associate it with the internal touchpad,
making “disable touchpad while typing” unreliable. The supplied quirk only
fixes this association; it does not change the touchpad palm threshold.

If `/etc/libinput/local-overrides.quirks` already exists, merge the project
section instead of overwriting the file. Otherwise:

```bash
sudo install -D -m 0644 libinput/local-overrides.quirks \
  /etc/libinput/local-overrides.quirks
```

Log out and back in once. KDE's “disable touchpad while typing” option must be
enabled.
