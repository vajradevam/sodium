const vscode = require('vscode');
const path = require('path');

/**
 * Activate the extension when a .cyan file is opened.
 */
function activate(context) {
    // The LSP server binary — bundled with the compiler.
    // Assumes ~/myc/sodium/build/cyan-lsp exists.
    const isDev = true;
    let serverPath;
    if (isDev) {
        // Development: point directly to the build output
        serverPath = path.join(__dirname, '..', '..', '..', 'myc', 'sodium', 'build', 'cyan-lsp');
    } else {
        // Production: on PATH or specified in settings
        const config = vscode.workspace.getConfiguration('cyan');
        serverPath = config.get('lspPath', 'cyan-lsp');
    }

    // Server options: spawn cyan-lsp as a child process
    const serverOptions = {
        command: serverPath,
        args: [],
        options: { stdio: ['pipe', 'pipe', 'pipe'] }
    };

    // Client options: only interested in .cyan files
    const clientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'cyan' }
        ],
        // Full-text sync (change = 1)
        synchronize: {
            textDocument: { change: 1 }
        }
    };

    // Create and start the client
    const client = new vscode.LanguageClient(
        'cyan-lsp',
        'Cyan Language Server',
        serverOptions,
        clientOptions
    );

    context.subscriptions.push(client.start());

    console.log('cyan-lsp: activated');
}

function deactivate() {
    // client.stop() is called automatically
    console.log('cyan-lsp: deactivated');
}

module.exports = { activate, deactivate };
