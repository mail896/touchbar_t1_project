#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo 'Usage: stage.sh T1BRIDGE_CHECKOUT NEW_OUTPUT_DIRECTORY' >&2
    exit 2
fi

project=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
upstream=$1
commit=81cbdf81026a16e02f0bea74735c6b029a8ffae2
if [ -e "$2" ] || [ -L "$2" ]; then
    echo 'The output directory must be new.' >&2
    exit 2
fi
output=$(realpath -m -- "$2")
image_tool=magick
if ! command -v "$image_tool" >/dev/null 2>&1; then
    image_tool=convert
fi
if ! command -v "$image_tool" >/dev/null 2>&1; then
    echo 'ImageMagick is required to prepare the shortcut artwork.' >&2
    exit 2
fi

work=$(mktemp -d)
staged=
cleanup() {
    rm -rf -- "$work"
    if [ -n "$staged" ]; then
        rm -rf -- "$staged"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir "$work/source"
git -C "$upstream" archive --format=tar --output="$work/base.tar" "$commit"
tar -xf "$work/base.tar" -C "$work/source"

(
    cd "$work/source"
    git apply --check "$project/renderer.patch"
    git apply "$project/renderer.patch"
    mkdir -p crates/t1-touchbar/assets
    "$image_tool" "$project/docs/assets/bbs-einbeck.png" -colorspace sRGB \
        -resize 108x50 -background none -gravity center -extent 108x50 \
        -alpha on -depth 8 RGBA:crates/t1-touchbar/assets/shortcut-1.rgba
    "$image_tool" "$project/docs/assets/laberpapa.png" -colorspace sRGB -trim +repage \
        -resize 108x50 -background none -gravity center -extent 108x50 \
        -alpha on -depth 8 RGBA:crates/t1-touchbar/assets/shortcut-2.rgba
    cargo build --locked --release --target-dir "$work/target" -p t1-touchbar --bin t1-touchbar-kde
)

staged=$(mktemp -d "${output}.XXXXXXXX")
install -d "$staged/bin" "$staged/systemd"
install -m 0755 "$work/target/release/t1-touchbar-kde" "$staged/bin/"
install -m 0755 "$project/bin/t1bridge-kde-provider" "$staged/bin/"
install -m 0644 "$project/systemd/10-kde-provider.conf" \
    "$project/systemd/20-graphical-session.conf" "$staged/systemd/"
mv -T -- "$staged" "$output"
staged=
printf '%s\n' "$output"
