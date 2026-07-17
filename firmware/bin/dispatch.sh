#!/bin/sh
set -eu

ROOT=${ROKID_VOICE_REMOTE_ROOT:-/data/rokid-voice-remote}
RUN=${ROKID_VOICE_REMOTE_RUN:-/run/rokid-voice-remote}
COMMANDS=${VOICE_REMOTE_COMMANDS:-$ROOT/config/commands.tsv}
TARGETS=${VOICE_REMOTE_TARGETS:-$ROOT/config/targets.conf}
HIDCTL=${VOICE_REMOTE_HIDCTL:-$ROOT/bin/voice_remote_hid}
TAB=$(printf '\t')

fail() {
    echo "VOICE_REMOTE_ERROR $*" >&2
    exit 1
}

[ "$#" -eq 1 ] || fail "dispatch requires exactly one phrase"
phrase=$1
[ -r "$COMMANDS" ] || fail "cannot read $COMMANDS"

target=
kind=
code=
repeat=
found=0

while IFS="$TAB" read -r row_phrase row_pinyin row_target row_kind row_code row_repeat extra; do
    case "$row_phrase" in ''|'#'*) continue ;; esac
    [ -z "${extra:-}" ] || fail "commands.tsv has more than 6 columns"
    [ -n "$row_pinyin" ] || fail "commands.tsv has empty pinyin"
    if [ "$row_phrase" = "$phrase" ]; then
        [ "$found" -eq 0 ] || fail "duplicate phrase: $phrase"
        found=1
        target=$row_target
        kind=$row_kind
        code=$row_code
        repeat=$row_repeat
    fi
done < "$COMMANDS"

[ "$found" -eq 1 ] || fail "unmapped phrase: $phrase"
case "$target" in *[!A-Za-z0-9_.-]*|'') fail "invalid target: $target" ;; esac
case "$kind" in consumer|key) ;; *) fail "invalid kind: $kind" ;; esac
case "$code" in 0[xX][0-9A-Fa-f]*|[0-9]*) ;; *) fail "invalid code: $code" ;; esac
case "$repeat" in ''|*[!0-9]*) fail "invalid repeat: $repeat" ;; esac
if [ "$repeat" -lt 1 ] || [ "$repeat" -gt 10 ]; then
    fail "repeat must be 1..10"
fi

mkdir -p "$RUN"
if ! mkdir "$RUN/dispatch.lock" 2>/dev/null; then
    lock_pid=
    if [ -r "$RUN/dispatch.lock/pid" ]; then
        lock_pid=$(sed -n '1p' "$RUN/dispatch.lock/pid")
    fi
    case "$lock_pid" in
        ''|*[!0-9]*) lock_pid= ;;
    esac
    if [ -n "$lock_pid" ] && kill -0 "$lock_pid" 2>/dev/null; then
        fail "another command is being dispatched"
    fi
    rm -rf "$RUN/dispatch.lock"
    mkdir "$RUN/dispatch.lock" 2>/dev/null || fail "cannot acquire dispatch lock"
fi
echo "$$" > "$RUN/dispatch.lock/pid"
trap 'rm -f "$RUN/dispatch.lock/pid"; rmdir "$RUN/dispatch.lock" 2>/dev/null || true' EXIT INT TERM HUP

if [ "$target" != active ]; then
    [ -r "$TARGETS" ] || fail "cannot read $TARGETS"
    address=
    while IFS="$TAB" read -r row_target row_address extra; do
        case "$row_target" in ''|'#'*) continue ;; esac
        [ -z "${extra:-}" ] || fail "targets.conf has more than 2 columns"
        if [ "$row_target" = "$target" ]; then
            [ -z "$address" ] || fail "duplicate target: $target"
            address=$row_address
        fi
    done < "$TARGETS"
    [ -n "$address" ] || fail "target is not configured: $target"
    [ "$address" != 00:00:00:00:00:00 ] || fail "target has placeholder address: $target"
    "$HIDCTL" ctl target "$address"
fi

case "$kind" in
    consumer) "$HIDCTL" ctl consumer "$code" "$repeat" ;;
    key) "$HIDCTL" ctl key "$code" 0 "$repeat" ;;
esac

echo "VOICE_REMOTE_SENT phrase=$phrase target=$target kind=$kind code=$code repeat=$repeat"
