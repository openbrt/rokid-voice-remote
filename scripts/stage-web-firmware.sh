#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
VERSION=$(sed -n '1p' "$PROJECT/VERSION")
ARCHIVE=rokid-voice-remote-$VERSION.tar.gz
SOURCE=$PROJECT/dist/$ARCHIVE
DEST=$PROJECT/web/public/firmware
DOWNLOAD=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --download) DOWNLOAD=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ "$DOWNLOAD" -eq 1 ]; then
    command -v gh >/dev/null 2>&1 || {
        echo "gh is required to download the release asset" >&2
        exit 1
    }
    mkdir -p "$PROJECT/dist"
    gh release download "v$VERSION" \
        --repo "${GITHUB_REPOSITORY:-openbrt/rokid-voice-remote}" \
        --pattern "$ARCHIVE" \
        --dir "$PROJECT/dist" \
        --clobber
fi

[ -r "$SOURCE" ] || {
    echo "missing $SOURCE; build/package it or use --download" >&2
    exit 1
}

mkdir -p "$DEST"
cp "$SOURCE" "$DEST/$ARCHIVE"
if command -v sha256sum >/dev/null 2>&1; then
    SHA=$(sha256sum "$SOURCE" | awk '{print $1}')
else
    SHA=$(shasum -a 256 "$SOURCE" | awk '{print $1}')
fi
SIZE=$(wc -c < "$SOURCE" | tr -d ' ')

printf '%s\n' \
    '{' \
    "  \"version\": \"$VERSION\"," \
    "  \"archive\": \"$ARCHIVE\"," \
    "  \"sha256\": \"$SHA\"," \
    "  \"size\": $SIZE" \
    '}' > "$DEST/release.json"

echo "WEB_FIRMWARE_OK $DEST/release.json"
