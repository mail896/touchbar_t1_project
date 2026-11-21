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
stayed at zero. S3/deep sleep was unreliable on resume on the test machine.
Hibernation works with the [hibernation](#hibernation) section below.

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

Global options such as `pcie_aspm=force`, forced NVMe D3cold or S3/deep sleep
are not part of this configuration.

## Thunderbolt controller power-off

macOS powers both Alpine Ridge controllers off while nothing is attached on
their side. The `mbp142_tb_poweroff` module does the same: on sleep entry it
signs both controllers off (Go2Sx), sets Link Disable and powers them off
through Apple's ACPI methods `SXFP`/`XRST`; on resume it restores them through
`TRPE`. With an external USB or Thunderbolt device attached, the controllers
stay on. If a check fails, the system sleeps normally; after a failed recovery
the module disables itself until reloaded.

With `THUNDERBOLT_AWAKE_POWEROFF=1`, `macbook-tb-awake` also powers a side off
while awake when nothing has been attached there for 40 s. With a controller
off, the USB-C ports route their USB 2 lines to the PCH, so USB devices work
immediately at USB 2 speed. The udev rule then powers the controller on and
cycles the PCH port once so the device re-enumerates at full speed. A USB-C
monitor without a USB device is not detected this way: `sudo macbook-tb-awake on`.

Battery measurement (1 h): 1.36 W instead of 2.56 W during sleep.

Awake and idle with the display off, both sides off draw 3.7 W on battery,
both sides on 7.2 W.

With `THUNDERBOLT_AWAKE_L1=1`, `macbook-tb-awake` enables ASPM L1 on the root
port and the Alpine Ridge bridge of every powered side (after plugging in, after
`on` and after each state change). The firmware leaves these links at "ASPM
Disabled", and Linux does not touch ASPM because the FADT declares no ASPM
support. Without L1 the CPU package stays in PC3 while a controller is on.
With both sides on: 5.5 W instead of 7.1 W, package 0.78 W instead of 2.17 W;
with a USB stick attached the package drops from about 1.9 W to 0.93 W, at the
same transfer rate. L1 is cleared again when an external Thunderbolt router or
display is present. Set the switch to 0 to clear it within 20 s.

## T1 sleep notification

macOS sends the T1 `ASOC.SOCW(0)` ("host sleeping") before every sleep and
`SOCW(1)` afterwards through the EC mailbox. Linux never did, so the T1 kept
running at full power during sleep. With `T1_SOCW=1` the hook sends both
through the `mbp142_t1_socw` module and restarts the T1Bridge services
afterwards. Measured (2 h on battery, together with the Thunderbolt power-off):
about 0.6–0.9 W during sleep, in the range of macOS on the same machine.

Build and install the modules (repeat after every kernel update):

```bash
for m in mbp142_tb_poweroff mbp142_t1_socw; do
  make -C /lib/modules/$(uname -r)/build M="$PWD/modules/$m" modules
  sudo install -D -m 0644 "modules/$m/$m.ko" \
    "/usr/local/lib/macbook/$(uname -r)/$m.ko"
done
sudo install -m 0755 bin/macbook-tb-poweroff bin/macbook-tb-awake /usr/local/sbin/
sudo install -m 0644 systemd/macbook-tb-poweroff.service \
  systemd/macbook-tb-awake.service /etc/systemd/system/
sudo install -m 0644 systemd/90-macbook-tb-awake.rules /etc/udev/rules.d/
sudo systemctl daemon-reload
sudo systemctl enable --now macbook-tb-poweroff.service macbook-tb-awake.service
```

The switches `THUNDERBOLT_POWEROFF`, `THUNDERBOLT_AWAKE_POWEROFF`,
`THUNDERBOLT_AWAKE_L1` and `T1_SOCW`
live in `/etc/macbook-s2idle-powersave.conf`.

## Keybag relay at boot

`t1bridge-keybag` failed once on every boot ("prepare shared keybags failed")
and only ran after systemd restarted it. The diagnostic log shows why: after a
boot the SEP has no device keybag. The relay creates and promotes it and then
queries its lock state at once. The first reply still reports "not unlocked"
(flags `0x14`, lock value 2); 100 ms later it is unlocked.
`keybag-lockstate.patch` (against T1Bridge v0.1.12) repeats only this read-only
query for up to 2 s. With the patch the relay succeeds on its first attempt
(`NRestarts=0`).

```bash
# in a T1Bridge v0.1.12 source tree; needs libsystemd-dev
patch -p1 < /path/to/touchbar_t1_project/keybag-lockstate.patch
cargo build --release --locked -p t1-daemons --features keybag-relay-service \
  --bin t1-keybag-relay
sudo install -D -m 0755 target/release/t1-keybag-relay \
  /usr/local/lib/macbook/t1-keybag-relay
sudo install -D -m 0644 /path/to/touchbar_t1_project/systemd/t1bridge-keybag-lockstate.conf \
  /etc/systemd/system/t1bridge-keybag.service.d/20-lockstate-fix.conf
sudo systemctl daemon-reload
```

To return to the packaged relay, delete the drop-in and run
`systemctl daemon-reload`.

## Hibernation

After S4 the Touch Bar and Touch ID stayed dark and Wi-Fi crashed. The
`macbook-hibernate` hook fixes both: before hibernation it unbinds Wi-Fi, puts
the device into D3hot and sends the T1 `SOCW(0)`; afterwards it rebinds Wi-Fi,
sends `SOCW(1)` and restarts the T1Bridge services. With
`HibernateMode=shutdown` the machine powers off completely after saving the
image. Measured: at most about 0.4 W during hibernation (1.3 W without these
steps).

Prerequisites are a swap file and `resume=`/`resume_offset=` on the kernel
command line:

```bash
sudo fallocate -l 10G /swapfile && sudo chmod 600 /swapfile
sudo mkswap /swapfile && sudo swapon --priority 10 /swapfile
echo '/swapfile none swap sw,pri=10 0 0' | sudo tee -a /etc/fstab
sudo filefrag -v /swapfile | awk 'NR==4 {print $4}'   # value for resume_offset
```

Then add `resume=UUID=<root file system UUID> resume_offset=<value>` to the boot
entry. An image built with `grub-mkstandalone` needs the `lvm` module preloaded
when root is on LVM and must not be restricted with `--install-modules`. Check
new boot images in QEMU/OVMF against the whole disk (read-only) first.

```bash
sudo install -m 0755 systemd/macbook-hibernate \
  /usr/lib/systemd/system-sleep/macbook-hibernate
sudo install -D -m 0644 systemd/60-macbook-suspend-then-hibernate.conf \
  /etc/systemd/sleep.conf.d/60-macbook-suspend-then-hibernate.conf
```

In KDE power settings for battery, set the lid action to "Sleep" and the sleep
mode to "Standby, then hibernate". After 60 min on battery the machine then
hibernates; on AC power it stays in sleep.

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

## Text boot without error lines

Apple's ACPI tables produce kernel errors on every boot (`AE_ALREADY_EXISTS`
for the CPU SSDTs, missing `\_SB.OSCP`), plus harmless Bluetooth, DMAR and SGX
messages. With `quiet loglevel=3` the kernel does not print them. They still
appeared in cyan: Plymouth 24 starts from the initrd even without `splash` (the
initramfs script defaults to `SPLASH=true`), reads the kernel log itself and
shows it regardless of `loglevel=3`.

For a text boot with systemd's `[  OK  ]` lines and no error lines, drop
`splash` from the kernel command line and add:

```
quiet loglevel=3 systemd.show_status=true plymouth.enable=0
```

The kernel then does the filtering. In rEFInd, `graphics on` in the
`menuentry` avoids the "Booting OS" text screen before GRUB.
