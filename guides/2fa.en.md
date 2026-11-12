# 2FA on the Touch Bar

[Deutsch](2fa.de.md)

Tap **2FA** to show a six-digit TOTP briefly and make it available to paste.
The optional helper uses one account from your local keyring. It runs as your
user in an unlocked KDE Plasma Wayland session with a Secret Service provider
such as KWallet. [TOTP specification](https://datatracker.ietf.org/doc/html/rfc6238)

## Select an account

Set up the [KDE renderer](setup.en.md#optional-kde-renderer), then run these
commands from the project directory.

```sh
sudo apt install python3-pyqt5 libsecret-tools spectacle zbar-tools
sudo install -D -m 0755 libexec/t1bridge-totp /usr/local/libexec/t1bridge-totp
sudo install -D -m 0644 libexec/t1bridge_clipboard.py /usr/local/libexec/t1bridge_clipboard.py
```

Display the setup QR for the chosen account in your authenticator or on the
service's 2FA setup screen. In a terminal within the same desktop session,
run the helper as your regular user. Select only that QR in Spectacle and
confirm the account label. The selection stays in a temporary directory in
your private runtime folder. The helper stores the TOTP parameters in your
keyring through Secret Service.

```sh
/usr/local/libexec/t1bridge-totp setup
```

Use `systemctl --user edit t1-touchbar.service` to add this setting.

```ini
[Service]
Environment=T1BRIDGE_TOTP_HELPER=/usr/local/libexec/t1bridge-totp
```

Restart the renderer with `systemctl --user restart t1-touchbar.service`.
For a different keyring alias, use the same `T1BRIDGE_TOTP_ACCOUNT` value in
the setup command's environment and in the service drop-in. The default alias
is `default`.

## Clipboard and removal

The helper releases its clipboard selection on expiry, after at most
15 seconds, or when the desktop locks or the selection changes. Desktop apps
can read the code while it is available to paste. The setup QR contains the
account's long-term 2FA secret. Keep it local.

To disable the button, remove `T1BRIDGE_TOTP_HELPER` from the drop-in and
restart the service. To delete the keyring entry too, run the following
command from your desktop session with the corresponding alias.

```sh
secret-tool clear application touchbar-t1-project account default
```
