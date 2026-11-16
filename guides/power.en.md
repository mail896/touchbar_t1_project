# Power and touchpad setup for MacBookPro14,2

This optional configuration was tested on **MacBookPro14,2** with Debian 13,
KDE Plasma and kernel `6.12.116+deb13-amd64`. It is not intended as a generic
MacBook configuration.

## s2idle power saving

The hook applies to `s2idle`. It turns off the keyboard backlight before
sleep and restores it on resume. With `MODE=wifi` and `WIFI_KEEP_BOUND=1`,
the Wi-Fi driver stays bound if `brcmfmac` has `mbp142_s2idle_keep_d0` enabled.
The firmware can then sleep while the PCI device stays in D0. If the option
or D0 protection is disabled, the hook unbinds Wi-Fi before sleep and
rebinds it on resume. In Wi-Fi mode the USB-C and Thunderbolt drivers stay
bound, so their controllers can enter D3hot. The internal T1/iBridge path
for Touch Bar and Touch ID remains available.

On one MacBookPro14,2, battery tests with the drivers bound measured lower
sleep power in both comparisons (1.85 vs. 2.06 W and 4.17 vs. 4.80 W).
The runs varied in length. The CPU package stayed at C3 and the S0ix counter
stayed at zero. S3/deep sleep and hibernation were unreliable on resume on
the test machine.

With `THUNDERBOLT_L1=1`, the hook enables ASPM L1 on both internal
Thunderbolt links and restores the original Link Control settings after
resume. It skips the change when an external Thunderbolt router or display
is connected. A USB-C drive remained readable after resume on both branches.
The two-hour battery test averaged 2.93 W and logged 96.1% package C10.
Set `THUNDERBOLT_L1=0` to disable L1 independently.

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
