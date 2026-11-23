#!/usr/bin/env bash
set -euo pipefail

SODIUM_DIR="$(cd "$(dirname "$(dirname "$0")")" && pwd)"
# Use ~/.local by default if /usr/local isn't writable
if [ -z "${INSTALL_PREFIX-}" ] && [ ! -w "/usr/local/bin" ]; then
    INSTALL_PREFIX="$HOME/.local"
else
    INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
fi
INSTALL_DIR="${INSTALL_DIR:-$INSTALL_PREFIX/bin}"
VSCODE=""
INSTALL_VSCODE=false

while [ $# -gt 0 ]; do
    case "$1" in
        --vscode) INSTALL_VSCODE=true ;;
        --prefix)
            [ $# -ge 2 ] || { echo "--prefix requires a directory" >&2; exit 1; }
            INSTALL_PREFIX="$2"
            shift
            ;;
        *) echo "usage: $0 [--prefix DIR] [--vscode]" >&2; exit 1 ;;
    esac
    shift
done

# ── Colors ──────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}==>${NC} $1"; }
ok()    { echo -e "${GREEN}  ✓${NC} $1"; }
err()   { echo -e "${RED}  ✗${NC} $1"; exit 1; }

# ── Check dependencies ──────────────────────────────────────────────
info "Checking dependencies..."
command -v cmake  >/dev/null 2>&1 || err "cmake is not installed"
command -v nasm   >/dev/null 2>&1 || err "nasm is not installed"
command -v ld     >/dev/null 2>&1 || err "ld is not installed"
ok "All build dependencies found"

# ── Build ───────────────────────────────────────────────────────────
info "Building Sodium compiler..."
cd "$SODIUM_DIR"
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | sed 's/^/  /'
cmake --build build 2>&1 | sed 's/^/  /'
ok "Build complete"

# ── Build runtime libraries ────────────────────────────────────────
info "Building runtime libraries..."
if command -v nasm >/dev/null 2>&1; then
    make -C "$SODIUM_DIR/sodium-rt" 2>&1 | sed 's/^/  /'
    ok "x86-64 runtime built"
else
    echo "  nasm not found — x86-64 runtime not built"
fi
if command -v riscv64-elf-gcc >/dev/null 2>&1; then
    make -C "$SODIUM_DIR/sodium-rt/riscv64" 2>&1 | sed 's/^/  /'
    ok "RISC-V runtime built"
else
    echo "  riscv64-elf-gcc not found — RISC-V runtime not built"
fi

# ── Install ─────────────────────────────────────────────────────────
info "Installing to $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
cp build/sodium "$INSTALL_DIR/sodium"
cp build/cyan-lsp "$INSTALL_DIR/cyan-lsp"
chmod +x "$INSTALL_DIR/sodium" "$INSTALL_DIR/cyan-lsp"
ok "Installed sodium → $INSTALL_DIR/sodium"
ok "Installed cyan-lsp → $INSTALL_DIR/cyan-lsp"

# Install runtime libraries alongside the binary
info "Installing runtime libraries..."
# Remove old runtime dir first to prevent nested sodium-rt/sodium-rt/ on reinstall
rm -rf "$INSTALL_DIR/sodium-rt"
cp -r "$SODIUM_DIR/sodium-rt" "$INSTALL_DIR/sodium-rt"
chmod -R +r "$INSTALL_DIR/sodium-rt"
ok "Installed sodium-rt → $INSTALL_DIR/sodium-rt/"

# Install cyanlib library alongside the binary
rm -rf "$INSTALL_DIR/cyanlib"
cp -r "$SODIUM_DIR/cyanlib" "$INSTALL_DIR/cyanlib"
chmod -R +r "$INSTALL_DIR/cyanlib"
ok "Installed cyanlib → $INSTALL_DIR/cyanlib/"

# ── VS Code extension ───────────────────────────────────────────────
if [ "$INSTALL_VSCODE" = true ]; then
    info "Installing VS Code extension..."
    if command -v code >/dev/null 2>&1; then
        # Install npm deps if needed
        if command -v npm >/dev/null 2>&1; then
            cd "$SODIUM_DIR/vscode-extension"
            if [ -f "package.json" ] && [ ! -d "node_modules" ]; then
                npm install 2>&1 | sed 's/^/  /'
            fi
            cd "$SODIUM_DIR"
        fi
        code --install-extension "$SODIUM_DIR/vscode-extension" 2>&1 | sed 's/^/  /' || \
            echo "  (install via: code --install-extension vscode-extension)"
        ok "VS Code extension installed"
    else
        echo "  'code' command not found. Install manually:"
        echo "    code --install-extension vscode-extension"
    fi
fi

# ── Verify ──────────────────────────────────────────────────────────
info "Verifying installation..."
[ -x "$INSTALL_DIR/sodium" ] || err "installed sodium is missing"
"$INSTALL_DIR/sodium" --version >/dev/null 2>&1 || err "installed sodium does not run"
ok "sodium → $INSTALL_DIR/sodium"
ok "cyan-lsp → $INSTALL_DIR/cyan-lsp"
echo ""
echo "Try it out (add $INSTALL_DIR to PATH if needed):"
echo "  echo 'function main() { return 42; }' > test.cyan"
echo "  $INSTALL_DIR/sodium test.cyan && ./test && echo \$?"
echo ""
ok "Installation complete!"
