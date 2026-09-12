'use strict';

// The fort extension: the compiler's diagnostics, and nothing else.
//
// One run of `fort --check --json <file>` answers a document opened and a
// document saved (D20.1, D20.2), its diagnostics are published into one
// collection, and a run that produces no document leaves the last ones standing
// and says why in the output channel. There is no setting, no language server
// and no state beyond the collection: the compiler is the whole interface.
//
// VS Code runs on the host and the compiler runs in the development VM, and the
// repository already owns that crossing: `tools/vm run <command>` runs a command
// in the guest directory matching the host working directory
// (notes/environment.md 1). `spawnCheck` below is the only function that knows
// any of this.

const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const check = require('./lib/check');

// The compiler: the release build in the guest, which is where everything this
// project builds is built (notes/environment.md 1). It is a constant rather than
// a setting because there is one right answer. It finds its own standard library
// in the `std` directory beside it (D14.1), so the command needs no other
// argument, and imports resolve from the importing file's directory (D9.2).
const COMPILER = '/vagrant/build/release/fort';

// A compiler that hangs must not leave the reader without an answer for ever,
// and a check document is far smaller than execFile's default 1 MiB only until
// a closure grows.
const CHECK_TIMEOUT_MS = 60000;
const MAX_OUTPUT_BYTES = 16 * 1024 * 1024;

let collection = null;
let output = null;

// Every run takes a number, and two maps read it, because a check answers about
// a whole closure and not only about the file it was given. `runOf` is the run
// last started for each checked file, which drops a superseded run whole --
// its failure included, so a dead run cannot report over a live one. And
// `publishedAt` is the run each file's diagnostics were last published from,
// which is what keeps an older run of one file from repainting a newer answer
// about another file its closure also names.
const runOf = new Map();
const publishedAt = new Map();
let runs = 0;

function activate(context) {
  collection = vscode.languages.createDiagnosticCollection('fort');
  output = vscode.window.createOutputChannel('fort');
  context.subscriptions.push(collection, output);
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(run),
    vscode.workspace.onDidSaveTextDocument(run),
    vscode.workspace.onDidCloseTextDocument(forget)
  );
  // A `.ft` file is what activates the extension, so its open event has already
  // fired by the time this runs and the first file would otherwise go unchecked.
  for (const document of vscode.workspace.textDocuments) run(document);
}

function deactivate() {
  runOf.clear();
  publishedAt.clear();
}

// A fort file the editor holds as a file on disk, which is what the compiler
// can be pointed at.
function isFortFile(document) {
  return document.languageId === 'fort' && document.uri.scheme === 'file';
}

function forget(document) {
  if (!isFortFile(document)) return;
  runOf.delete(document.uri.fsPath);
  publishedAt.delete(document.uri.fsPath);
  collection.delete(vscode.Uri.file(document.uri.fsPath));
}

// How the compiler is reached, which is the one thing about it that could be
// otherwise: `tools/vm run` of the workspace folder, spawned there, with the
// file named relative to it. The compiler names each file the path it opened it
// by, the entry file as given on the command line (`toolchain.md` 2, D14.2), so
// a relative path comes back relative and the answer needs no host-to-guest
// mapping in either direction. The guest command is one argument that
// `tools/vm run` hands to a shell, so the file is quoted for it.
// `done` is given the command line as the reader could paste it, the spawn error
// if there was one, and the two output streams.
function spawnCheck(folder, relativePath, done) {
  // `tools/vm` of the workspace folder itself, which costs about a tenth of a
  // second per run and needs no configuration of its own.
  const tool = path.join(folder, 'tools', 'vm');
  const guest = COMPILER + ' --check --json ' + shellQuote(relativePath);
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

// One check of the document. The compiler runs in a child process and its answer
// arrives in the callback, so nothing waits on the VM; the publishing that
// follows does read each answered file from disk, which is the text the columns
// of the document are counted in. A file outside every workspace folder is
// skipped: `tools/vm run` works in the directory matching its own, and there is
// nothing to say about a file that has none.
function run(document) {
  if (!isFortFile(document)) return;
  const filePath = document.uri.fsPath;
  const folder = folderOf(document.uri);
  if (folder === null) return;
  runs += 1;
  const generation = runs;
  runOf.set(filePath, generation);
  spawnCheck(folder, path.relative(folder, filePath), (result) => {
    if (runOf.get(filePath) !== generation) return;
    const answer = check.parseDocument(result.stdout);
    if (answer === null) {
      report(result);
      return;
    }
    publish(folder, generation, answer);
  });
}

// The workspace folder the file belongs to, or null when it belongs to none.
function folderOf(uri) {
  const folder = vscode.workspace.getWorkspaceFolder(uri);
  return folder ? folder.uri.fsPath : null;
}

// The diagnostics of every file of the closure that lies in the workspace
// folder, which clears the squiggles of a file that is now clean (D20.2). Their
// columns are byte columns of the text the compiler read, which is the file on
// disk and not a buffer edited since, so that is what they are converted
// against.
function publish(folder, generation, document) {
  const byFile = check.diagnosticsByFile(document, folder, fileLines);
  for (const [file, items] of byFile) {
    // A later run may have answered about this file already -- a check of the
    // file itself, or of another file whose closure holds it -- and that answer
    // is the newer one and stands.
    const published = publishedAt.get(file);
    if (published !== undefined && published > generation) continue;
    publishedAt.set(file, generation);
    collection.set(vscode.Uri.file(file), items.map(toVsDiagnostic));
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

// The two severities the document has: `error` on everything the compiler
// reports as one, there being no warnings in v1, and `note` on a note that
// follows no error and so stands on its own, which is not an error and must not
// be painted as one (D20.2). A note that belongs to an error is that error's
// related information and not a diagnostic of its own: the two often carry the
// same range, and two squiggles over one span, with two rows in Problems, say
// nothing about being one diagnostic.
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

// No document is no answer, not an answer of "no errors" -- the VM is down, the
// binary is missing because nobody ran `tools/vm build release`, or the compiler
// died (D20.1). The diagnostics already on screen therefore stay, and the
// command and its stderr go to the output channel, where the line is the one the
// reader can paste into a terminal.
function report(result) {
  output.appendLine(result.command);
  output.appendLine('fort: no diagnostics from that run: ' + reason(result.error));
  const stderr = result.stderr;
  if (typeof stderr === 'string' && stderr.trim() !== '') output.appendLine(stderr.trim());
}

// execFile reports a failed spawn, a timeout and a signal death through the same
// error as a non-zero exit, and only a numeric `code` is an exit status.
function reason(error) {
  if (!error) return 'it wrote no JSON document';
  if (typeof error.code === 'number') return 'it exited ' + String(error.code);
  return error.message;
}

module.exports = { activate, deactivate };
