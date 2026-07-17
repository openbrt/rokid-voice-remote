#!/bin/sh
set -eu

PROJECT=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
"$PROJECT/scripts/stage-web-firmware.sh"
cd "$PROJECT/web"
npm ci
npm run build
