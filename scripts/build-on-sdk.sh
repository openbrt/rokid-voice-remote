#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
HOST=${ROKID_BUILD_HOST:-100}
SDK=${ROKID_SDK_ROOT:-/home/csc/rokid_src/home/csc/rokid_src}
TOOLCHAIN=$SDK/toolchain/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin
CC=$TOOLCHAIN/aarch64-linux-gnu-gcc
READELF=$TOOLCHAIN/aarch64-linux-gnu-readelf
STRIP=$TOOLCHAIN/aarch64-linux-gnu-strip
BSA=$SDK/vendor/broadcom/brcm-bsa/3rdparty/embedded/bsa_examples/linux
INCLUDE=$BSA/libbsa/include
LIBRARY=$BSA/libbsa/build/arm64/sharedlib

case "$HOST" in ''|*[!A-Za-z0-9_.@-]*) echo "unsafe ROKID_BUILD_HOST" >&2; exit 2 ;; esac
case "$SDK" in ''|*[!A-Za-z0-9_./-]*) echo "unsafe ROKID_SDK_ROOT" >&2; exit 2 ;; esac

remote_tmp=$(ssh "$HOST" 'mktemp -d /tmp/rokid-voice-remote.XXXXXX')
case "$remote_tmp" in /tmp/rokid-voice-remote.*) ;; *) echo "unsafe remote temp: $remote_tmp" >&2; exit 2 ;; esac

cleanup() {
    # shellcheck disable=SC2029 -- remote_tmp is prefix-validated above.
    ssh "$HOST" "rm -rf -- '$remote_tmp'" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM HUP

scp "$PROJECT/src/voice_remote_hid.c" "$HOST:$remote_tmp/voice_remote_hid.c" >/dev/null

# shellcheck disable=SC2029 -- every interpolated path is validated or constant.
ssh "$HOST" "'$CC' -std=c11 -O2 -Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2 -I'$INCLUDE' '$remote_tmp/voice_remote_hid.c' -L'$LIBRARY' -Wl,-z,relro,-z,now -lbsa -lpthread -o '$remote_tmp/voice_remote_hid' && '$STRIP' --strip-unneeded '$remote_tmp/voice_remote_hid' && '$READELF' -h '$remote_tmp/voice_remote_hid' | grep -E 'Class:|Machine:|Type:'"

mkdir -p "$PROJECT/build"
scp "$HOST:$remote_tmp/voice_remote_hid" "$PROJECT/build/voice_remote_hid" >/dev/null
chmod 0755 "$PROJECT/build/voice_remote_hid"

echo "BUILD_OK $PROJECT/build/voice_remote_hid"
