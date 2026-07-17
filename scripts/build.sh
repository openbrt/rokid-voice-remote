#!/bin/sh
set -eu

PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$PROJECT/scripts/check.sh"
"$PROJECT/scripts/build-on-sdk.sh"
"$PROJECT/scripts/package.sh"
