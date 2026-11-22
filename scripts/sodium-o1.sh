#!/bin/bash
# Run the discoverable test suite with -O1 via COMPILER=scripts/sodium-o1.sh.
# Kept as a file (rather than inline flags) because run_tests.sh takes an
# executable path in COMPILER.
cd "$(dirname "$0")/.." || exit 1
exec ./build/sodium -O1 "$@"
