#!/bin/sh
set -eu

ROOT=${ROKID_VOICE_REMOTE_ROOT:-/data/rokid-voice-remote}
DISPATCH=${VOICE_REMOTE_DISPATCH:-$ROOT/bin/dispatch.sh}
pending=
trigger_at=0

export ROKID_VOICE_REMOTE_ROOT="$ROOT"

/usr/bin/rklua -l "$ROOT/lua/voice" voice 2>&1 |
while IFS= read -r line; do
    echo "$line"
    case "$line" in
        *"trigger: "*", start:"*)
            pending=${line#*trigger: }
            pending=${pending%%, start:*}
            trigger_at=$(date +%s)
            ;;
        *VOICE_REMOTE_AWAKE*)
            now=$(date +%s)
            age=$((now - trigger_at))
            if [ -n "$pending" ] && [ "$age" -ge 0 ] && [ "$age" -le 3 ]; then
                "$DISPATCH" "$pending" || \
                    echo "VOICE_REMOTE_DISPATCH_FAILED phrase=$pending" >&2
            else
                echo "VOICE_REMOTE_AWAKE_WITHOUT_FRESH_TRIGGER" >&2
            fi
            pending=
            trigger_at=0
            ;;
        *VOICE_REMOTE_CANCEL*)
            pending=
            trigger_at=0
            ;;
    esac
done

# A healthy rklua listener is long-lived. Let systemd restart either side of a
# broken pipeline instead of silently leaving voice control disabled.
exit 32
