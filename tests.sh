#!/bin/sh
set -eu

# Resolve project files relative to this script, regardless of the caller's directory.
CDPATH= cd -- "$(dirname -- "$0")"

# Enable the suite even if the existing build was configured without tests.
./build.sh -Dtests=true
exec meson test -C build --print-errorlogs "$@"
