#!/bin/sh
set -u

ROOT=${ROKID_VOICE_REMOTE_ROOT:-/data/rokid-voice-remote}
failed=0

check_file() {
    if [ -e "$1" ]; then
        echo "OK file $1"
    else
        echo "FAIL missing $1" >&2
        failed=1
    fi
}

check_unit() {
    if systemctl is-active "$1" >/dev/null 2>&1; then
        echo "OK active $1"
    else
        echo "FAIL inactive $1" >&2
        failed=1
    fi
}

check_file /usr/bin/rklua
check_file /usr/bin/prepare-bsiren
check_file /usr/lib/libbsa.so
check_file /usr/lua/lib/rokidsiren.so
check_file "$ROOT/bin/voice_remote_hid"
check_file "$ROOT/bin/voice_remote_config"
check_file "$ROOT/config/commands.tsv"
check_file "$ROOT/config/targets.conf"
check_file "$ROOT/config/web-token"
check_file "$ROOT/web/index.html"

check_unit bsa_server.service
check_unit rokid-voice-remote-hid.service
check_unit rokid-voice-remote-voice.service
check_unit rokid-voice-remote-config.service

if "$ROOT/bin/voice_remote_hid" ctl status; then
    :
else
    echo "FAIL HID control socket" >&2
    failed=1
fi

if "$ROOT/bin/voice_remote_config" validate \
    "$ROOT/config/commands.tsv" "$ROOT/config/targets.conf"; then
    :
else
    echo "FAIL configuration validation" >&2
    failed=1
fi

count=$(awk -F '\t' '!/^#/ && NF { count++ } END { print count + 0 }' \
    "$ROOT/config/commands.tsv")
if [ "$count" -ge 1 ] && [ "$count" -le 5 ]; then
    echo "OK commands=$count"
else
    echo "FAIL command count=$count (expected 1..5)" >&2
    failed=1
fi

if grep -q '00:00:00:00:00:00' "$ROOT/config/targets.conf"; then
    echo "WARN one or more Bluetooth targets still use placeholder addresses"
fi

exit "$failed"
