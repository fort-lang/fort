'use strict';

/** Runs compiler checks and publishes their diagnostics in VS Code. */

const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const check = require('./lib/check');

// Use the release compiler in the development VM.
const COMPILER = '/vagrant/build/release/fort';

// Bound a stalled compiler and output from a large module closure.
const CHECK_TIMEOUT_MS = 60000;
const MAX_OUTPUT_BYTES = 16 * 1024 * 1024;

let collection = null;
let output = null;

// Event numbers order updates per published file.
// `runOf` identifies the latest check of an entry file.
// `publishedAt` records the event, entry file, folder, and search roots for each file.
// `closureOf` records the files read by each entry-file check.
const runOf = new Map();
const publishedAt = new Map();
const closureOf = new Map();
let events = 0;

/** Starts diagnostics and checks documents that are already open. */
function activate(context) {
  collection = vscode.languages.createDiagnosticCollection('fort');
  output = vscode.window.createOutputChannel('fort');
  context.subscriptions.push(collection, output);
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(run),
    vscode.workspace.onDidSaveTextDocument(run),
    vscode.workspace.onDidCloseTextDocument(forget)
  );
  // Activation follows the first open event, so check documents already open.
  for (const document of vscode.workspace.textDocuments) run(document);
}

/** Clears extension state when VS Code deactivates the extension. */
function deactivate() {
  runOf.clear();
  publishedAt.clear();
  closureOf.clear();
  pending.length = 0;
  recheckRunning = false;
}

// A fort file the editor holds as a file on disk, which is what the compiler
// can be pointed at.
function isFortFile(document) {
  return document.languageId === 'fort' && document.uri.scheme === 'file';
}

// A close clears the file and cancels its entry-file check.
// Keep the event record so an older check cannot repaint the file.
// Recheck files painted by the closed file. Their current diagnostics stay until answers arrive.
function forget(document) {
  if (!isFortFile(document)) return;
  const filePath = document.uri.fsPath;
  if (!publishedAt.has(filePath) && !runOf.has(filePath)) return;
  const published = publishedAt.get(filePath);
  runOf.delete(filePath);
  events += 1;
  publishedAt.set(filePath, {
    at: events,
    by: null,
    folder: published === undefined ? null : published.folder,
    roots: published === undefined ? [] : published.roots,
  });
  collection.delete(vscode.Uri.file(filePath));
  const departed = departedFrom(filePath, new Set());
  if (closureOf.has(filePath)) closureOf.set(filePath, new Set());
  if (published === undefined || published.folder === null) return;
  const closedAt = events;
  for (const file of departed) {
    if (file === filePath) continue;
    queueRecheck(file, published.folder, published.roots, filePath, closedAt);
  }
}

// Runs the compiler from the workspace folder through `tools/vm`.
// Quote the guest command because `tools/vm run` passes it to a shell.
function spawnCheck(folder, relativePath, roots, done) {
  const tool = path.join(folder, 'tools', 'vm');
  let guest = COMPILER + ' --check --json';
  for (const root of roots) guest += ' -I ' + shellQuote(root);
  guest += ' ' + shellQuote(relativePath);
  const args = ['run', guest];
  const options = { cwd: folder, timeout: CHECK_TIMEOUT_MS, maxBuffer: MAX_OUTPUT_BYTES };
  childProcess.execFile(tool, args, options, (error, stdout, stderr) => {
    done({ command: tool + ' run ' + shellQuote(guest), error, stdout, stderr });
  });
}

// One word for a POSIX shell. Single quotes take everything literally, and a
// single quote inside them is closed, escaped and reopened.
function shellQuote(word) {
  return "'" + word.split("'").join("'\\''") + "'";
}

// One check of the document the editor hands over. A file outside every
// workspace folder is skipped. `tools/vm run` works in the directory matching
// its own, and there is nothing to say about a file that has none.
function run(document) {
  if (!isFortFile(document)) return;
  const filePath = document.uri.fsPath;
  const folder = folderOf(document.uri);
  if (folder === null) return;
  startCheck(filePath, folder, knownRoots(filePath, folder), null);
}

// Returns prior search roots for this file and workspace folder.
// The file directory is already the compiler's first root, so omit it from `-I`.
// A file without a prior closure gets no inferred project root.
function knownRoots(filePath, folder) {
  const published = publishedAt.get(filePath);
  if (published === undefined || published.folder !== folder) return [];
  const own = ownRoot(filePath, folder);
  return published.roots.filter((root) => root !== own);
}

// One check of `filePath`, named relative to the workspace folder `folder`. The
// compiler runs in a child process and its answer arrives in the callback, so
// nothing waits on the VM. The publishing that follows does read each answered
// file from disk, which is the text the columns of the document are counted in.
//
// An open and a save start a check here, and so does a file leaving a closure.
// The three are the same event: a run of the compiler over one file, with a
// number of its own. `roots` are the `-I` directories the run carries, which an
// open and a save leave empty and a re-check fills (see `rootsFor`). `onDone`
// is called when the answer has been dealt with, whatever it was, which is how
// the queue of re-checks knows the slot is free.
function startCheck(filePath, folder, roots, onDone) {
  events += 1;
  const generation = events;
  runOf.set(filePath, generation);
  spawnCheck(folder, path.relative(folder, filePath), roots, (result) => {
    try {
      if (runOf.get(filePath) !== generation) return;
      const answer = check.parseDocument(result.stdout);
      if (answer === null) {
        report(result);
        return;
      }
      publish(filePath, folder, generation, roots, answer);
    } finally {
      if (onDone !== null) onDone();
    }
  });
}

// The workspace folder the file belongs to, or null when it belongs to none.
function folderOf(uri) {
  const folder = vscode.workspace.getWorkspaceFolder(uri);
  return folder ? folder.uri.fsPath : null;
}

// The diagnostics of every file of the closure that lies in the workspace
// folder, which clears the squiggles of a file that is now clean. The
// columns are byte columns of the text the compiler read. That text is the file
// on disk and not a buffer edited since, so that is what they are converted
// against.
//
// Recheck each file that leaves this closure. Keep its old diagnostics until
// the answer arrives.
function publish(entryPath, folder, generation, roots, document) {
  const byFile = check.diagnosticsByFile(document, folder, fileLines);
  // Record roots for later checks of each file in this closure.
  const childRoots = rootsFor(entryPath, folder, roots);
  for (const [file, items] of byFile) {
    if (!mayReplace(file, generation)) continue;
    publishedAt.set(file, { at: generation, by: entryPath, folder, roots: childRoots });
    collection.set(vscode.Uri.file(file), items.map(toVsDiagnostic));
  }
  // A closure contains files read by the compiler, not all diagnostic paths.
  const closure = new Set(check.filesUnder(document, folder));
  const departed = departedFrom(entryPath, closure);
  closureOf.set(entryPath, closure);
  if (departed.length === 0) return;
  for (const file of departed) queueRecheck(file, folder, childRoots, entryPath, generation);
}

// Returns roots that preserve the entry-file module search.
// The entry run adds its directory once. Later runs pass that root unchanged.
// Do not add each module directory because no original closure searched it.
function rootsFor(entryPath, folder, roots) {
  if (roots.length > 0) return roots;
  return [ownRoot(entryPath, folder)];
}

// The directory of a file, named as a search root relative to the workspace
// folder. The folder itself is `.`, since an empty word is no path.
function ownRoot(filePath, folder) {
  const own = path.relative(folder, path.dirname(filePath));
  return own === '' ? '.' : own;
}

// Whether this run may write about `file`. A later event may have spoken about
// it already. That word is the newer one and stands. It comes from a check of
// the file itself, from a check of another file whose closure holds it, or from
// a close of it.
function mayReplace(file, generation) {
  const published = publishedAt.get(file);
  return published === undefined || published.at <= generation;
}

// Returns files that left this entry file's closure.
// A close passes an empty set, so all files leave that closure.
function departedFrom(entryPath, stillNamed) {
  const before = closureOf.get(entryPath);
  const departed = [];
  if (before === undefined) return departed;
  for (const file of before) {
    if (!stillNamed.has(file)) departed.push(file);
  }
  return departed;
}

// Run departure checks one at a time to bound VM connections.
// Start a queued check only while its source entry still paints the file.
// Also drop it when a newer event has updated the file.
const pending = [];
let recheckRunning = false;

function queueRecheck(filePath, folder, roots, by, at) {
  pending.push({ filePath, folder, roots, by, at });
  drainRechecks();
}

function drainRechecks() {
  while (!recheckRunning && pending.length > 0) {
    const next = pending.shift();
    const published = publishedAt.get(next.filePath);
    if (published === undefined || published.by !== next.by) continue;
    if (published.at > next.at) continue;
    recheckRunning = true;
    startCheck(next.filePath, next.folder, next.roots, () => {
      recheckRunning = false;
      drainRechecks();
    });
  }
}

// The lines of a file's text, or null when it cannot be read, which leaves the
// conversion its ASCII fallback.
function fileLines(absolutePath) {
  try {
    return check.splitLines(fs.readFileSync(absolutePath, 'utf8'));
  } catch (error) {
    return null;
  }
}

// Standalone notes use information severity. Notes on an error become related information.
function toVsDiagnostic(item) {
  const severity =
    item.severity === 'note'
      ? vscode.DiagnosticSeverity.Information
      : vscode.DiagnosticSeverity.Error;
  const diagnostic = new vscode.Diagnostic(toVsRange(item.range), item.message, severity);
  diagnostic.source = 'fort';
  diagnostic.relatedInformation = item.notes.map(
    (note) =>
      new vscode.DiagnosticRelatedInformation(
        new vscode.Location(vscode.Uri.file(note.file), toVsRange(note.range)),
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

// Keep current diagnostics when a run returns no document. Report the command and failure.
function report(result) {
  output.appendLine(result.command);
  output.appendLine('fort: no diagnostics from that run: ' + reason(result.error));
  const stderr = result.stderr;
  if (typeof stderr === 'string' && stderr.trim() !== '') output.appendLine(stderr.trim());
}

// Only a numeric `code` is an exit status. Other errors describe spawn or signal failures.
function reason(error) {
  if (!error) return 'it wrote no JSON document';
  if (typeof error.code === 'number') return 'it exited ' + String(error.code);
  return error.message;
}

/** Returns state sizes for tests. */
function bookkeeping() {
  return {
    runOf: runOf.size,
    publishedAt: publishedAt.size,
    closureOf: closureOf.size,
    pending: pending.length,
  };
}

module.exports = { activate, bookkeeping, deactivate };
