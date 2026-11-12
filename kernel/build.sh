#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 T1BRIDGE_GIT DEBIAN_LINUX_SOURCE NEW_BUILD_DIRECTORY" >&2
    exit 2
fi

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
upstream=$(realpath -- "$1")
linux_source=$(realpath -- "$2")
build_dir=$(realpath -m -- "$3")
commit=81cbdf81026a16e02f0bea74735c6b029a8ffae2
release=6.12.116+deb13-amd64
headers=/lib/modules/$release/build

if [ ! -d "$headers" ] ||
   [ "$(dpkg-query -W -f='${Version}' "linux-headers-$release")" != 6.12.116-1 ]; then
    echo "Install the matching Debian 6.12.116-1 headers first." >&2
    exit 1
fi
if [ "$(git -C "$upstream" rev-parse 'v0.1.12^{commit}')" != "$commit" ]; then
    echo "Expected the T1Bridge v0.1.12 source tag." >&2
    exit 1
fi
if [ "$(dpkg-parsechangelog -l "$linux_source/debian/changelog" -S Version)" != 6.12.116-1 ]; then
    echo "Expected the Debian linux source at 6.12.116-1." >&2
    exit 1
fi

mkdir -- "$build_dir"
git -C "$upstream" archive --format=tar -o "$build_dir/t1-source.tar" "$commit" \
    kernel/appletbdrm-t1 kernel/t1-cfgsel kernel/apple-t1-ncm
tar -xf "$build_dir/t1-source.tar" -C "$build_dir"
rm -- "$build_dir/t1-source.tar"
patch --batch --forward --fuzz=0 -p1 -d "$build_dir" \
    < "$here/patches/appletbdrm-t1-debian-6.12.patch"

mkdir -p -- "$build_dir/linux/drivers/media/usb"
cp -R -- "$linux_source/drivers/media/usb/uvc" "$build_dir/linux/drivers/media/usb/"
patch --batch --forward --fuzz=0 -p1 -d "$build_dir/linux" \
    < "$here/patches/uvc-t1-debian-6.12.116.patch"

make -C "$headers" M="$build_dir/kernel/t1-cfgsel" modules
make -C "$headers" M="$build_dir/kernel/appletbdrm-t1" modules
make -C "$headers" M="$build_dir/kernel/apple-t1-ncm" modules
make -C "$headers" M="$build_dir/linux/drivers/media/usb/uvc" modules

mkdir -- "$build_dir/modules"
install -m 0644 \
    "$build_dir/kernel/t1-cfgsel/t1_cfgsel.ko" \
    "$build_dir/kernel/appletbdrm-t1/appletbdrm.ko" \
    "$build_dir/kernel/apple-t1-ncm/apple_t1_ncm.ko" \
    "$build_dir/linux/drivers/media/usb/uvc/uvcvideo.ko" "$build_dir/modules/"
for module in "$build_dir/modules/"*.ko; do
    vermagic=$(modinfo -F vermagic "$module")
    test "${vermagic%% *}" = "$release"
    printf '%s %s\n' "${module##*/}" "$vermagic"
done
