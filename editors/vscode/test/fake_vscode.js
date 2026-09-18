'use strict';

/** Provides the VS Code and child-process fakes for extension tests. */

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

// The folders a window holds, from the one path or the several a test gives.
function foldersOf(setting) {
  if (setting === undefined || setting === null) return [];
  return Array.isArray(setting) ? setting.slice() : [setting];
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
      // Match the nearest workspace folder when folders are nested.
      getWorkspaceFolder(uri) {
        let best = null;
        for (const folder of state.folders) {
          if (!uri.fsPath.startsWith(folder + path.sep)) continue;
          if (best === null || folder.length > best.length) best = folder;
        }
        return best === null ? undefined : { uri: uriFile(best) };
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

/** Loads a fresh extension with fake editor and child-process APIs. */
function install(options) {
  const settings = options === undefined ? {} : options;
  const state = {
    folders: foldersOf(settings.workspaceFolder),
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

/** Creates the document fields that the extension reads. */
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

/** Opens or replaces a document and sends its open event. */
function open(state, filePath, options) {
  const doc = document(filePath, options);
  const at = state.openDocuments.findIndex((each) => each.uri.fsPath === filePath);
  if (at < 0) state.openDocuments.push(doc);
  else state.openDocuments[at] = doc;
  for (const handler of state.openHandlers) handler(doc);
  return doc;
}

/** Sends a save event for a document. */
function save(state, filePath, options) {
  const doc = document(filePath, options);
  for (const handler of state.saveHandlers) handler(doc);
  return doc;
}

/** Closes a document and sends its close event. */
function close(state, filePath, options) {
  const doc = document(filePath, options);
  const at = state.openDocuments.findIndex((each) => each.uri.fsPath === filePath);
  if (at >= 0) state.openDocuments.splice(at, 1);
  for (const handler of state.closeHandlers) handler(doc);
  return doc;
}

/** Completes one fake compiler run with output, an exit status, or a spawn failure. */
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
