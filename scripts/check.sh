#!/bin/sh
set -eu

PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
COMMANDS=$PROJECT/config/commands.tsv

find "$PROJECT/firmware" "$PROJECT/scripts" "$PROJECT/tools" -type f -name '*.sh' |
while IFS= read -r script; do
    sh -n "$script"
done
sh -n "$PROJECT/tests/test-dispatch.sh"

awk -F '\t' '
    /^#/ || NF == 0 { next }
    NF != 6 { print "commands.tsv: expected 6 fields at line " NR > "/dev/stderr"; failed=1 }
    $1 in seen { print "commands.tsv: duplicate phrase at line " NR > "/dev/stderr"; failed=1 }
    { seen[$1]=1; count++ }
    END {
        if (count < 1 || count > 32) {
            print "commands.tsv: expected 1..32 commands, got " count > "/dev/stderr"
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

if find "$PROJECT" -type f \( -name 'bsa_api.h' -o -name 'libbsa.so' -o -name 'rokidsiren.so' \) | grep -q .; then
    echo "vendor headers or binaries must not be committed" >&2
    exit 1
fi

"$PROJECT/tests/test-dispatch.sh"

echo "CHECK_OK"
