#!/usr/bin/env bash
set -euo pipefail

SODIUM_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
INSTALL_DIR="${INSTALL_DIR:-$INSTALL_PREFIX/bin}"
VSCODE="${1:-}"
INSTALL_VSCODE=false

if [ "$VSCODE" = "--vscode" ]; then
    INSTALL_VSCODE=true
fi

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
cp -r "$SODIUM_DIR/sodium-rt" "$INSTALL_DIR/sodium-rt"
chmod -R +r "$INSTALL_DIR/sodium-rt"
ok "Installed sodium-rt → $INSTALL_DIR/sodium-rt/"

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
echo "  sodium = $(which sodium)"
echo "  cyan-lsp = $(which cyan-lsp)"
echo ""
echo "Try it out:"
echo "  echo 'return(42);' > test.cyan"
echo "  sodium test.cyan && ./out && echo \$?"
echo ""
ok "Installation complete!"
