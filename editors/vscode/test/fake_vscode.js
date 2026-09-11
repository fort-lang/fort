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
const fs = require('node:fs');
const os = require('node:os');

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

class MarkdownString {
  constructor(value) {
    this.value = value;
  }
}

class Hover {
  constructor(contents, range) {
    this.contents = contents;
    this.range = range;
  }
}

// The settings the extension reads, with the defaults of package.json already
// resolved to a VM that is not there.
const SETTINGS = {
  'vm.sshConfig': '/tmp/fort-ssh-config',
  'vm.host': 'default',
  compiler: '/vagrant/build/debug/fort',
  stdDir: '/vagrant/build/debug/std',
  includeDirs: [],
};

function createApi(state) {
  const uriFile = (fsPath) => ({ scheme: 'file', fsPath, toString: () => 'file://' + fsPath });
  return {
    Uri: { file: uriFile },
    Range,
    Diagnostic,
    Location,
    DiagnosticRelatedInformation,
    MarkdownString,
    Hover,
    // The values VS Code gives them, which a test asserts by number.
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 },
    StatusBarAlignment: { Left: 1 },
    languages: {
      createDiagnosticCollection() {
        return {
          set(uri, items) {
            if (state.throwOnPublish) {
              state.throwOnPublish = false;
              throw new Error('the collection is gone');
            }
            state.published.push(uri.fsPath);
            state.diagnostics.set(uri.fsPath, items);
          },
          dispose() {},
        };
      },
      registerHoverProvider(language, provider) {
        state.hoverProvider = provider;
        return { dispose() {} };
      },
      registerDefinitionProvider(language, provider) {
        state.definitionProvider = provider;
        return { dispose() {} };
      },
    },
    window: {
      createStatusBarItem() {
        return {
          text: '',
          tooltip: '',
          show() {
            state.status.visible = true;
            state.status.text = this.text;
            state.status.tooltip = this.tooltip;
          },
          hide() {
            state.status.visible = false;
          },
          dispose() {},
        };
      },
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
      onDidSaveTextDocument(handler) {
        state.saveHandlers.push(handler);
        return { dispose() {} };
      },
      getWorkspaceFolder() {
        return state.workspaceFolder ? { uri: uriFile(state.workspaceFolder) } : undefined;
      },
      getConfiguration() {
        return { get: (key) => state.settings[key] };
      },
    },
  };
}

// Load a fresh copy of the extension with the editor and the spawn faked.
function install(options) {
  const state = {
    settings: Object.assign({}, SETTINGS, (options || {}).settings),
    workspaceFolder: (options || {}).workspaceFolder || '',
    diagnostics: new Map(),
    published: [],
    output: [],
    status: { visible: false, text: '', tooltip: '' },
    saveHandlers: [],
    openDocuments: [],
    calls: [],
    throwOnPublish: false,
  };
  const api = createApi(state);
  const childProcess = {
    execFile(command, args, execOptions, callback) {
      state.calls.push({ command, args, options: execOptions, callback });
      return {};
    },
  };
  // The temporary directory is faked too, so a test can hand the extension the
  // `/var/folders/<...>/T` of macOS on any machine.
  const tmpdir = (options || {}).tmpdir;
  const fakeOs = tmpdir === undefined ? os : Object.assign({}, os, { tmpdir: () => tmpdir });
  const original = Module._load;
  Module._load = function fakeLoad(request, parent, isMain) {
    if (request === 'vscode') return api;
    if (request === 'child_process') return childProcess;
    if (request === 'os') return fakeOs;
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

// A document as the editor would hand it over: `version` counts every change
// VS Code has applied to the buffer, `isDirty` says it has unsaved ones, and
// `text` overrides what the buffer holds, which is the file on disk otherwise.
function document(filePath, options) {
  const settings = options || {};
  return {
    uri: { scheme: 'file', fsPath: filePath },
    languageId: filePath.endsWith('.ft') ? 'fort' : 'plaintext',
    isDirty: settings.dirty === true,
    version: settings.version === undefined ? 1 : settings.version,
    getText: () =>
      settings.text === undefined
        ? fs.existsSync(filePath)
          ? fs.readFileSync(filePath, 'utf8')
          : ''
        : settings.text,
  };
}

// Open a document in the fake editor, replacing the one of that path.
function open(state, doc) {
  const at = state.openDocuments.findIndex((each) => each.uri.fsPath === doc.uri.fsPath);
  if (at < 0) state.openDocuments.push(doc);
  else state.openDocuments[at] = doc;
  return doc;
}

// Save a file, which in an editor means the buffer is open and now clean.
function save(state, filePath, options) {
  const doc = open(state, document(filePath, options));
  for (const handler of state.saveHandlers) handler(doc);
  return doc;
}

// Answer the pending run. `code` is the exit status of a process that ran;
// `spawnFailure` is a spawn that never happened, which Node reports with a
// *string* `code` such as `'ENOENT'` -- the very case the extension separates
// from an exit status; and `timeout` is the run execFile killed, which carries
// no code at all.
function complete(call, result) {
  const stdout = result.stdout === undefined ? '' : result.stdout;
  const stderr = result.stderr === undefined ? '' : result.stderr;
  let error = null;
  if (result.spawnFailure) {
    error = new Error('spawn ssh ' + result.spawnFailure);
    error.code = result.spawnFailure;
    error.errno = -2;
    error.syscall = 'spawn ssh';
  } else if (result.timeout) {
    error = new Error('Command failed: ssh');
    error.killed = true;
    error.signal = 'SIGTERM';
  } else if (result.code) {
    error = new Error('Command failed');
    error.code = result.code;
  }
  call.callback(error, stdout, stderr);
}

module.exports = { complete, document, install, open, save };
