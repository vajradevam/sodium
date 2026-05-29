#!/usr/bin/env bash
# Install the Cyan VS Code extension.
# Run from the project root (sodium/).

set -euo pipefail

EXT_DIR="${HOME}/.vscode/extensions/cyan-lsp-client"

echo "Installing Cyan VS Code extension to ${EXT_DIR} ..."

mkdir -p "${EXT_DIR}/syntaxes" "${EXT_DIR}/themes"

cp vscode-extension/package.json           "${EXT_DIR}/"
cp vscode-extension/extension.js           "${EXT_DIR}/"
cp vscode-extension/language-configuration.json "${EXT_DIR}/"
cp vscode-extension/syntaxes/*.json        "${EXT_DIR}/syntaxes/"
cp vscode-extension/themes/*.json          "${EXT_DIR}/themes/"

# Install npm dependencies
cd "${EXT_DIR}"
if [ ! -d node_modules ]; then
    npm install vscode-languageclient --silent 2>/dev/null
fi

echo ""
echo "✅ Installed! Restart VS Code to activate."
echo ""
echo "Then:"
echo "  1. Open any .cyan file → syntax highlighting works immediately"
echo "  2. A Cyan Dark theme is available (Ctrl+K Ctrl+T → 'Cyan Dark')"
echo "  3. Diagnostics, completions, hover work via cyan-lsp"
echo ""
echo "NOTE: Make sure cyan-lsp is on your PATH, or edit extension.js"
echo "      to set the correct path."
