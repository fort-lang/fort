'use strict';

// A stand-in for the editor, so the glue in `extension.js` can be driven.
//
// `extension.js` is the only file that requires `vscode`, and no editor exists
// while the tests run, so this module answers that require with the small part
// of the API the extension uses and answers `child_process` with a spawn that
// hands the test the callback. What it cannot check is that VS Code calls these
// functions the way it is documented to; that is what the manual smoke test in
// `editors/README.md` is for.

const Module = require('node:module');
const path = require('node:path');

class Range {
  constructor(startLine, startCharacter, endLine, endCharacter) {
    this.start = { line: startLine, character: startCharacter };
    this.end = { line: endLine, character: endCharacter };
  }
}

class Diagnostic {
  constructor(range, message, severity) {
    this.range = range;
    this.message = message;
    this.severity = severity;
    this.source = '';
    this.relatedInformation = [];
  }
}

class Location {
  constructor(uri, range) {
    this.uri = uri;
    this.range = range;
  }
}

class DiagnosticRelatedInformation {
  constructor(location, message) {
    this.location = location;
    this.message = message;
  }
}

function createApi(state) {
  const uriFile = (fsPath) => ({ scheme: 'file', fsPath, toString: () => 'file://' + fsPath });
  return {
    Uri: { file: uriFile },
    Range,
    Diagnostic,
    Location,
    DiagnosticRelatedInformation,
    // The values VS Code gives them, which a test asserts by number.
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
    languages: {
      createDiagnosticCollection() {
        return {
          set(uri, items) {
            state.published.push(uri.fsPath);
            state.diagnostics.set(uri.fsPath, items);
          },
          delete(uri) {
            state.cleared.push(uri.fsPath);
            state.diagnostics.delete(uri.fsPath);
          },
          dispose() {},
        };
      },
    },
    window: {
      createOutputChannel() {
        return {
          appendLine(line) {
            state.output.push(line);
          },
          dispose() {},
        };
      },
    },
    workspace: {
      textDocuments: state.openDocuments,
      // One folder, since what the extension asks is which folder a file
      // belongs to and a file outside it belongs to none.
      getWorkspaceFolder(uri) {
        if (state.workspaceFolder === null) return undefined;
        const inside = uri.fsPath.startsWith(state.workspaceFolder + path.sep);
        return inside ? { uri: uriFile(state.workspaceFolder) } : undefined;
      },
      onDidOpenTextDocument(handler) {
        state.openHandlers.push(handler);
        return { dispose() {} };
      },
      onDidSaveTextDocument(handler) {
        state.saveHandlers.push(handler);
        return { dispose() {} };
      },
      onDidCloseTextDocument(handler) {
        state.closeHandlers.push(handler);
        return { dispose() {} };
      },
    },
  };
}

// Load a fresh copy of the extension with the editor and the spawn faked.
// `options.documents` are the ones the editor already holds open when it
// activates, which is how VS Code starts an extension that `onLanguage:fort`
// woke, and `options.workspaceFolder` is the folder it opened, null for a file
// opened outside every folder.
function install(options) {
  const settings = options === undefined ? {} : options;
  const state = {
    workspaceFolder: settings.workspaceFolder === undefined ? null : settings.workspaceFolder,
    diagnostics: new Map(),
    published: [],
    cleared: [],
    output: [],
    openHandlers: [],
    saveHandlers: [],
    closeHandlers: [],
    openDocuments: settings.documents === undefined ? [] : settings.documents.slice(),
    calls: [],
  };
  const api = createApi(state);
  const childProcess = {
    execFile(command, args, execOptions, callback) {
      state.calls.push({ command, args, options: execOptions, callback });
      return {};
    },
  };
  const original = Module._load;
  Module._load = function fakeLoad(request, parent, isMain) {
    if (request === 'vscode') return api;
    if (request === 'child_process') return childProcess;
    return original.call(this, request, parent, isMain);
  };
  delete require.cache[require.resolve('../extension')];
  let extension = null;
  try {
    extension = require('../extension');
  } finally {
    Module._load = original;
  }
  extension.activate({ subscriptions: [] });
  return { extension, state };
}

// A document as the editor would hand it over: its path, its language and its
// scheme, which is all the extension reads of one. `scheme` is `file` unless a
// test says otherwise, since a diff view hands over the same path under another
// one.
function document(filePath, options) {
  const settings = options || {};
  const scheme = settings.scheme === undefined ? 'file' : settings.scheme;
  return {
    uri: { scheme, fsPath: filePath },
    languageId: settings.languageId === undefined ? languageOf(filePath) : settings.languageId,
  };
}

function languageOf(filePath) {
  return filePath.endsWith('.ft') ? 'fort' : 'plaintext';
}

// Open a file in the fake editor, replacing the document of that path.
function open(state, filePath, options) {
  const doc = document(filePath, options);
  const at = state.openDocuments.findIndex((each) => each.uri.fsPath === filePath);
  if (at < 0) state.openDocuments.push(doc);
  else state.openDocuments[at] = doc;
  for (const handler of state.openHandlers) handler(doc);
  return doc;
}

// Save a file, which in an editor means the buffer is open and now clean.
function save(state, filePath, options) {
  const doc = document(filePath, options);
  for (const handler of state.saveHandlers) handler(doc);
  return doc;
}

function close(state, filePath, options) {
  const doc = document(filePath, options);
  const at = state.openDocuments.findIndex((each) => each.uri.fsPath === filePath);
  if (at >= 0) state.openDocuments.splice(at, 1);
  for (const handler of state.closeHandlers) handler(doc);
  return doc;
}

// Answer a pending run. `code` is the exit status of a process that ran;
// `spawnFailure` is a spawn that never happened, which Node reports with a
// *string* `code` such as `'ENOENT'` -- the very case the extension separates
// from an exit status; and `timeout` is the run execFile killed, which carries
// no code at all.
function complete(call, result) {
  const answer = result === undefined ? {} : result;
  const stdout = answer.stdout === undefined ? '' : answer.stdout;
  const stderr = answer.stderr === undefined ? '' : answer.stderr;
  let error = null;
  if (answer.spawnFailure) {
    error = new Error('spawn tools/vm ' + answer.spawnFailure);
    error.code = answer.spawnFailure;
    error.errno = -2;
    error.syscall = 'spawn tools/vm';
  } else if (answer.timeout) {
    error = new Error('Command failed: tools/vm run');
    error.killed = true;
    error.signal = 'SIGTERM';
  } else if (answer.code) {
    error = new Error('Command failed');
    error.code = answer.code;
  }
  call.callback(error, stdout, stderr);
}

module.exports = { close, complete, document, install, open, save };
