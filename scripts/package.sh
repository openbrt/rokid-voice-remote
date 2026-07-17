#!/bin/sh
set -eu

PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(sed -n '1p' "$PROJECT/VERSION")
BINARY=$PROJECT/build/voice_remote_hid
ARCHIVE=$PROJECT/dist/rokid-voice-remote-$VERSION.tar.gz

[ -x "$BINARY" ] || { echo "missing build/voice_remote_hid; run scripts/build-on-sdk.sh" >&2; exit 1; }
mkdir -p "$PROJECT/dist"
stage=$(mktemp -d "$PROJECT/dist/.stage.XXXXXX")
cleanup() {
    rm -rf "$stage"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$stage/bin" "$stage/config" "$stage/lua" "$stage/systemd"
cp "$BINARY" "$stage/bin/voice_remote_hid"
cp "$PROJECT/firmware/bin/voice-listener.sh" "$stage/bin/voice-listener.sh"
cp "$PROJECT/firmware/bin/dispatch.sh" "$stage/bin/dispatch.sh"
cp "$PROJECT/firmware/bin/doctor.sh" "$stage/bin/doctor.sh"
cp "$PROJECT/firmware/bin/paired-devices.sh" "$stage/bin/paired-devices.sh"
cp -R "$PROJECT/firmware/lua/voice" "$stage/lua/voice"
cp "$PROJECT/firmware/systemd/"*.service "$stage/systemd/"
cp "$PROJECT/config/commands.tsv" "$stage/config/commands.tsv"
cp "$PROJECT/config/targets.conf.example" "$stage/config/targets.conf.example"
cp "$PROJECT/config/project.env" "$stage/project.env"
cp "$PROJECT/firmware/install.sh" "$stage/install.sh"
cp "$PROJECT/firmware/uninstall.sh" "$stage/uninstall.sh"
cp "$PROJECT/VERSION" "$stage/VERSION"

chmod 0755 "$stage/bin/"* "$stage/install.sh" "$stage/uninstall.sh"
chmod 0644 "$stage/config/"* "$stage/lua/voice/main.lua" \
    "$stage/systemd/"* "$stage/project.env" "$stage/VERSION"

(
    cd "$stage"
    find . -type f ! -name MANIFEST.sha256 | LC_ALL=C sort |
    while IFS= read -r file; do
        shasum -a 256 "$file"
    done > MANIFEST.sha256
)

rm -f "$ARCHIVE"
tar -C "$stage" -czf "$ARCHIVE" .
echo "PACKAGE_OK $ARCHIVE"
