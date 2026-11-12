# Driver source and licences

The driver patches apply to [T1Bridge v0.1.12][t1bridge] (commit
`81cbdf81026a16e02f0bea74735c6b029a8ffae2`) and to the Debian
[Linux source package 6.12.116-1][debian-linux]. Build scripts and configuration
examples in this directory use the [MIT licence](LICENSES/MIT.txt).

- The adapted `appletbdrm` driver retains Kerem Karabay’s copyright notice
  and its GPL-2.0-only licence. The unpark sequence is Nicolas Nistal’s
  [T1Bridge pull request #49][pr49], commit
  `26f6ef618811296e4e19ca210ef08ff4b4283f00`. The same patch includes the
  Debian 6.12 DRM/PRIME compatibility change.
- `t1_cfgsel` and `apple_t1_ncm` come from the pinned T1Bridge source and
  retain its MIT or GPL-2.0-only dual licensing.
- The UVC port uses Debian’s matching USB Video Class source and T1Bridge’s
  H.264 descriptor and iBridge power-management changes. Files retain their
  original GPL notices, including GPL-2.0-or-later for the H.264 parser.
- [GNU GPL version 2 text](LICENSES/GPL-2.0-only.txt)

To distribute locally built kernel binaries, make the exact corresponding
sources, patches and build scripts available under the applicable GPL terms.
The Debian userspace packages remain upstream downloads.

[t1bridge]: https://github.com/standardagents/t1bridge/tree/v0.1.12
[debian-linux]: https://snapshot.debian.org/package/linux/6.12.116-1/
[pr49]: https://github.com/standardagents/t1bridge/pull/49
