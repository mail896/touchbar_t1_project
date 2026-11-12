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
