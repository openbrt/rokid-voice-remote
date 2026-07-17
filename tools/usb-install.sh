#!/bin/sh
set -eu

PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SERIAL=${ADB_SERIAL:-}
PACKAGE=
REMOTE=/data/local/tmp/rokid-voice-remote-install

while [ "$#" -gt 0 ]; do
    case "$1" in
        --serial) [ "$#" -ge 2 ] || exit 2; SERIAL=$2; shift 2 ;;
        --package) [ "$#" -ge 2 ] || exit 2; PACKAGE=$2; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

command -v adb >/dev/null 2>&1 || { echo "adb is required" >&2; exit 1; }

if [ -z "$SERIAL" ]; then
    SERIAL=$(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
    count=$(printf '%s\n' "$SERIAL" | awk 'NF { count++ } END { print count + 0 }')
    [ "$count" -eq 1 ] || {
        echo "expected exactly one ADB device; use --serial" >&2
        exit 1
    }
fi

adb_cmd() {
    adb -s "$SERIAL" "$@"
}

if [ -z "$PACKAGE" ]; then
    "$PROJECT/scripts/build.sh"
    version=$(sed -n '1p' "$PROJECT/VERSION")
    PACKAGE=$PROJECT/dist/rokid-voice-remote-$version.tar.gz
fi
[ -r "$PACKAGE" ] || { echo "package not found: $PACKAGE" >&2; exit 1; }

adb_cmd wait-for-device
adb_cmd shell rm -rf "$REMOTE"
adb_cmd shell mkdir -p "$REMOTE"
adb_cmd push "$PACKAGE" "$REMOTE/package.tar.gz"
adb_cmd shell "gzip -dc '$REMOTE/package.tar.gz' | tar -C '$REMOTE' -xf -"
adb_cmd shell sh "$REMOTE/install.sh"
adb_cmd shell rm -rf "$REMOTE"
echo "USB_INSTALL_OK serial=$SERIAL"
