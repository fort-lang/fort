'use strict';

// The fort extension: diagnostics on save, hover and go-to-definition, all
// answered by one batch run of `fort --check --json --index` in the VM
// (D20.1, D20.2, D20.3). The index is a batch answer about the file as it was
// saved, so between two saves the answers are stale by design; the status bar
// says when the last run did not produce one.

const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const vscode = require('vscode');

const cache = require('./lib/cache');
const commands = require('./lib/command');
const diagnostics = require('./lib/diagnostics');
const documents = require('./lib/document');
const paths = require('./lib/paths');
const positions = require('./lib/positions');
const symbols = require('./lib/symbols');
const vmdir = require('./lib/vmdir');

// A hung ssh must not leave the user without an answer forever, and the index
// of a large closure is far past execFile's default 1 MiB of output.
const CHECK_TIMEOUT_MS = 120000;
const MAX_OUTPUT_BYTES = 64 * 1024 * 1024;

// A long session must not hold the index of every closure it ever checked, so
// both maps below are bounded (lib/cache.js).
const MAX_REMEMBERED_FILES = 512;

// The records of the last successful check, per file of its closure, and the
// run that published each file, which is what keeps two checks finishing out of
// order from publishing the older answer last.
const indexByFile = new Map();
const publishedAt = new Map();
const checkedBuffer = new Map();

let diagnosticCollection = null;
let statusItem = null;
let output = null;
let lastStatusMessage = '';
let lastRun = 0;
let controlDir = null;
const running = new Map();

function activate(context) {
  diagnosticCollection = vscode.languages.createDiagnosticCollection('fort');
  statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 0);
  output = vscode.window.createOutputChannel('fort');
  context.subscriptions.push(diagnosticCollection, statusItem, output);
  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument((document) => {
      if (document.languageId === 'fort' || paths.isFortPath(document.uri.fsPath)) {
        check(document.uri.fsPath);
      }
    })
  );
  context.subscriptions.push(
    vscode.languages.registerHoverProvider('fort', { provideHover }),
    vscode.languages.registerDefinitionProvider('fort', { provideDefinition })
  );
}

function deactivate() {
  indexByFile.clear();
  publishedAt.clear();
  checkedBuffer.clear();
  running.clear();
  if (controlDir !== null) {
    try {
      fs.rmSync(controlDir, { recursive: true, force: true });
    } catch (error) {
      // A socket another window still holds open is not this one's to mourn.
    }
    controlDir = null;
  }
}

// ---- running the compiler ---------------------------------------------------

function settingsFor(filePath) {
  const uri = vscode.Uri.file(filePath);
  const folder = vscode.workspace.getWorkspaceFolder(uri);
  const workspaceFolder = folder ? folder.uri.fsPath : path.dirname(filePath);
  const config = vscode.workspace.getConfiguration('fort', uri);
  const raw = {
    sshConfig: config.get('vm.sshConfig'),
    host: config.get('vm.host'),
    compiler: config.get('compiler'),
    stdDir: config.get('stdDir'),
    includeDirs: config.get('includeDirs'),
  };
  // `.vagrant/ssh-config` lives in the VM directory, which is the main checkout
  // when the workspace is a worktree (AGENTS.md, Environment).
  const vmDir = vmdir.vmDirectory(workspaceFolder, process.env, readTextFile);
  return commands.resolveSettings(raw, workspaceFolder, vmDir);
}

// A file's text, or null when it cannot be read -- a `.git` directory rather
// than a worktree's `.git` file included.
function readTextFile(file) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (error) {
    return null;
  }
}

// One check of `filePath`. The compiler runs in a child process and every
// answer arrives in its callback, so the UI thread is never blocked; a save
// while a check is in flight queues exactly one more run of the same file.
function check(filePath) {
  const state = running.get(filePath);
  if (state && state.running) {
    state.rerun = true;
    return;
  }
  running.set(filePath, { running: true, rerun: false });
  lastRun += 1;
  const generation = lastRun;
  const settings = settingsFor(filePath);
  const command = commands.checkCommand(settings, filePath, controlDirectory());
  // The line the output channel shows is the line the user can paste into a
  // terminal, so the remote word is quoted there exactly as it is passed.
  output.appendLine(command.command + ' ' + commands.shellJoin(command.args));
  const options = { timeout: CHECK_TIMEOUT_MS, maxBuffer: MAX_OUTPUT_BYTES };
  childProcess.execFile(command.command, command.args, options, (error, stdout, stderr) => {
    // A throw anywhere in the answer must still release the file: the bookkeeping
    // lives in the `finally`, or a single failure would leave `running` set and
    // that file would never be checked again, silently.
    try {
      finish(filePath, generation, {
        error: spawnError(error),
        status: exitStatus(error),
        stdout,
        stderr,
      });
    } catch (failure) {
      output.appendLine('fort: the extension could not publish the answer: ' + String(failure));
    } finally {
      const state = running.get(filePath);
      running.delete(filePath);
      if (state && state.rerun) check(filePath);
    }
  });
}

// Where the multiplexing socket lives. Two things constrain it. The socket name
// `fort-%C` is a hash of the local host, the remote host, the port and the user,
// so it is predictable: a local user who owns the directory first could leave a
// socket there and `ControlMaster=auto` would attach to their multiplexer, which
// would both leak the command line and let forged JSON come back as diagnostics
// and jump targets. And a Unix domain socket path is short (lib/command.js),
// which `os.tmpdir()` is not on macOS. So the directory is created by
// `mkdtemp`, whose name nobody can predict and which fails rather than reusing
// an existing path -- `mkdir` with `recursive: true` accepts a directory that is
// already there whatever its mode, its owner or whether it is a symlink, so its
// `mode` argument guarantees nothing -- inside whichever base a socket path fits
// in. It is made once per activation and removed on deactivate.
const CONTROL_PREFIX = 'fort-';
const CONTROL_SAMPLE = CONTROL_PREFIX + 'XXXXXX';

function controlDirectory() {
  if (controlDir !== null) return controlDir;
  const candidate = commands.controlDirectory(
    path.join(os.tmpdir(), CONTROL_SAMPLE),
    path.join('/tmp', CONTROL_SAMPLE)
  );
  try {
    controlDir = fs.mkdtempSync(path.join(path.dirname(candidate), CONTROL_PREFIX));
  } catch (error) {
    // ssh then reports the socket it could not bind and the run reads as a VM
    // that is unreachable, which is what it is.
    output.appendLine('fort: could not create a control directory: ' + String(error));
    controlDir = path.dirname(candidate);
  }
  return controlDir;
}

// execFile reports a failed spawn, a timeout and a signal death through the
// same error, and a plain non-zero exit through it too; only the first three
// mean the connection itself failed.
function spawnError(error) {
  if (!error) return null;
  if (typeof error.code === 'number') return null;
  return error;
}

function exitStatus(error) {
  if (!error) return 0;
  return typeof error.code === 'number' ? error.code : -1;
}

function finish(filePath, generation, result) {
  const verdict = documents.classifyResult(result);
  if (verdict.kind === 'verdict') {
    apply(filePath, generation, verdict.document);
    clearStatus();
  } else {
    report(verdict.message, result.stderr);
  }
}

// The diagnostics of the document, published for every file of the closure and
// cleared for the files that no longer have any (D20.2). The text of each file
// is read here, because a byte column converts only against the line it names.
function apply(entryPath, generation, document) {
  const baseDir = path.dirname(entryPath);
  const texts = new Map();
  // The columns of the document are byte columns of the text the compiler read,
  // which is the file on disk as it stood at check time and not a buffer some
  // other window has edited since (D20.2).
  const linesOf = (file) => {
    if (!texts.has(file)) texts.set(file, readFileLines(paths.resolveDocumentPath(file, baseDir)));
    return texts.get(file);
  };
  const byFile = diagnostics.diagnosticsByFile(document, linesOf);
  for (const [file, items] of byFile) {
    const absolute = paths.resolveDocumentPath(file, baseDir);
    // A check of another file may have answered about this one later; its
    // answer is the newer one and stands (lib/cache.js).
    if (!cache.isNewer(publishedAt, absolute, generation)) continue;
    cache.putGeneration(publishedAt, absolute, generation, MAX_REMEMBERED_FILES);
    const uri = vscode.Uri.file(absolute);
    diagnosticCollection.set(uri, items.map((item) => toVsDiagnostic(item, baseDir)));
    const records = symbols.recordsForFile(document.symbols, file);
    cache.put(indexByFile, absolute, records, MAX_REMEMBERED_FILES);
    // What the buffer of this file was when the compiler read it, so a later
    // hover can tell an answer about the text on screen from an older one.
    cache.put(checkedBuffer, absolute, bufferStateOf(absolute), MAX_REMEMBERED_FILES);
  }
}

// The text of a file on disk, or null when it cannot be read, which leaves the
// conversion its ASCII fallback.
function readFileLines(absolutePath) {
  try {
    return positions.splitLines(fs.readFileSync(absolutePath, 'utf8'));
  } catch (error) {
    return null;
  }
}

// The text of a file as the reader sees it: the open buffer when there is one
// and the file on disk otherwise. This is for showing a position to the user,
// where the buffer is what is on screen; a column that came out of the compiler
// converts against `readFileLines` instead.
function readLines(absolutePath) {
  const open = openDocumentFor(absolutePath);
  return open === null ? readFileLines(absolutePath) : positions.splitLines(open.getText());
}

function openDocumentFor(absolutePath) {
  for (const open of vscode.workspace.textDocuments) {
    if (open.uri.scheme === 'file' && open.uri.fsPath === absolutePath) return open;
  }
  return null;
}

// The version of the buffer of a file and whether it had unsaved edits, or null
// when no window held it open.
function bufferStateOf(absolutePath) {
  const open = openDocumentFor(absolutePath);
  if (open === null) return null;
  return { version: open.version, dirty: open.isDirty === true };
}

// The two severities the document has: `error` on everything the compiler
// reports as one, there being no warnings in v1, and `note` on a note that
// follows no error and so stands on its own (D20.2). A note is not an error and
// must not be painted as one.
function toVsSeverity(severity) {
  return severity === 'note'
    ? vscode.DiagnosticSeverity.Information
    : vscode.DiagnosticSeverity.Error;
}

function toVsDiagnostic(item, baseDir) {
  const diagnostic = new vscode.Diagnostic(
    toVsRange(item.range),
    item.message,
    toVsSeverity(item.severity)
  );
  diagnostic.source = 'fort';
  diagnostic.relatedInformation = item.notes.map(
    (note) =>
      new vscode.DiagnosticRelatedInformation(
        new vscode.Location(
          vscode.Uri.file(paths.resolveDocumentPath(note.file, baseDir)),
          toVsRange(note.range)
        ),
        note.message
      )
  );
  return diagnostic;
}

function toVsRange(range) {
  return new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character
  );
}

// ---- the status bar ---------------------------------------------------------

// Exit 2 with an empty stdout is a crash and not a verdict, and a dead ssh is
// no answer at all (D20.2), so the diagnostics already published stay and the
// failure is said once, in the status bar, with the detail in the output
// channel.
function report(message, stderr) {
  if (message !== lastStatusMessage) {
    output.appendLine(message);
    if (typeof stderr === 'string' && stderr.trim() !== '') output.appendLine(stderr.trim());
  }
  lastStatusMessage = message;
  statusItem.text = '$(warning) fort: check failed';
  statusItem.tooltip = message + ' (the diagnostics shown are from the last successful check)';
  statusItem.show();
}

function clearStatus() {
  lastStatusMessage = '';
  statusItem.hide();
}

// ---- hover and definition ---------------------------------------------------

// Both are served from the last successful check whose closure contained the
// file (toolchain.md 9.2), so a file never checked has no answer.
// The record under the cursor with the lines it was found against, or null. The
// text is split once per request and only for a file some check has covered.
function lookup(document, position) {
  const records = indexByFile.get(document.uri.fsPath);
  if (!records) return null;
  const lines = positions.splitLines(document.getText());
  const record = symbols.recordAtPosition(records, position, lines);
  return record === null ? null : { record, lines };
}

function provideHover(document, position) {
  const found = lookup(document, position);
  if (!found) return null;
  const { record, lines } = found;
  // The answer is the one a past check gave. It is presented as current only
  // when that check read this very text; anything else -- an edit, a failed
  // check since, a file reloaded from disk, a file checked while another window
  // held unsaved edits -- is marked, since the extension cannot know it is
  // fresh and must not imply it (toolchain.md 9.2).
  const fresh = cache.isFresh(checkedBuffer.get(document.uri.fsPath), document.version);
  const markdown = new vscode.MarkdownString(symbols.hoverText(record, !fresh));
  return new vscode.Hover(markdown, toVsRange(positions.toEditorRange(record, lines)));
}

function provideDefinition(document, position) {
  const found = lookup(document, position);
  if (!found) return null;
  const baseDir = path.dirname(document.uri.fsPath);
  const lineSource = (file) => readLines(paths.resolveDocumentPath(file, baseDir));
  const target = symbols.definitionLocation(found.record, lineSource);
  if (!target) return null;
  const absolute = paths.resolveDocumentPath(target.file, baseDir);
  return new vscode.Location(vscode.Uri.file(absolute), toVsRange(target.range));
}

module.exports = { activate, deactivate };
