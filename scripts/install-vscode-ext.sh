#!/usr/bin/env bash
# Install the Cyan VS Code extension.
# This is a convenience wrapper around install.sh --vscode.
#
# Run directly for a faster re-install of just the extension:
#   ./install-vscode-ext.sh

set -euo pipefail

SODIUM_DIR="$(cd "$(dirname "$0")" && pwd)"

# If the compiler is already built, skip the full build
if [ -f "$SODIUM_DIR/build/sodium" ]; then
    # Just install the extension
    EXT_DIR="${HOME}/.vscode/extensions/cyan-lsp-client"

    echo "Installing Cyan VS Code extension to ${EXT_DIR} ..."

    mkdir -p "${EXT_DIR}/syntaxes" "${EXT_DIR}/themes"

    cp "$SODIUM_DIR/vscode-extension/package.json"              "${EXT_DIR}/"
    cp "$SODIUM_DIR/vscode-extension/extension.js"              "${EXT_DIR}/"
    cp "$SODIUM_DIR/vscode-extension/language-configuration.json" "${EXT_DIR}/"
    cp "$SODIUM_DIR/vscode-extension/syntaxes"/*.json           "${EXT_DIR}/syntaxes/"
    cp "$SODIUM_DIR/vscode-extension/themes"/*.json             "${EXT_DIR}/themes/"

    cd "${EXT_DIR}"
    if [ ! -d node_modules ]; then
        npm install vscode-languageclient --silent 2>/dev/null || true
    fi

    echo ""
    echo "✅ Installed! Restart VS Code to activate."
else
    # Full build + install
    exec "$SODIUM_DIR/install.sh" --vscode
fi
