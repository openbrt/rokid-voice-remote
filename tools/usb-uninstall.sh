#!/bin/sh
set -eu

SERIAL=${ADB_SERIAL:-}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --serial) [ "$#" -ge 2 ] || exit 2; SERIAL=$2; shift 2 ;;
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

adb -s "$SERIAL" wait-for-device
adb -s "$SERIAL" shell test -x /data/rokid-voice-remote/uninstall.sh
adb -s "$SERIAL" shell /data/rokid-voice-remote/uninstall.sh
echo "USB_UNINSTALL_OK serial=$SERIAL"
