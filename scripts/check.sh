#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
COMMANDS=$PROJECT/config/commands.tsv

find "$PROJECT/firmware" "$PROJECT/scripts" "$PROJECT/tools" -type f -name '*.sh' |
while IFS= read -r script; do
    sh -n "$script"
done
sh -n "$PROJECT/tests/test-dispatch.sh"
sh -n "$PROJECT/tests/test-config-server.sh"

awk -F '\t' '
    /^#/ || NF == 0 { next }
    NF != 6 { print "commands.tsv: expected 6 fields at line " NR > "/dev/stderr"; failed=1 }
    $1 in seen { print "commands.tsv: duplicate phrase at line " NR > "/dev/stderr"; failed=1 }
    { seen[$1]=1; count++ }
    END {
        if (count < 1 || count > 5) {
            print "commands.tsv: expected 1..5 commands, got " count > "/dev/stderr"
            failed=1
        }
        exit failed
    }
' "$COMMANDS"

if command -v shellcheck >/dev/null 2>&1; then
    find "$PROJECT/firmware" "$PROJECT/scripts" "$PROJECT/tools" "$PROJECT/tests" \
        -type f -name '*.sh' -print0 |
        xargs -0 shellcheck -x
fi

if command -v luac >/dev/null 2>&1; then
    luac -p "$PROJECT/firmware/lua/voice/main.lua"
fi

if command -v node >/dev/null 2>&1; then
    node --check "$PROJECT/firmware/web/app.js"
fi

mkdir -p "$PROJECT/build"
cc -std=c11 -O2 -Wall -Wextra -Werror \
    -o "$PROJECT/build/voice_remote_config-host" \
    "$PROJECT/src/voice_remote_config.c"

if find "$PROJECT" \
    \( -path "$PROJECT/.git" -o -path "$PROJECT/build" -o \
       -path "$PROJECT/dist" -o -path "$PROJECT/backups" \) -prune -o \
    -type f \( -name 'bsa_api.h' -o -name 'libbsa.so' -o \
    -name 'rokidsiren.so' \) -print | grep -q .; then
    echo "vendor headers or binaries must not be committed" >&2
    exit 1
fi

"$PROJECT/tests/test-dispatch.sh"
"$PROJECT/tests/test-config-server.sh"

echo "CHECK_OK"
