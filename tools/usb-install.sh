#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
SERIAL=${ADB_SERIAL:-}
PACKAGE=
REMOTE=/data/local/tmp/rokid-voice-remote-install
REPLACE=0
OPEN_PAGE=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --serial) [ "$#" -ge 2 ] || exit 2; SERIAL=$2; shift 2 ;;
        --package) [ "$#" -ge 2 ] || exit 2; PACKAGE=$2; shift 2 ;;
        --replace) REPLACE=1; shift ;;
        --no-open) OPEN_PAGE=0; shift ;;
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
if [ "$REPLACE" -eq 1 ] && adb_cmd shell test -d /data/rokid-chatgpt; then
    mkdir -p "$PROJECT/backups"
    backup=$PROJECT/backups/rokid-chatgpt-$SERIAL-$(date +%Y%m%d-%H%M%S)
    [ ! -e "$backup" ] || { echo "backup path already exists: $backup" >&2; exit 1; }
    adb_cmd pull /data/rokid-chatgpt "$backup"
    [ -f "$backup/state/original-state-v1" ] || {
        echo "conflicting profile backup is incomplete" >&2
        exit 1
    }
    echo "CONFLICT_BACKUP $backup"
fi
adb_cmd shell rm -rf "$REMOTE"
adb_cmd shell mkdir -p "$REMOTE"
adb_cmd push "$PACKAGE" "$REMOTE/package.tar.gz"
adb_cmd shell "gzip -dc '$REMOTE/package.tar.gz' | tar -C '$REMOTE' -xf -"
adb_cmd shell "ROKID_VOICE_REMOTE_REPLACE_CONFLICT=$REPLACE sh '$REMOTE/install.sh'"
adb_cmd shell rm -rf "$REMOTE"
device_ip=$(adb_cmd shell ip address show | tr -d '\r' | \
    awk '/inet / && $2 !~ /^127\./ { sub("/.*", "", $2); print $2; exit }')
forward_port=$(adb_cmd forward tcp:0 tcp:8090 2>/dev/null || true)
case "$forward_port" in ''|*[!0-9]*) forward_port= ;; esac
if [ -n "$forward_port" ]; then
    config_url=http://127.0.0.1:$forward_port/
elif [ -n "$device_ip" ]; then
    config_url=http://$device_ip:8090/
else
    config_url=
fi
if [ -n "$config_url" ]; then
    echo "CONFIG_URL $config_url"
    if [ "$OPEN_PAGE" -eq 1 ] && command -v open >/dev/null 2>&1; then
        open "$config_url"
    fi
fi
echo "USB_INSTALL_OK serial=$SERIAL"
