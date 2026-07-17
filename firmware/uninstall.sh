#!/bin/sh
set -eu

ROOT=/data/rokid-voice-remote
RUN=/run/rokid-voice-remote
STATE=$ROOT/state
SYSTEMD=/etc/systemd/system

VENDOR_UNITS="
activation.service
light.service
lumenflinger.service
mqtt.service
track.service
homebase.service
pivotdb.service
zygote.service
ota.timer
xms.service
xms-convd.service
hfp-supervisor.service
hfp-ledd.service
hfp-keyd.service
"

SUPPORT_UNITS="
net_manager.service
bsa_server.service
pulseaudio.service
btflinger.service
"

PROJECT_UNITS="
rokid-voice-remote-config.service
rokid-voice-remote-hid.service
rokid-voice-remote-voice.service
"

MANAGED_UNITS="$SUPPORT_UNITS $VENDOR_UNITS"

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
[ -r "$STATE/original-state-v1" ] || {
    echo "missing original service state; refusing an incomplete restore" >&2
    exit 1
}

for unit in $PROJECT_UNITS; do
    systemctl stop "$unit" >/dev/null 2>&1 || true
    systemctl disable "$unit" >/dev/null 2>&1 || true
    rm -f "$SYSTEMD/$unit"
done
systemctl daemon-reload

for unit in $MANAGED_UNITS; do
    systemctl stop "$unit" >/dev/null 2>&1 || true
    systemctl disable "$unit" >/dev/null 2>&1 || true
done

while IFS= read -r unit; do
    [ -n "$unit" ] || continue
    systemctl enable "$unit" >/dev/null 2>&1 || true
done < "$STATE/original-enabled"

while IFS= read -r unit; do
    [ -n "$unit" ] || continue
    systemctl start "$unit" >/dev/null 2>&1 || true
done < "$STATE/original-active"

rm -rf "$ROOT" "$RUN"
echo "UNINSTALL_OK restored original systemd state"
