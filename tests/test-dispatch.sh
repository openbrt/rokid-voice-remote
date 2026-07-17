#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
TEST_ROOT=$(mktemp -d)
cleanup() {
    rm -rf "$TEST_ROOT"
}
trap cleanup EXIT INT TERM HUP

cat > "$TEST_ROOT/commands.tsv" <<'EOF'
# phrase<TAB>pinyin<TAB>target<TAB>kind<TAB>code<TAB>repeat
打开电视	da3kai1dian4shi4	television	consumer	0x0030	1
确认	que4ren4	active	key	0x28	2
EOF

cat > "$TEST_ROOT/targets.conf" <<'EOF'
television	AA:BB:CC:DD:EE:FF
EOF

cat > "$TEST_ROOT/fake-hidctl" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$FAKE_HID_LOG"
printf 'OK fake\n'
EOF
chmod 0755 "$TEST_ROOT/fake-hidctl"
: > "$TEST_ROOT/hid.log"

run_dispatch() {
    FAKE_HID_LOG="$TEST_ROOT/hid.log" \
    ROKID_VOICE_REMOTE_ROOT="$TEST_ROOT" \
    ROKID_VOICE_REMOTE_RUN="$TEST_ROOT/run" \
    VOICE_REMOTE_COMMANDS="$TEST_ROOT/commands.tsv" \
    VOICE_REMOTE_TARGETS="$TEST_ROOT/targets.conf" \
    VOICE_REMOTE_HIDCTL="$TEST_ROOT/fake-hidctl" \
        "$PROJECT/firmware/bin/dispatch.sh" "$@"
}

run_dispatch 打开电视 >/dev/null
run_dispatch 确认 >/dev/null

cat > "$TEST_ROOT/expected.log" <<'EOF'
ctl target AA:BB:CC:DD:EE:FF
ctl consumer 0x0030 1
ctl key 0x28 0 2
EOF

cmp "$TEST_ROOT/expected.log" "$TEST_ROOT/hid.log"

if run_dispatch 不存在 >/dev/null 2>&1; then
    echo "unmapped phrase unexpectedly succeeded" >&2
    exit 1
fi

sed 's/AA:BB:CC:DD:EE:FF/00:00:00:00:00:00/' \
    "$TEST_ROOT/targets.conf" > "$TEST_ROOT/targets.placeholder"
mv "$TEST_ROOT/targets.placeholder" "$TEST_ROOT/targets.conf"
if run_dispatch 打开电视 >/dev/null 2>&1; then
    echo "placeholder target unexpectedly succeeded" >&2
    exit 1
fi

echo "TEST_DISPATCH_OK"
