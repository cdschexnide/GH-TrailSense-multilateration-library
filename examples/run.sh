#!/usr/bin/env sh
# Run the built CLI on the sample input. Build first:
#   cmake -S . -B build && cmake --build build
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)"
CLI="$DIR/../build/trailsense-triangulate"
if [ ! -x "$CLI" ]; then
    echo "build the CLI first: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi
cat "$DIR/sample-input.json" | "$CLI"
