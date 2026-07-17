#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(sed -n '1p' "$PROJECT/VERSION")
BINARY=$PROJECT/build/voice_remote_hid
CONFIG_BINARY=$PROJECT/build/voice_remote_config
ARCHIVE=$PROJECT/dist/rokid-voice-remote-$VERSION.tar.gz

[ -x "$BINARY" ] || { echo "missing build/voice_remote_hid; run scripts/build-on-sdk.sh" >&2; exit 1; }
[ -x "$CONFIG_BINARY" ] || { echo "missing build/voice_remote_config; run scripts/build-on-sdk.sh" >&2; exit 1; }
mkdir -p "$PROJECT/dist"
stage=$(mktemp -d "$PROJECT/dist/.stage.XXXXXX")
manifest_files=$(mktemp "$PROJECT/dist/.manifest-files.XXXXXX")
cleanup() {
    rm -rf "$stage"
    rm -f "$manifest_files"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$stage/bin" "$stage/config" "$stage/lua" "$stage/systemd" "$stage/web"
cp "$BINARY" "$stage/bin/voice_remote_hid"
cp "$CONFIG_BINARY" "$stage/bin/voice_remote_config"
cp "$PROJECT/firmware/bin/voice-listener.sh" "$stage/bin/voice-listener.sh"
cp "$PROJECT/firmware/bin/dispatch.sh" "$stage/bin/dispatch.sh"
cp "$PROJECT/firmware/bin/doctor.sh" "$stage/bin/doctor.sh"
cp "$PROJECT/firmware/bin/paired-devices.sh" "$stage/bin/paired-devices.sh"
cp -R "$PROJECT/firmware/lua/voice" "$stage/lua/voice"
cp "$PROJECT/firmware/systemd/"*.service "$stage/systemd/"
cp "$PROJECT/firmware/web/index.html" "$stage/web/index.html"
cp "$PROJECT/firmware/web/style.css" "$stage/web/style.css"
cp "$PROJECT/firmware/web/app.js" "$stage/web/app.js"
cp "$PROJECT/config/commands.tsv" "$stage/config/commands.tsv"
cp "$PROJECT/config/targets.conf.example" "$stage/config/targets.conf.example"
cp "$PROJECT/config/project.env" "$stage/project.env"
cp "$PROJECT/firmware/install.sh" "$stage/install.sh"
cp "$PROJECT/firmware/uninstall.sh" "$stage/uninstall.sh"
cp "$PROJECT/VERSION" "$stage/VERSION"

chmod 0755 "$stage/bin/"* "$stage/install.sh" "$stage/uninstall.sh"
chmod 0644 "$stage/config/"* "$stage/lua/voice/main.lua" \
    "$stage/systemd/"* "$stage/web/"* "$stage/project.env" "$stage/VERSION"

(
    cd "$stage"
    find . -type f ! -name MANIFEST.sha256 | LC_ALL=C sort > "$manifest_files"
    while IFS= read -r file; do
        shasum -a 256 "$file"
    done < "$manifest_files" > MANIFEST.sha256
)

rm -f "$ARCHIVE"
tar -C "$stage" -czf "$ARCHIVE" .
echo "PACKAGE_OK $ARCHIVE"
