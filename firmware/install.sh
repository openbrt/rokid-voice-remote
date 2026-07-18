#!/bin/sh
set -eu

PACKAGE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
# project.env is beside install.sh in the assembled device package.
# shellcheck disable=SC1091
. "$PACKAGE/project.env"

ROOT=$DEVICE_ROOT
RUN=$RUN_ROOT
STATE=$ROOT/state
SYSTEMD=/etc/systemd/system
EXPECTED_ROOT=/data/rokid-voice-remote
EXPECTED_RUN=/run/rokid-voice-remote

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
INSTALL_COMPLETE=0
STATE_CAPTURED=0

fail() {
    echo "INSTALL_FAILED $*" >&2
    exit 1
}

restore_original_state() {
    for unit in $MANAGED_UNITS; do
        systemctl stop "$unit" >/dev/null 2>&1 || true
        systemctl disable "$unit" >/dev/null 2>&1 || true
    done
    if [ -r "$STATE/original-enabled" ]; then
        while IFS= read -r unit; do
            [ -n "$unit" ] || continue
            systemctl enable "$unit" >/dev/null 2>&1 || true
        done < "$STATE/original-enabled"
    fi
    if [ -r "$STATE/original-active" ]; then
        while IFS= read -r unit; do
            [ -n "$unit" ] || continue
            systemctl start "$unit" >/dev/null 2>&1 || true
        done < "$STATE/original-active"
    fi
}

rollback_install() {
    [ "$INSTALL_COMPLETE" -eq 0 ] || return 0
    [ "$STATE_CAPTURED" -eq 1 ] || return 0
    echo "Install did not complete; restoring original service state." >&2
    for unit in $PROJECT_UNITS; do
        systemctl stop "$unit" >/dev/null 2>&1 || true
        systemctl disable "$unit" >/dev/null 2>&1 || true
        rm -f "$SYSTEMD/$unit"
    done
    systemctl daemon-reload >/dev/null 2>&1 || true
    restore_original_state
    rm -rf "$ROOT" "$RUN"
}

trap rollback_install EXIT INT TERM HUP

[ "$(id -u)" = 0 ] || fail "run as root"
[ "$ROOT" = "$EXPECTED_ROOT" ] || fail "unsafe DEVICE_ROOT: $ROOT"
[ "$RUN" = "$EXPECTED_RUN" ] || fail "unsafe RUN_ROOT: $RUN"
[ "$(uname -m)" = aarch64 ] || fail "unsupported architecture: $(uname -m)"
[ ! -e "$ROOT/state/original-state-v1" ] || \
    fail "an installation already exists; uninstall it before reinstalling"

for file in \
    /usr/bin/rklua \
    /usr/bin/prepare-bsiren \
    /usr/bin/prepare-pulseaudio \
    /usr/lib/libbsa.so \
    /usr/lua/lib/rokidsiren.so \
    "$PACKAGE/bin/voice_remote_config" \
    "$PACKAGE/bin/voice_remote_hid" \
    "$PACKAGE/MANIFEST.sha256"; do
    [ -e "$file" ] || fail "missing required component: $file"
done

command -v sha256sum >/dev/null 2>&1 || fail "sha256sum is required"
(cd "$PACKAGE" && sha256sum -c MANIFEST.sha256 >/dev/null) || \
    fail "release manifest verification failed"

bsa_hash=$(sha256sum /usr/lib/libbsa.so | awk '{print $1}')
case "$bsa_hash" in
    6e61a4369b8d758a0e9e060d183e015ed6758d2f8d1c1430f45b03ce322e93ae) ;;
    *) fail "unsupported factory libbsa.so: $bsa_hash" ;;
esac

if [ -e /data/rokid-chatgpt ]; then
    if [ "${ROKID_VOICE_REMOTE_REPLACE_CONFLICT:-0}" != 1 ]; then
        fail "/data/rokid-chatgpt exists; use the explicit replacement option"
    fi
    conflict_uninstaller=
    if [ -x /data/rokid-chatgpt/uninstall.sh ]; then
        conflict_uninstaller=/data/rokid-chatgpt/uninstall.sh
    elif [ -x /data/rokid-chatgpt/bin/uninstall.sh ]; then
        conflict_uninstaller=/data/rokid-chatgpt/bin/uninstall.sh
    fi
    [ -n "$conflict_uninstaller" ] || \
        fail "conflicting profile has no recoverable uninstall script"
    echo "REPLACING_CONFLICT /data/rokid-chatgpt"
    "$conflict_uninstaller" || fail "conflicting profile uninstall failed"
    [ ! -e /data/rokid-chatgpt ] || fail "conflicting profile was not removed"
fi

siren_hash=$(sha256sum /usr/lua/lib/rokidsiren.so | awk '{print $1}')
case "$siren_hash" in
    a3377d10dd39a973af55740baf3d74dd26069bcd230b94582e7460f1260828af|\
    3503568af5ebf83457a715c3bf0599636235bbca15a6ba887f69173cd9f08f5f) ;;
    *) fail "unsupported factory rokidsiren.so: $siren_hash" ;;
esac

for unit in $PROJECT_UNITS; do
    systemctl stop "$unit" >/dev/null 2>&1 || true
done

mkdir -p "$ROOT/bin" "$ROOT/config" "$ROOT/lua" "$ROOT/web" "$STATE" "$RUN"

: > "$STATE/original-enabled"
: > "$STATE/original-active"
for unit in $MANAGED_UNITS; do
    if systemctl is-enabled "$unit" >/dev/null 2>&1; then
        echo "$unit" >> "$STATE/original-enabled"
    fi
    if systemctl is-active "$unit" >/dev/null 2>&1; then
        echo "$unit" >> "$STATE/original-active"
    fi
done
echo 1 > "$STATE/original-state-v1"
STATE_CAPTURED=1

cp "$PACKAGE/bin/voice_remote_hid" "$ROOT/bin/voice_remote_hid"
cp "$PACKAGE/bin/voice_remote_config" "$ROOT/bin/voice_remote_config"
cp "$PACKAGE/bin/voice-listener.sh" "$ROOT/bin/voice-listener.sh"
cp "$PACKAGE/bin/dispatch.sh" "$ROOT/bin/dispatch.sh"
cp "$PACKAGE/bin/doctor.sh" "$ROOT/bin/doctor.sh"
cp "$PACKAGE/bin/paired-devices.sh" "$ROOT/bin/paired-devices.sh"
cp "$PACKAGE/uninstall.sh" "$ROOT/uninstall.sh"
cp "$PACKAGE/project.env" "$ROOT/project.env"
rm -rf "$ROOT/lua/voice"
cp -R "$PACKAGE/lua/voice" "$ROOT/lua/voice"
rm -rf "$ROOT/web"
cp -R "$PACKAGE/web" "$ROOT/web"

if [ ! -e "$ROOT/config/commands.tsv" ]; then
    cp "$PACKAGE/config/commands.tsv" "$ROOT/config/commands.tsv"
fi
if [ ! -e "$ROOT/config/targets.conf" ]; then
    cp "$PACKAGE/config/targets.conf.example" "$ROOT/config/targets.conf"
fi
if [ ! -s "$ROOT/config/web-token" ]; then
    umask 077
    od -An -tx1 -N24 /dev/urandom | tr -d ' \n' > "$ROOT/config/.web-token.tmp"
    [ "$(wc -c < "$ROOT/config/.web-token.tmp")" -eq 48 ] || \
        fail "configuration token generation failed"
    mv "$ROOT/config/.web-token.tmp" "$ROOT/config/web-token"
fi
umask 022

chmod 0755 "$ROOT/bin/voice_remote_hid" "$ROOT/bin/voice_remote_config" \
    "$ROOT/bin/voice-listener.sh" \
    "$ROOT/bin/dispatch.sh" "$ROOT/bin/doctor.sh" \
    "$ROOT/bin/paired-devices.sh" "$ROOT/uninstall.sh"
chmod 0644 "$ROOT/config/commands.tsv" "$ROOT/config/targets.conf" \
    "$ROOT/lua/voice/main.lua" "$ROOT/project.env" "$ROOT/web/"*
chmod 0600 "$ROOT/config/web-token"
chmod 0755 "$ROOT" "$ROOT/bin" "$ROOT/lua" "$ROOT/web" "$STATE" "$RUN"
chmod 0700 "$ROOT/config"

for unit in $VENDOR_UNITS; do
    systemctl stop "$unit" >/dev/null 2>&1 || true
    systemctl disable "$unit" >/dev/null 2>&1 || true
done
for unit in $SUPPORT_UNITS; do
    systemctl enable "$unit" >/dev/null 2>&1 || true
    systemctl start "$unit" >/dev/null 2>&1 || true
done

for unit in $PROJECT_UNITS; do
    cp "$PACKAGE/systemd/$unit" "$SYSTEMD/$unit"
    chmod 0644 "$SYSTEMD/$unit"
done
systemctl daemon-reload
for unit in $PROJECT_UNITS; do
    systemctl enable "$unit"
    systemctl start "$unit"
done

sleep 2
"$ROOT/bin/doctor.sh" || fail "post-install diagnostics failed"

INSTALL_COMPLETE=1
trap - EXIT INT TERM HUP
echo "INSTALL_OK root=$ROOT"
device_ip=$(ip address show 2>/dev/null | awk '/inet / && $2 !~ /^127\./ { sub("/.*", "", $2); print $2; exit }')
config_token=$(sed -n '1p' "$ROOT/config/web-token")
if [ -n "$device_ip" ]; then
    echo "CONFIG_URL http://$device_ip:$CONFIG_PORT/#$config_token"
else
    echo "CONFIG_PAGE port=$CONFIG_PORT token=$config_token"
fi
echo "Next: learn remote keys over USB, then pair 'Rokid Voice Remote' on the TV/projector"
