#!/bin/bash
# Run the compiler with -O0 explicitly. Kept as a file so COMPILER can point at
# it from scripts that take an executable path (D-032: -O1 is now the default).
cd "$(dirname "$0")/.." || exit 1
exec ./build/sodium -O0 "$@"
