const vscode = require('vscode');
const path = require('path');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

/**
 * Activate the extension when a .cyan file is opened.
 * The LSP server implements diagnostics, completion, hover, go-to-definition,
 * document symbols, rename, references, signature help and semantic tokens;
 * the client enables each feature the server advertises at initialize.
 */
function activate(context) {
    const config = vscode.workspace.getConfiguration('cyan');
    let serverPath = config.get('lspPath', 'cyan-lsp');

    // Development convenience: if no explicit path is configured and a local
    // build exists in the workspace, prefer it over whatever is on PATH.
    if (!config.has('lspPath') && vscode.workspace.workspaceFolders) {
        const root = vscode.workspace.workspaceFolders[0].uri.fsPath;
        const local = path.join(root, 'build', 'cyan-lsp');
        const fs = require('fs');
        if (fs.existsSync(local)) serverPath = local;
    }

    const serverOptions = {
        command: serverPath,
        args: [],
        options: { stdio: ['pipe', 'pipe', 'pipe'] }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'cyan' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.cyan')
        },
        outputChannelName: 'Cyan Language Server'
    };

    client = new LanguageClient('cyan-lsp', 'Cyan Language Server', serverOptions, clientOptions);
    context.subscriptions.push(client.start());

    console.log('cyan-lsp: activated');
}

function deactivate() {
    return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
