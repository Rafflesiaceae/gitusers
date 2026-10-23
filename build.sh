#!/bin/sh
set -eu

# Resolve project files relative to this script, regardless of the caller's directory.
CDPATH= cd -- "$(dirname -- "$0")"

# Apply setup options on repeat builds as well as the initial configuration.
if [ -f build/meson-private/coredata.dat ]; then
    set -- --reconfigure "$@"
fi

meson setup build --native-file musl.ini "$@"
meson compile -C build
