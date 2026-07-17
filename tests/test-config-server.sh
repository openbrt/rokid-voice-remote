#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=$PROJECT/build/voice_remote_config-host
TEST_ROOT=$(mktemp -d)
PORT=$((20000 + ($$ % 20000)))
TOKEN=0123456789abcdef0123456789abcdef0123456789abcdef
SERVER_PID=

cleanup() {
    [ -z "$SERVER_PID" ] || kill "$SERVER_PID" 2>/dev/null || true
    [ -z "$SERVER_PID" ] || wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$TEST_ROOT"
}
trap cleanup EXIT INT TERM HUP

mkdir -p "$TEST_ROOT/config" "$TEST_ROOT/web"
cp "$PROJECT/config/commands.tsv" "$TEST_ROOT/config/commands.tsv"
cp "$PROJECT/config/targets.conf.example" "$TEST_ROOT/config/targets.conf"
cp "$PROJECT/firmware/web/"* "$TEST_ROOT/web/"
printf '%s\n' "$TOKEN" > "$TEST_ROOT/config/web-token"

"$BINARY" validate "$TEST_ROOT/config/commands.tsv" \
    "$TEST_ROOT/config/targets.conf" >/dev/null
"$BINARY" serve --root "$TEST_ROOT" --bind 127.0.0.1 --port "$PORT" \
    --no-restart > "$TEST_ROOT/server.log" 2>&1 &
SERVER_PID=$!

attempt=0
while [ "$attempt" -lt 40 ]; do
    if curl -fsS "http://127.0.0.1:$PORT/" >/dev/null 2>&1; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.05
done
[ "$attempt" -lt 40 ] || { echo "config server did not start" >&2; exit 1; }

status=$(curl -sS -o /dev/null -w '%{http_code}' \
    "http://127.0.0.1:$PORT/api/status")
[ "$status" = 401 ] || { echo "unauthorized API status=$status" >&2; exit 1; }

curl -fsS -H "X-Config-Token: $TOKEN" \
    "http://127.0.0.1:$PORT/api/commands" > "$TEST_ROOT/received.tsv"
cmp "$TEST_ROOT/config/commands.tsv" "$TEST_ROOT/received.tsv"

cp "$PROJECT/config/commands.tsv" "$TEST_ROOT/new-commands.tsv"
cp "$PROJECT/config/targets.conf.example" "$TEST_ROOT/new-targets.conf"
curl -fsS -H "X-Config-Token: $TOKEN" \
    --data-urlencode "commands@$TEST_ROOT/new-commands.tsv" \
    --data-urlencode "targets@$TEST_ROOT/new-targets.conf" \
    "http://127.0.0.1:$PORT/api/config" | grep -q '^OK saved commands=4 targets=2'

awk '!changed && /0x0030/ { sub(/0x0030/, "0xffff"); changed=1 } { print }' \
    "$PROJECT/config/commands.tsv" > "$TEST_ROOT/invalid.tsv"
status=$(curl -sS -o "$TEST_ROOT/error.txt" -w '%{http_code}' \
    -H "X-Config-Token: $TOKEN" \
    --data-urlencode "commands@$TEST_ROOT/invalid.tsv" \
    --data-urlencode "targets@$TEST_ROOT/new-targets.conf" \
    "http://127.0.0.1:$PORT/api/config")
[ "$status" = 422 ] || { echo "invalid configuration status=$status" >&2; exit 1; }
grep -q 'invalid key data' "$TEST_ROOT/error.txt"

cp "$PROJECT/config/commands.tsv" "$TEST_ROOT/overflow.tsv"
printf '%b\n' \
    '测试第五条\tce4shi4di4wu3tiao2\ttelevision\tconsumer\t0x00e2\t1' \
    '测试第六条\tce4shi4di4liu4tiao2\ttelevision\tconsumer\t0x00cd\t1' \
    >> "$TEST_ROOT/overflow.tsv"
status=$(curl -sS -o "$TEST_ROOT/overflow-error.txt" -w '%{http_code}' \
    -H "X-Config-Token: $TOKEN" \
    --data-urlencode "commands@$TEST_ROOT/overflow.tsv" \
    --data-urlencode "targets@$TEST_ROOT/new-targets.conf" \
    "http://127.0.0.1:$PORT/api/config")
[ "$status" = 422 ] || { echo "overflow configuration status=$status" >&2; exit 1; }
grep -q 'command count exceeds 5' "$TEST_ROOT/overflow-error.txt"

curl -fsS "http://127.0.0.1:$PORT/app.js" | grep -q "MAX_COMMANDS = 5"
echo "TEST_CONFIG_SERVER_OK"
