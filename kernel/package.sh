#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 BUILD_DIRECTORY OUTPUT_DIRECTORY" >&2
    exit 2
fi
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(realpath -- "$1")
output=$(realpath -- "$2")
release=6.12.116+deb13-amd64
package=macbook-t1-modules-6.12.116
version=0.1.12-2~debian13+mbp612.1
staging=$(mktemp -d)
trap 'rm -rf -- "$staging"' EXIT
chmod 0755 "$staging"

target="$staging/usr/lib/modules/$release/updates/t1bridge"
docs="$staging/usr/share/doc/$package"
install -d "$target" "$docs" "$staging/DEBIAN"
for name in t1_cfgsel appletbdrm apple_t1_ncm uvcvideo; do
    module="$build_dir/modules/$name.ko"
    vermagic=$(modinfo -F vermagic "$module")
    test "${vermagic%% *}" = "$release"
    install -m 0644 "$module" "$target/"
done
install -m 0644 "$here/LICENSES/"*.txt "$here/NOTICE.md" "$docs/"
cat > "$staging/DEBIAN/control" <<'CONTROL'
Package: macbook-t1-modules-6.12.116
Version: 0.1.12-2~debian13+mbp612.1
Architecture: amd64
Section: kernel
Priority: optional
Maintainer: Local Debian administrator <root@localhost>
Depends: linux-image-6.12.116+deb13-amd64 (= 6.12.116-1), kmod
Provides: t1bridge-dkms (= 0.1.12-2~debian13)
Conflicts: t1bridge-dkms
Description: T1Bridge modules for Debian Linux 6.12.116 amd64
 Exact-kernel Apple iBridge configuration, Touch Bar, private network and
 UVC camera modules for the MacBook Pro with Apple T1.
CONTROL
cat > "$staging/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -eu
case "$1" in
    configure) depmod -a 6.12.116+deb13-amd64 ;;
esac
POSTINST
cat > "$staging/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -eu
case "$1" in
    remove|purge|abort-install|abort-upgrade)
        if command -v depmod >/dev/null 2>&1 &&
           [ -d /usr/lib/modules/6.12.116+deb13-amd64/kernel ]; then
            depmod -a 6.12.116+deb13-amd64
        fi
        ;;
esac
POSTRM
chmod 0755 "$staging/DEBIAN/postinst" "$staging/DEBIAN/postrm"

destination="$output/${package}_${version}_amd64.deb"
test ! -e "$destination"
dpkg-deb --root-owner-group --build "$staging" "$destination"
