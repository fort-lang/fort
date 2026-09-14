'use strict';

// The fort extension: the compiler's diagnostics, and nothing else.
//
// One run of `fort --check --json <file>` answers a document opened and a
// document saved (D20.1, D20.2), its diagnostics are published into one
// collection, and a run that produces no document leaves the last ones standing
// and says why in the output channel. There is no setting and no language
// server: the compiler is the whole interface. The only state beside the
// collection is the ordering of the events, which is what keeps an older
// answer from painting over a newer one.
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

// Every event takes a number, and three maps read it. A check answers about a
// whole closure and not only about the file it was given, so the ordering is
// per published file.
//
// `runOf` is the run last started for each checked file. It drops a superseded
// run whole, its failure included, so a dead run cannot report over a live one.
//
// `publishedAt` is what was last said about each file: `{at, by, folder,
// roots}`, the number of the event, the checked file whose run painted it, the
// workspace folder that run worked in and the search roots a later check of the
// file must carry. `by` is null when the last event painted nothing, which today
// is a close. `at` keeps an older run of one file from repainting a newer answer
// about another file its closure also names. `by` says whose paint is on the
// file, and only that run asks again about it. `roots` is what the file needs to
// be checked the way that closure checked it, and it outlives a close: the
// paint goes, the root the closure searched is still the root it searched.
//
// `closureOf` is the set of files each check published about. Its difference
// with the next check of that same file is the set of files that have left that
// closure. Each of those is checked in its own right.
const runOf = new Map();
const publishedAt = new Map();
const closureOf = new Map();
let events = 0;

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
  closureOf.clear();
  pending.length = 0;
  recheckRunning = false;
}

// A fort file the editor holds as a file on disk, which is what the compiler
// can be pointed at.
function isFortFile(document) {
  return document.languageId === 'fort' && document.uri.scheme === 'file';
}

// A close drops the file's squiggles and any run of it still in flight. It must
// not drop the file's place in the ordering. Deleting the `publishedAt` entry
// would let a run older than the close repaint the file the close has just
// cleared. The close is therefore an event like a run, and the newest word about
// that file, painted by nobody.
//
// A close also ends the closure that file's checks walked. The squiggles a
// check of `main.ft` put on `mathx.ft` came from that walk, and after the close
// nothing supports them: the file that carried them is shut and no run of it
// will speak again. A close is therefore a check that names nothing, which is
// `departedFrom` with an empty set, and every file of that closure leaves it at
// once. Each is checked in its own right, exactly as a deleted `import` has it
// checked. The extension decides nothing about such a file and the old
// squiggles stand until the answer arrives (`spec/toolchain.md` 9.2).
//
// No new event orders those runs. A close takes its number here, before it
// queues anything, and each run takes the next number when the queue starts it,
// as every other run does. A run older than the close therefore cannot repaint
// what the close settled, and the answers arrive under `mayReplace` like any
// other (T-106).
//
// The file that was closed is not asked about. Its paint is gone and the reader
// has shut it, so there is nothing to ask for. `by` alone would drop it in the
// ordinary case, the close having set it to null. The `at` half of the
// precondition in `drainRechecks` drops it in the other one, where the file is
// opened again while its own re-check waits in the queue. This line says it
// directly rather than leaving it to those two, and no test can tell it from
// them (T-106).
//
// A file this window knows nothing about is left alone. It was never checked
// and never painted, so there is nothing to clear and nothing to order. A file
// outside every workspace folder is exactly that file. The test is the
// bookkeeping itself and not the folder. A file painted while its folder was
// open is therefore still cleared if that folder has left the workspace.
//
// Each map then holds one entry per file this window has checked or published
// about. None of them holds one entry per event. The keys are file paths, so a
// file opened and closed a hundred times is the one entry it already had. The
// emptied `closureOf` entry is one this file already had, and a file that was
// never checked as an entry gets none. Two closes in a row therefore ask once:
// the second finds an empty closure. `deactivate` drops all three with the
// window.
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

// How the compiler is reached, which is the one thing about it that could be
// otherwise: `tools/vm run` of the workspace folder, spawned there, with the
// file named relative to it. The compiler names each file the path it opened it
// by, the entry file as given on the command line (`toolchain.md` 2, D14.2), so
// a relative path comes back relative and the answer needs no host-to-guest
// mapping in either direction. The guest command is one argument that
// `tools/vm run` hands to a shell, so the file is quoted for it.
// `done` is given the command line as the reader could paste it, the spawn error
// if there was one, and the two output streams.
function spawnCheck(folder, relativePath, roots, done) {
  // `tools/vm` of the workspace folder itself, which costs about a tenth of a
  // second per run and needs no configuration of its own.
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

// The `-I` directories an opened or saved file is checked with. A module is not
// a file that checks the same way on its own: `util/strings.ft` imports its
// sibling as `util.chars` and resolves that import only from the directory its
// entry file sits in (D9.2). Opening or saving it therefore paints `module
// 'util.chars' not found` on correct code, unless the run carries that
// directory.
//
// The window knows that directory whenever a check has already painted the
// file, which is what `roots` records, so an open and a save carry exactly what
// a re-check of the same file carries. The file's own directory is dropped from
// the list, since D9.2 puts it first in every case and no `-I` can add it or
// remove it. A file this window has never published about carries nothing, as
// before: no check has walked a closure that holds it, so nothing here knows
// which directory is the root of its project. Inventing one is a project model,
// which no decision states; D20.5 fixes only what the compiler's modules must be
// for a language server to be possible and leaves the server's own structure
// undecided (T-111).
//
// The roots are relative to the workspace folder the run worked in, so they are
// used only for a run in that same folder. A folder added to the workspace
// under another one takes the files below it, and a path relative to the old
// folder names nothing from the new one.
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
// folder, which clears the squiggles of a file that is now clean (D20.2). The
// columns are byte columns of the text the compiler read. That text is the file
// on disk and not a buffer edited since, so that is what they are converted
// against.
//
// A file that has left this closure is checked in its own right after that. The
// client publishes what a document says and decides nothing else
// (`spec/toolchain.md` 9.2). It therefore neither clears such a file nor leaves
// a guess on it. It asks the compiler about that file and publishes the answer.
// The old squiggles stand until the answer arrives, as they do after any save.
function publish(entryPath, folder, generation, roots, document) {
  const byFile = check.diagnosticsByFile(document, folder, fileLines);
  // What a later check of any file of this closure must carry to see what this
  // check saw. It is recorded for every file the check publishes about, the
  // entry file included, and read again by an open, by a save and by a
  // re-check.
  const childRoots = rootsFor(entryPath, folder, roots);
  for (const [file, items] of byFile) {
    if (!mayReplace(file, generation)) continue;
    publishedAt.set(file, { at: generation, by: entryPath, folder, roots: childRoots });
    collection.set(vscode.Uri.file(file), items.map(toVsDiagnostic));
  }
  // The closure is what the compiler read and not what was published. A
  // diagnostic may name a file the compiler never read (`spec/toolchain.md`
  // 9.2). Such a file is in no closure, so it leaves none.
  const closure = new Set(check.filesUnder(document, folder));
  const departed = departedFrom(entryPath, closure);
  closureOf.set(entryPath, closure);
  if (departed.length === 0) return;
  for (const file of departed) queueRecheck(file, folder, childRoots, entryPath, generation);
}

// The `-I` directories a later check of any file of this closure is checked
// with. A module is not a file that can be checked on its own. The first search
// root is always the directory of the file the compiler was given (D9.2). A
// module in `util/` that imports its sibling as `util.chars` therefore resolves
// that import only from the directory the entry file of its closure sits in.
// That directory goes on the command line as `-I`.
//
// The roots a run carries are the roots of that entry file, and a run hands
// them on unchanged. A run that carries none is the entry file itself: nothing
// has painted it, the reader pointed the editor at it, and its own directory is
// the root of the closure it starts. Only such a run puts a directory into the
// list.
//
// A run must not add its own directory to what it hands on. That directory is
// the first root of that one run by D9.2 and of no other. Adding it would send
// the root of `a/x.ft` to a check of `b/y.ft`, which no closure searched, and
// `b.z` in `y.ft` would then read `a/b/z.ft` -- an answer from a root the
// closure never had, which is the defect this whole record exists to stop
// (T-111 finding 3). The list therefore holds one directory and does not grow.
//
// This is faithful wherever one directory is enough. It is not faithful in
// general: the directory of the file being checked stays the first root, and no
// option removes it. A layout that repeats a path prefix, `util/util/chars.ft`
// beside `util/chars.ft`, makes this run read the nearer file. Only a project
// model would settle that, and no decision states one (T-092, T-111).
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

// The files this same check painted last time and does not name now, which is
// what a removed `import` leaves behind. The squiggles of `mathx.ft` came from a
// walk of `main.ft`'s closure, and that walk no longer reaches `mathx.ft`. Each
// of them is checked in its own right, and `stillNamed` is the closure this run
// did reach.
//
// Which of them is asked about is `drainRechecks`'s to say, and it says it in
// one place. A run asks again about the paint it put there itself and about no
// other paint. An error that a check of `mathx.ft` itself reported already has a
// run behind it. A file the user has closed carries a `by` of null.
//
// The cost is one run for each file this entry painted and no longer names.
// Each answer paints those files in the name of the file that was checked. This
// entry therefore cannot name them again until an `import` brings them back.
// The chain that a re-check starts is finite for the same reason. A run of
// `mathx.ft` asks again only about what `mathx.ft` itself painted, and the
// import relation is acyclic (D9.5).
//
// A close is this same set with an empty `stillNamed`: the closed file names
// nothing any more, so every file of its closure has left it (`forget`, T-106).
function departedFrom(entryPath, stillNamed) {
  const before = closureOf.get(entryPath);
  const departed = [];
  if (before === undefined) return departed;
  for (const file of before) {
    if (!stillNamed.has(file)) departed.push(file);
  }
  return departed;
}

// The re-checks a departure asks for, one at a time. A closure can be wide:
// `src/fort/main.ft` reads 23 files of this repository, so deleting one
// `import` departs 22 files at once. Each run opens its own ssh connection to
// the guest, `tools/vm` multiplexing nothing. 22 at once on a six-CPU VM is a
// storm the reader gains nothing from. The queue runs them one after another,
// off the UI thread. An open and a save still go out at once, the reader being
// the one waiting for those.
//
// A queued file takes its number when its run starts, like every other event.
// The precondition is tested there and not when the file was queued, and it has
// two halves.
//
// A run asks again about its own paint: `by` must still name the event that
// queued this. Another check may have painted the file since, or the user may
// have closed it, and in both cases nothing is asked.
//
// And the paint must still be the paint that was there when the file was
// queued: `at` must be no newer than the number the queueing event had. `by`
// alone does not say this. A close of `main.ft` queues the files its closure
// painted, and re-opening `main.ft` checks it again and writes `by` back onto
// exactly those files. That answer is the newest word about them, and the
// queued run, which takes its number after it, would replace a live error with
// its own older reading and lose it until the next save. The window is the
// drain of the queue, which is 22 runs wide for `src/fort/main.ft` (T-106).
//
// The same half holds for a departure, where nothing has spoken about the file
// since the check that queued it, so `at` is older than the queueing event's
// number and the run goes out.
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

// The size of each map, for the test that measures the bookkeeping. VS Code
// calls `activate` and `deactivate` and nothing else.
function bookkeeping() {
  return {
    runOf: runOf.size,
    publishedAt: publishedAt.size,
    closureOf: closureOf.size,
    pending: pending.length,
  };
}

module.exports = { activate, bookkeeping, deactivate };
