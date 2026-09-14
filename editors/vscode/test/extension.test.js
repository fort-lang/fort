'use strict';

// The glue: an open and a save each run the compiler through `tools/vm run` in a
// child process, its document is published into the one collection, a close
// clears the file, and a run that answers with no document leaves the last
// diagnostics standing (D20.1, D20.2). The editor and the spawn are faked
// (test/fake_vscode.js), so what is exercised here is the extension's own case
// analysis, with no editor, no VM and no compiler.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const fake = require('./fake_vscode');

const FIXTURES = path.join(__dirname, 'fixtures');
const PROJECT = path.join(FIXTURES, 'project');
const MAIN = path.join(PROJECT, 'main.ft');
const MATHX = path.join(PROJECT, 'mathx.ft');
const NOTES_FT = path.join(FIXTURES, 'notes.ft');
const LEXICAL_FT = path.join(FIXTURES, 'lexical.ft');
const DOCUMENT = fs.readFileSync(path.join(FIXTURES, 'check-document.json'), 'utf8');
const NOTES = fs.readFileSync(path.join(FIXTURES, 'notes-document.json'), 'utf8');
const LEXICAL = fs.readFileSync(path.join(FIXTURES, 'lexical-document.json'), 'utf8');
const COMPILER = '/vagrant/build/release/fort';
const VM = path.join(PROJECT, 'tools', 'vm');
// The guest command as `tools/vm run` receives it: one argument for a shell,
// with the path relative to the workspace folder and quoted for it.
const GUEST = COMPILER + " --check --json 'main.ft'";
// The guest command of a re-check: the directory of the file whose check
// painted the departed file goes on it as a search root (D9.2).
const RECHECK_MATHX = COMPILER + " --check --json -I '.' 'mathx.ft'";
// Severities as VS Code numbers them, which the fake copies.
const ERROR = 0;
const INFORMATION = 2;
const CLEAN = JSON.stringify({
  version: 1,
  files: ['main.ft', 'mathx.ft'],
  diagnostics: [],
  symbols: [],
});
// Two answers about one closure, in the shape check-document.json has: a check
// of main.ft reporting an error in the module it imports, and a check of that
// module alone finding it clean.
const MAIN_WITH_STALE_MATHX = JSON.stringify({
  version: 1,
  files: ['main.ft', 'mathx.ft'],
  diagnostics: [
    {
      file: 'mathx.ft',
      line: 5,
      col: 8,
      end_line: 5,
      end_col: 11,
      severity: 'error',
      message: 'the error the user has just fixed',
      notes: [],
    },
    {
      file: 'main.ft',
      line: 6,
      col: 31,
      end_line: 6,
      end_col: 35,
      severity: 'error',
      message: "unknown name 'nope'",
      notes: [],
    },
  ],
  symbols: [],
});
const MATHX_CLEAN = JSON.stringify({
  version: 1,
  files: ['mathx.ft'],
  diagnostics: [],
  symbols: [],
});
// A check of mathx.ft that finds the error in it, which is the newer answer the
// two guards below must not overwrite.
const MATHX_BROKEN = JSON.stringify({
  version: 1,
  files: ['mathx.ft'],
  diagnostics: [
    {
      file: 'mathx.ft',
      line: 5,
      col: 8,
      end_line: 5,
      end_col: 11,
      severity: 'error',
      message: 'the error mathx.ft has of its own',
      notes: [],
    },
  ],
  symbols: [],
});
// A closure of three files, clean, and the same closure with an error in the
// third. A close of main.ft asks about two files, so the queue is busy while
// the second waits, which is what an event arriving during the drain needs.
const THREE = JSON.stringify({
  version: 1,
  files: ['main.ft', 'mathx.ft', 'other.ft'],
  diagnostics: [],
  symbols: [],
});
const THREE_WITH_BROKEN_OTHER = JSON.stringify({
  version: 1,
  files: ['main.ft', 'mathx.ft', 'other.ft'],
  diagnostics: [
    {
      file: 'other.ft',
      line: 1,
      col: 1,
      end_line: 1,
      end_col: 4,
      severity: 'error',
      message: 'the error the open has just reported',
      notes: [],
    },
  ],
  symbols: [],
});
// main.ft after its `import mathx;` is deleted: the closure is one file, and
// mathx.ft is no longer in it.
const MAIN_ALONE = JSON.stringify({
  version: 1,
  files: ['main.ft'],
  diagnostics: [],
  symbols: [],
});

// A window opened on the fixture project, which is the workspace folder every
// test below works in.
function open(options) {
  return fake.install(Object.assign({ workspaceFolder: PROJECT }, options));
}

// A saved main.ft whose check answered with the fixture document.
function checked() {
  const harness = open();
  fake.save(harness.state, MAIN);
  fake.complete(harness.state.calls[0], { stdout: DOCUMENT, code: 1 });
  return harness.state;
}

// ---- what is run ------------------------------------------------------------

// The whole crossing: `tools/vm run` of the workspace folder, spawned there,
// with the file named relative to it, since the compiler echoes the path it was
// given and the answer then resolves against that same folder on the host.
test('a save runs the compiler through tools/vm run and nothing else', () => {
  const { state } = open();
  fake.save(state, MAIN);
  assert.equal(state.calls.length, 1);
  assert.equal(state.calls[0].command, VM);
  assert.deepEqual(state.calls[0].args, ['run', GUEST]);
  assert.equal(state.calls[0].options.cwd, PROJECT);
  assert.ok(state.calls[0].options.timeout > 0);
  assert.ok(state.calls[0].options.maxBuffer > 1024 * 1024);
  // Nothing is published until the compiler has answered.
  assert.equal(state.diagnostics.size, 0);
});

test('a file in a sub-directory is named relative to the folder', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, MAIN);
  assert.equal(state.calls[0].command, path.join(FIXTURES, 'tools', 'vm'));
  assert.deepEqual(state.calls[0].args, [
    'run',
    COMPILER + " --check --json 'project/main.ft'",
  ]);
});

// `tools/vm run` hands its argument to a shell in the guest, so a quote in a
// file name must not end the word the compiler is given.
test('a file name holding a quote is quoted for the guest shell', () => {
  const { state } = open();
  fake.save(state, path.join(PROJECT, "it's.ft"));
  assert.equal(state.calls[0].args[1], COMPILER + " --check --json 'it'\\''s.ft'");
});

test('opening a fort file checks it', () => {
  const { state } = open();
  fake.open(state, MAIN);
  assert.equal(state.calls.length, 1);
  assert.deepEqual(state.calls[0].args, ['run', GUEST]);
});

// `onLanguage:fort` is what wakes the extension, so the file that woke it is
// already open and its open event has been and gone.
test('activation checks the fort files already open', () => {
  const documents = [fake.document(MAIN), fake.document(path.join(PROJECT, 'notes.txt'))];
  const { state } = open({ documents });
  assert.equal(state.calls.length, 1);
  assert.deepEqual(state.calls[0].args, ['run', GUEST]);
});

test('a document that is not fort is not checked', () => {
  const { state } = open();
  fake.save(state, path.join(PROJECT, 'notes.txt'));
  fake.open(state, path.join(PROJECT, 'notes.txt'));
  assert.equal(state.calls.length, 0);
});

// A diff view hands over a document of the same path under another scheme, and
// the compiler can only be pointed at a file on disk.
test('a fort document that is not a file on disk is not checked', () => {
  const { state } = open();
  fake.save(state, MAIN, { scheme: 'git' });
  assert.equal(state.calls.length, 0);
});

// `tools/vm run` works in the guest directory matching its own, so a file with
// no workspace folder has nowhere to be checked from.
test('a file outside every workspace folder is not checked', () => {
  const nowhere = fake.install();
  fake.save(nowhere.state, MAIN);
  assert.equal(nowhere.state.calls.length, 0);
  // A window opened on the project folder says nothing about a file beside it.
  const { state } = open();
  fake.save(state, LEXICAL_FT);
  assert.equal(state.calls.length, 0);
});

// ---- what is published ------------------------------------------------------

test('a save the compiler rejects publishes its diagnostics', () => {
  const state = checked();
  const items = state.diagnostics.get(MAIN);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, "unknown name 'nope'");
  assert.equal(items[0].severity, ERROR);
  assert.equal(items[0].source, 'fort');
  // The range is the converted one: line 6 of main.ft holds two multi-byte
  // characters before the name (D20.2, D20.4).
  assert.deepEqual(items[0].range.start, { line: 5, character: 27 });
  assert.deepEqual(items[0].range.end, { line: 5, character: 31 });
});

// The closure holds six standard library files, whose paths are the guest's:
// they are dropped rather than published against a path the host cannot open.
test('every file of the folder is published, so a clean one is cleared', () => {
  const state = checked();
  assert.deepEqual(state.published, [MAIN, MATHX]);
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

test('a check with no diagnostic at all clears the file', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: DOCUMENT, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: CLEAN });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
});

// One diagnostic with its note attached, and not two overlapping squiggles: in
// this real document the note carries the same range as its error.
test('a note becomes related information of its error', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, NOTES_FT);
  fake.complete(state.calls[0], { stdout: NOTES, code: 1 });
  const items = state.diagnostics.get(NOTES_FT);
  assert.equal(items.length, 1);
  assert.equal(items[0].severity, ERROR);
  assert.equal(items[0].relatedInformation.length, 1);
  const related = items[0].relatedInformation[0];
  assert.match(related.message, /looked for/);
  assert.equal(related.location.uri.fsPath, NOTES_FT);
  assert.deepEqual(related.location.range, items[0].range);
});

// A note that follows no error is a diagnostic of its own and is shown as
// information rather than as another error (D20.2).
test('a standalone note is published as information', () => {
  const standalone = JSON.stringify({
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'main.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 7,
        severity: 'note',
        message: 'on its own',
        notes: [],
      },
    ],
    symbols: [],
  });
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: standalone, code: 1 });
  const items = state.diagnostics.get(MAIN);
  assert.equal(items[0].severity, INFORMATION);
  assert.deepEqual(items[0].relatedInformation, []);
});

// An error whose file is the standard library in the guest is dropped. Its note
// is dropped with it, even when it points into the workspace folder. 9.2
// licenses dropping the diagnostics of a file the editor cannot open, and
// nothing else. A note that follows an error is not the standalone note of
// D20.2. Nothing is painted, and nothing is invented.
test('a note of a dropped error is dropped with it', () => {
  const outside = JSON.stringify({
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: '/vagrant/build/release/std/io.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 2,
        severity: 'error',
        message: 'conflicting declarations of print',
        notes: [
          {
            file: 'main.ft',
            line: 1,
            col: 1,
            end_line: 1,
            end_col: 7,
            message: 'previous declaration here',
          },
        ],
      },
    ],
    symbols: [],
  });
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: outside, code: 1 });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
});

test('closing a file clears its diagnostics', () => {
  const state = checked();
  fake.close(state, MAIN);
  assert.deepEqual(state.cleared, [MAIN]);
  assert.equal(state.diagnostics.has(MAIN), false);
  // The file that was only published about, not closed, keeps its entry.
  assert.equal(state.diagnostics.has(MATHX), true);
});

test('closing a file that is not fort clears nothing', () => {
  const state = checked();
  fake.close(state, path.join(PROJECT, 'notes.txt'));
  assert.deepEqual(state.cleared, []);
});

// ---- when there is no answer ------------------------------------------------

// No document is no answer and never "no errors" (D20.1): the squiggles on
// screen are the last thing the compiler said and they stay until it says
// something else.
test('a run that produces no document leaves the diagnostics standing', () => {
  const state = checked();
  const before = state.published.length;
  fake.complete(state.calls[0], { stdout: '', stderr: 'fort: internal error\n', code: 2 });
  assert.equal(state.diagnostics.get(MAIN).length, 1);
  assert.equal(state.published.length, before);
});

test('a failed run writes the command and the stderr to the output channel', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: '', stderr: 'fort: internal error\n', code: 2 });
  assert.equal(state.output[0], VM + " run '" + GUEST.split("'").join("'\\''") + "'");
  assert.match(state.output[1], /exited 2/);
  assert.equal(state.output[2], 'fort: internal error');
});

// Nobody ran `tools/vm build release`, so there is no binary to spawn: Node
// reports that with a string code rather than an exit status.
test('a compiler that cannot be spawned is reported with its reason', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { spawnFailure: 'ENOENT' });
  assert.equal(state.output[0], VM + " run '" + GUEST.split("'").join("'\\''") + "'");
  assert.match(state.output[1], /ENOENT/);
  assert.equal(state.diagnostics.size, 0);
});

test('a run killed by the timeout is reported too', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { timeout: true });
  assert.match(state.output[1], /Command failed/);
});

test('stdout that is not a document is no answer whatever the status', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: 'usage: fort [options] <file>\n' });
  assert.equal(state.diagnostics.size, 0);
  assert.match(state.output[1], /no JSON document/);
});

test('a run with no stderr says only what it can', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { code: 2 });
  assert.equal(state.output.length, 2);
});

// ---- two checks of one file -------------------------------------------------

// Whichever run finishes last, what stands is the answer about the newest text.
test('the newer check publishes even when the older one answers last', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MAIN);
  assert.equal(state.calls.length, 2);
  fake.complete(state.calls[1], { stdout: CLEAN });
  fake.complete(state.calls[0], { stdout: DOCUMENT, code: 1 });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
  assert.deepEqual(state.published, [MAIN, MATHX]);
});

test('the newer check publishes when the older one answers first', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: DOCUMENT, code: 1 });
  fake.complete(state.calls[1], { stdout: CLEAN });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
});

// The dropped answer is dropped whole: it must not reach the output channel
// either, or a stale failure would be reported over a run that succeeded.
test('a superseded run that failed is not reported', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: CLEAN });
  fake.complete(state.calls[0], { stdout: '', stderr: 'boom', code: 2 });
  assert.deepEqual(state.output, []);
});

// The bug this closes: the guard used to be keyed on the checked file while a
// check publishes its whole closure, so a check of main.ft answering late could
// repaint an error in mathx.ft that a newer check of mathx.ft had just cleared,
// and it stayed painted until the user saved again.
test('an older run does not repaint a file a newer run has answered about', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MATHX);
  // The newer check of mathx.ft answers first: that file is clean now.
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  // The older check of main.ft, whose closure holds mathx.ft, answers last with
  // the error the user has already fixed.
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
  // Its own file is still published: it is the newest answer about main.ft.
  assert.equal(state.diagnostics.get(MAIN).length, 1);
});

test('a newer run does repaint a file an older run answered about', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MATHX);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

test('checks of two files do not supersede each other', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, LEXICAL_FT);
  fake.save(state, NOTES_FT);
  fake.complete(state.calls[1], { stdout: NOTES, code: 1 });
  fake.complete(state.calls[0], { stdout: LEXICAL, code: 1 });
  assert.equal(state.diagnostics.get(LEXICAL_FT).length, 1);
  assert.equal(state.diagnostics.get(NOTES_FT).length, 1);
  assert.equal(state.diagnostics.get(NOTES_FT)[0].relatedInformation.length, 1);
});

// A file checked again after it was closed is the ordinary case of reopening
// it, so nothing may be left behind that silences the new run.
test('a file checked, closed and opened again is published again', () => {
  const state = checked();
  fake.close(state, MAIN);
  fake.open(state, MAIN);
  fake.complete(state.calls[1], { stdout: DOCUMENT, code: 1 });
  assert.equal(state.diagnostics.get(MAIN).length, 1);
});

// The close resets nothing: a run older than the close must not repaint the
// file the close has just cleared. The sequence is the one the per-file
// ordering was written for, with a close where the second check was.
test('a run older than a close does not repaint the closed file', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.save(state, MATHX);
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  fake.close(state, MATHX);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.has(MATHX), false);
  // The older run is still the newest answer about its own file.
  assert.equal(state.diagnostics.get(MAIN).length, 1);
});

// The close is the newest word about the file, not the last word. A check
// started after it paints the file again, which is what reopening a file and
// saving it must do.
test('a check after a close paints the closed file again', () => {
  const { state } = open();
  fake.save(state, MATHX);
  fake.complete(state.calls[0], { stdout: MATHX_CLEAN });
  fake.close(state, MATHX);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// A close drops the run of that file that is still in flight, whole. The user
// has shut the file, and an answer about it reaches nobody.
test('a close drops the run of its own file that is in flight', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.close(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.has(MAIN), false);
  assert.equal(state.diagnostics.has(MATHX), false);
});

// ---- a file that leaves the closure ------------------------------------------

// The `import mathx;` of main.ft is deleted and main.ft is saved. The squiggle
// in mathx.ft came from a walk of main.ft's closure, and that walk no longer
// reaches mathx.ft. The extension asks the compiler about mathx.ft rather than
// deciding for it, and the old squiggle stands until that answer arrives.
test('a file that leaves the closure is checked in its own right', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  // A third run, which nobody asked for by opening or saving anything.
  assert.equal(state.calls.length, 3);
  assert.deepEqual(state.calls[2].args, ['run', RECHECK_MATHX]);
  assert.equal(state.calls[2].options.cwd, PROJECT);
  // Nothing is cleared and nothing is guessed while that run is in flight.
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  assert.deepEqual(state.cleared, []);
});

// The answer of that run is published like any other. The file ends up showing
// its own truth in either direction: clean here, broken in the test after this
// one.
test('the answer about a departed file is published like any other', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

test('a departed file that is broken keeps a squiggle of its own', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  fake.complete(state.calls[2], { stdout: MATHX_BROKEN, code: 1 });
  const items = state.diagnostics.get(MATHX);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, 'the error mathx.ft has of its own');
});

// A run that answers with no document is no answer here either (D20.1). The
// squiggles stand and the output channel says why, which is what a deleted
// module looks like: the compiler cannot read it.
test('a departed file whose run fails keeps its last answer', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  const failure = { stdout: '', stderr: "fort: error: cannot read 'mathx.ft'", code: 2 };
  fake.complete(state.calls[2], failure);
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  assert.match(state.output[1], /exited 2/);
  assert.match(state.output[2], /cannot read/);
});

// The first check of a file has no closure to compare with, so it asks about
// nothing. What another file's check painted is that check's to ask about.
test('the first check of a file checks no other file', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, path.join(PROJECT, 'other.ft'));
  fake.complete(state.calls[1], { stdout: JSON.stringify({
    version: 1,
    files: ['other.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  assert.equal(state.calls.length, 2);
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// A check of mathx.ft itself painted that error, so mathx.ft already has a run
// behind it. Asking again would cost a run and could only repeat it.
test('a departure asks nothing about a file another check painted', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MATHX);
  fake.complete(state.calls[1], { stdout: MATHX_BROKEN, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[2], { stdout: MAIN_ALONE });
  assert.equal(state.calls.length, 3);
  const items = state.diagnostics.get(MATHX);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, 'the error mathx.ft has of its own');
});

// The user has closed the file, so its `by` is null and its paint is gone.
// There is nothing to ask about.
test('a departure asks nothing about a file the user has closed', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MATHX);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  assert.equal(state.calls.length, 2);
});

// The re-check is an event like any other and takes the next number. A save of
// that file made after it therefore supersedes it. The run started last is the
// live one, and the dead one is dropped whole.
test('a save of the departed file supersedes the run the departure started', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  fake.save(state, MATHX);
  assert.equal(state.calls.length, 4);
  fake.complete(state.calls[3], { stdout: MATHX_CLEAN });
  fake.complete(state.calls[2], { stdout: MATHX_BROKEN, code: 1 });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

// The other order of the same two runs. A check of mathx.ft was already in
// flight when the departure asked again about it. The departure's run is the
// live one, and the older answer is dropped whole. Neither run guesses, and
// both read the same file.
test('a check of the departed file in flight is superseded by the new run', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MATHX);
  fake.save(state, MAIN);
  fake.complete(state.calls[2], { stdout: MAIN_ALONE });
  assert.equal(state.calls.length, 4);
  // The check of mathx.ft that the user's save started answers last and is
  // dropped; the run the departure started answers for that file.
  fake.complete(state.calls[3], { stdout: MATHX_BROKEN, code: 1 });
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  const items = state.diagnostics.get(MATHX);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, 'the error mathx.ft has of its own');
});

// Two files leave one closure and each is asked about once. The runs go out one
// at a time: a closure can be wide, and every run is its own ssh connection.
test('two files that leave one closure are asked about one run at a time', () => {
  const { state } = open();
  const OTHER = path.join(PROJECT, 'other.ft');
  const two = JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: two });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  // One run, not two, and the second waits its turn.
  assert.equal(state.calls.length, 3);
  assert.equal(state.calls[2].args[1], RECHECK_MATHX);
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.equal(state.calls.length, 4);
  assert.equal(state.calls[3].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
  fake.complete(state.calls[3], { stdout: JSON.stringify({
    version: 1,
    files: ['other.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  assert.equal(state.calls.length, 4);
  assert.deepEqual(state.diagnostics.get(OTHER), []);
});

// A run that answers with nothing releases the queue as surely as one that
// answers, or a wide departure would stop after its first failure.
test('a re-check that fails lets the next one go', () => {
  const { state } = open();
  const two = JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: two });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  fake.complete(state.calls[2], { spawnFailure: 'ENOENT' });
  assert.equal(state.calls.length, 4);
  assert.equal(state.calls[3].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
});

// A departure the queue is still holding is dropped when its reason goes. The
// user closed the file, so there is nothing to ask about and nothing to paint.
test('a queued re-check of a file the user closes is dropped', () => {
  const harness = open();
  const state = harness.state;
  const two = JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: two });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  // other.ft is queued behind mathx.ft; the user closes it before its turn.
  fake.close(state, path.join(PROJECT, 'other.ft'));
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.equal(state.calls.length, 3);
  assert.equal(harness.extension.bookkeeping().pending, 0);
});

// A run the queue started may be superseded before it answers, and that answer
// is dropped whole. The slot must be released even so, or one save would stop
// every departure that follows it.
test('a superseded re-check releases the queue', () => {
  const { state } = open();
  const two = JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: two });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  // The user saves mathx.ft while the run the departure started is in flight,
  // so that run is the superseded one.
  fake.save(state, MATHX);
  fake.complete(state.calls[2], { stdout: MATHX_BROKEN, code: 1 });
  // other.ft was queued behind it and still goes out.
  assert.equal(state.calls.length, 5);
  assert.equal(state.calls[4].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
  // The superseded answer painted nothing.
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

// A module is not a file that checks the same way on its own. The first search
// root is always the directory of the file the compiler was given (D9.2). A
// module in `util/` that imports its sibling as `util.chars` resolves that
// import only from the directory its entry file sits in. The re-check therefore
// carries that directory as `-I`, or it paints `module not found` on a file the
// user never touched. Measured with the real compiler on
// `test/lang/run/modules/nested`. Without the root: exit 1 and `not found`.
// With `-I` the directory of `main.ft`: exit 0 and a closure of both files.
test('a re-check carries the directory of the entry that painted the file', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  const NOTES = path.join(FIXTURES, 'notes.ft');
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: JSON.stringify({
    version: 1,
    files: ['project/main.ft', 'notes.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: JSON.stringify({
    version: 1,
    files: ['project/main.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  assert.equal(state.calls.length, 3);
  assert.equal(state.calls[2].args[1], COMPILER + " --check --json -I 'project' 'notes.ft'");
  assert.equal(state.calls[2].options.cwd, FIXTURES);
  fake.complete(state.calls[2], { stdout: JSON.stringify({
    version: 1,
    files: ['notes.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  assert.deepEqual(state.diagnostics.get(NOTES), []);
});

// A re-check that asks about a file of its own closure hands on the root it was
// given, and not its own directory. Its own directory is the first root of that
// one run by D9.2 and of no other run, so handing it on would check the next
// file with a root no closure ever searched (T-111 finding 3).
test('a re-check hands on the root it was given and not its own', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  const closure = (...files) =>
    JSON.stringify({ version: 1, files, diagnostics: [], symbols: [] });
  // notes.ft is checked on its own and reads lexical.ft.
  fake.save(state, NOTES_FT);
  fake.complete(state.calls[0], { stdout: closure('notes.ft', 'lexical.ft') });
  // project/main.ft reads notes.ft, and paints it in its own name.
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: closure('project/main.ft', 'notes.ft') });
  // It drops notes.ft, which is asked about with the directory of main.ft.
  fake.save(state, MAIN);
  fake.complete(state.calls[2], { stdout: closure('project/main.ft') });
  assert.equal(state.calls[3].args[1], COMPILER + " --check --json -I 'project' 'notes.ft'");
  // That answer drops lexical.ft, which notes.ft had painted. The run about it
  // carries the root notes.ft was given, which is the root of the closure both
  // files came from, and nothing else.
  fake.complete(state.calls[3], { stdout: closure('notes.ft') });
  assert.equal(state.calls[4].args[1], COMPILER + " --check --json -I 'project' 'lexical.ft'");
});

// ---- the root an open and a save carry (T-111) ------------------------------
//
// A module is not a file that checks the same way on its own. The first search
// root is always the directory of the file the compiler was given (D9.2), so an
// open or a save of `util/strings.ft` resolves `import util.chars;` from
// `util/` and paints `module 'util.chars' not found` on correct code. Measured
// with the release compiler on `test/lang/run/modules/nested`: without a root,
// exit 1 and that message; with `-I` the directory of `main.ft`, exit 0 and a
// closure of both files. The window knows that directory whenever a check has
// painted the file, which is what these tests are about.

// The fixture project of these tests: an entry file with a module in a
// sub-directory of its own, which is the shape the root matters for. Only
// `main.ft` is a file on disk. The others are names on a command line, as
// `other.ft` is above: the compiler is faked, and a name is read from disk only
// when a diagnostic must be converted against its text.
const SUB_MAIN = path.join(PROJECT, 'main.ft');
const SUB_STRINGS = path.join(PROJECT, 'util', 'strings.ft');
const SUB_CHARS = path.join(PROJECT, 'util', 'chars.ft');
// A check document that names the files it read and reports nothing.
const readFiles = (...files) =>
  JSON.stringify({ version: 1, files, diagnostics: [], symbols: [] });

// A check of the entry file that painted the module, with the workspace folder
// one directory above the entry so that the root is a path and not `.`.
function painted() {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, SUB_MAIN);
  fake.complete(state.calls[0], {
    stdout: readFiles('project/main.ft', 'project/util/strings.ft'),
  });
  return state;
}

test('an open of a module another check painted carries the root of that check', () => {
  const state = painted();
  fake.open(state, SUB_STRINGS);
  assert.equal(
    state.calls[1].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/strings.ft'"
  );
  assert.equal(state.calls[1].options.cwd, FIXTURES);
});

test('a save of a module another check painted carries the root of that check', () => {
  const state = painted();
  fake.save(state, SUB_STRINGS);
  assert.equal(
    state.calls[1].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/strings.ft'"
  );
});

// A module nothing has painted is checked as it always was. No closure that
// holds it has been walked, so nothing here knows which directory is the root
// of its project, and the extension does not invent one: a project model is
// what that takes, and no decision states one (T-111).
test('a module nothing has painted is checked with no root', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.open(state, SUB_STRINGS);
  assert.equal(state.calls.length, 1);
  assert.equal(state.calls[0].args[1], COMPILER + " --check --json 'project/util/strings.ft'");
});

// The root survives the module's own check: that check records what it was
// given, plus its own directory, so the next save of the same file carries the
// root again rather than losing it after one answer.
test('the root stays on the module after its own check answers', () => {
  const state = painted();
  fake.open(state, SUB_STRINGS);
  fake.complete(state.calls[1], {
    stdout: readFiles('project/util/strings.ft', 'project/util/chars.ft'),
  });
  fake.save(state, SUB_STRINGS);
  assert.equal(
    state.calls[2].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/strings.ft'"
  );
  // And a file that only that check has painted carries it too.
  fake.open(state, SUB_CHARS);
  assert.equal(
    state.calls[3].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/chars.ft'"
  );
});

// The directory of the file being checked is the first root in every case and
// no option adds it or removes it (D9.2), so it is not repeated on the command
// line. An entry file therefore looks exactly as it did before this root
// existed.
test('the directory of the file itself is not repeated as a root', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: CLEAN });
  fake.save(state, MAIN);
  assert.equal(state.calls[1].args[1], GUEST);
});

// A close clears the paint and keeps the root. The closure that searched that
// directory searched it whether the file is open or not, and re-opening the
// module must not paint the message the closure never saw.
test('a close keeps the root the closure searched', () => {
  const state = painted();
  fake.open(state, SUB_STRINGS);
  fake.complete(state.calls[1], { stdout: readFiles('project/util/strings.ft') });
  fake.close(state, SUB_STRINGS);
  fake.open(state, SUB_STRINGS);
  assert.equal(
    state.calls[2].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/strings.ft'"
  );
});

// The root follows the last check that painted the file, which is the same rule
// `by` follows. A module read by two entry files in two directories is checked
// the way the closure that painted it last was checked.
test('the newest check decides the root of a module', () => {
  const state = painted();
  const OTHER_MAIN = path.join(FIXTURES, 'other.ft');
  fake.save(state, OTHER_MAIN);
  fake.complete(state.calls[1], {
    stdout: readFiles('other.ft', 'project/util/strings.ft'),
  });
  fake.open(state, SUB_STRINGS);
  assert.equal(
    state.calls[2].args[1],
    COMPILER + " --check --json -I '.' 'project/util/strings.ft'"
  );
});

// An entry file that sits in the workspace folder itself gives `.` as its root,
// an empty word being no path at all.
test('an entry in the workspace folder itself gives the root a name', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, path.join(FIXTURES, 'notes.ft'));
  fake.complete(state.calls[0], { stdout: readFiles('notes.ft', 'project/util/strings.ft') });
  fake.open(state, SUB_STRINGS);
  assert.equal(
    state.calls[1].args[1],
    COMPILER + " --check --json -I '.' 'project/util/strings.ft'"
  );
});

// The roots of a file are a set and not a history. Saving one module again and
// again must not lengthen its command line, or a session would pay for every
// save it has ever made.
test('saving a module again does not lengthen its roots', () => {
  const state = painted();
  for (let i = 0; i < 20; i += 1) {
    fake.save(state, SUB_STRINGS);
    fake.complete(state.calls[state.calls.length - 1], {
      stdout: readFiles('project/util/strings.ft', 'project/util/chars.ft'),
    });
  }
  assert.equal(
    state.calls[state.calls.length - 1].args[1],
    COMPILER + " --check --json -I 'project' 'project/util/strings.ft'"
  );
});

// The root of a closure is one directory and it does not grow. A check of a
// module records the root it was given, not its own directory: its own
// directory is the first root of that one run by D9.2 and of no other. Two
// modules in sibling directories therefore each get the root of the entry file
// that read them, and never each other's.
//
// Without this, an open of `a/x.ft` and then of `b/y.ft` would put `-I 'a'` on
// the second, and `b.z` in `y.ft` would read `a/b/z.ft`: an answer from a root
// the closure never searched, which is the defect the record exists to stop
// (T-111 finding 3).
test('the root of a closure does not grow across sibling directories', () => {
  const { state } = fake.install({ workspaceFolder: PROJECT });
  const X = path.join(PROJECT, 'a', 'x.ft');
  const Y = path.join(PROJECT, 'b', 'y.ft');
  const ROOT_ONLY = COMPILER + ' --check --json -I ' + "'.'";
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: readFiles('main.ft', 'a/x.ft', 'b/y.ft') });
  // Each module is checked with the directory of main.ft, the entry file that
  // read it, and its answer names the other module of the same closure.
  fake.open(state, X);
  assert.equal(state.calls[1].args[1], ROOT_ONLY + " 'a/x.ft'");
  fake.complete(state.calls[1], { stdout: readFiles('a/x.ft', 'b/y.ft') });
  fake.open(state, Y);
  assert.equal(state.calls[2].args[1], ROOT_ONLY + " 'b/y.ft'");
  fake.complete(state.calls[2], { stdout: readFiles('b/y.ft', 'a/x.ft') });
  // And back again: the list is the one root, however many checks have run.
  fake.open(state, X);
  assert.equal(state.calls[3].args[1], ROOT_ONLY + " 'a/x.ft'");
});

// The root and the publishing are one path, not two. A module checked with the
// root of its closure answers about the whole closure, and each diagnostic must
// land on the file it names, with the columns converted against that file's text
// on disk (D20.2, D20.4). This is the boundary the command-line assertions above
// stop at.
test('a module checked with a root publishes onto the files it names', () => {
  const { state } = fake.install({ workspaceFolder: FIXTURES });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: readFiles('project/main.ft', 'notes.ft') });
  fake.open(state, NOTES_FT);
  assert.equal(state.calls[1].args[1], COMPILER + " --check --json -I 'project' 'notes.ft'");
  // That run reads lexical.ft and reports the error of the fixture document in
  // it: line 3 of a file whose third line is a tab and then `i32 x = 0755;`.
  fake.complete(state.calls[1], {
    code: 1,
    stdout: JSON.stringify({
      version: 1,
      files: ['notes.ft', 'lexical.ft'],
      diagnostics: [
        {
          file: 'lexical.ft',
          line: 3,
          col: 10,
          end_line: 3,
          end_col: 10,
          severity: 'error',
          message: "decimal literal may not start with '0'",
          notes: [],
        },
      ],
      symbols: [],
    }),
  });
  const items = state.diagnostics.get(LEXICAL_FT);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, "decimal literal may not start with '0'");
  assert.equal(items[0].severity, ERROR);
  // 1-based byte column 10, a tab counting as one column, is character 9; the
  // empty range is expanded to the word at that position.
  assert.deepEqual(items[0].range.start, { line: 2, character: 9 });
  assert.deepEqual(items[0].range.end, { line: 2, character: 13 });
  // And the module the run was pointed at is cleared, not left painted.
  assert.deepEqual(state.diagnostics.get(NOTES_FT), []);
});

// A root is a path relative to the workspace folder the run worked in. A folder
// added under that one takes the files below it, and `project` names nothing
// from inside `project`, so the root is dropped rather than carried into a run
// of another folder.
test('a root relative to another workspace folder is dropped', () => {
  const { state } = fake.install({ workspaceFolder: [FIXTURES] });
  fake.save(state, SUB_MAIN);
  fake.complete(state.calls[0], {
    stdout: readFiles('project/main.ft', 'project/util/strings.ft'),
  });
  // The user adds project/ to the window, and the module now belongs to it.
  state.folders.push(PROJECT);
  fake.open(state, SUB_STRINGS);
  assert.equal(state.calls[1].options.cwd, PROJECT);
  assert.equal(state.calls[1].args[1], COMPILER + " --check --json 'util/strings.ft'");
});

// The closure is what the compiler read, which is `"files"`, and not what was
// published. A diagnostic may name a file that is in no `"files"` array (9.2).
// Such a file never entered a closure, so it never leaves one, and no run is
// spent on it.
test('a diagnostic about a file the compiler never read enters no closure', () => {
  const { state } = open();
  const outside = JSON.stringify({
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'mathx.ft',
        line: 5,
        col: 8,
        end_line: 5,
        end_col: 11,
        severity: 'error',
        message: 'about a file that is in no files array',
        notes: [],
      },
    ],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: outside, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  assert.equal(state.calls.length, 2);
  // The paint stands: no run said anything new about that file.
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// A cyclic import is an error and not an impossibility (D9.5), and the compiler
// still lists both files, so a cyclic closure reaches this code. It terminates
// because a hop destroys its own precondition: the answer rewrites the closure
// of the file that was checked and moves `by` to it.
test('a closure that names its own importer stops after one hop', () => {
  const { state } = open();
  const closure = (...files) =>
    JSON.stringify({ version: 1, files, diagnostics: [], symbols: [] });
  fake.save(state, MATHX);
  fake.complete(state.calls[0], { stdout: closure('mathx.ft', 'main.ft') });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: closure('main.ft', 'mathx.ft') });
  // main.ft drops mathx.ft. mathx.ft is asked about, and its answer names
  // main.ft, which it painted before; main.ft has since been painted by itself.
  fake.save(state, MAIN);
  fake.complete(state.calls[2], { stdout: closure('main.ft') });
  assert.equal(state.calls.length, 4);
  fake.complete(state.calls[3], { stdout: closure('mathx.ft') });
  assert.equal(state.calls.length, 4);
});

// The user closes the departed file while the run about it is in flight. The
// close drops that run whole, as it drops any run of the file it closes. The
// answer therefore paints nothing into a file the reader has shut.
test('a close drops the re-check of its own file that is in flight', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  fake.close(state, MATHX);
  fake.complete(state.calls[2], { stdout: MATHX_BROKEN, code: 1 });
  assert.equal(state.diagnostics.has(MATHX), false);
});

// The chain a re-check starts is finite. mathx.ft is asked about, its own
// closure no longer holds other.ft, so other.ft is asked about in turn. There
// the chain stops: other.ft painted nobody.
test('a re-check that finds its own departure asks once more and stops', () => {
  const { state } = open();
  const OTHER = path.join(PROJECT, 'other.ft');
  // mathx.ft imported other.ft when it was last checked on its own.
  fake.save(state, MATHX);
  fake.complete(state.calls[0], { stdout: JSON.stringify({
    version: 1,
    files: ['mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  // main.ft imports mathx.ft, whose import of other.ft is now deleted.
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  // The import of mathx.ft goes too, so mathx.ft is asked about.
  fake.save(state, MAIN);
  fake.complete(state.calls[2], { stdout: MAIN_ALONE });
  assert.equal(state.calls.length, 4);
  assert.equal(state.calls[3].args[1], RECHECK_MATHX);
  // Its answer no longer names other.ft, which mathx.ft had painted, so
  // other.ft is asked about in turn.
  fake.complete(state.calls[3], { stdout: MATHX_CLEAN });
  assert.equal(state.calls.length, 5);
  assert.equal(state.calls[4].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
  fake.complete(state.calls[4], { stdout: JSON.stringify({
    version: 1,
    files: ['other.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  // other.ft painted nobody, so nothing follows it.
  assert.equal(state.calls.length, 5);
  assert.deepEqual(state.diagnostics.get(OTHER), []);
});

// A newer answer about the departed file stands. That is the same rule as when
// the file is still in the closure, and it holds for the answer the departure
// itself asked for.
test('a departure does not let an older answer beat a newer one', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_ALONE });
  // The run the departure started answers, and a newer check of main.ft that
  // names mathx.ft again answers after it.
  fake.save(state, MAIN);
  fake.complete(state.calls[3], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// ---- a close, which is a closure that names nothing ---------------------------

// The squiggle on mathx.ft came from a walk of main.ft's closure. Closing
// main.ft ends that walk as surely as deleting the `import` does, so mathx.ft is
// checked in its own right and nothing is guessed about it in the meantime
// (T-106).
test('closing a file asks about the files its closure painted', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  fake.close(state, MAIN);
  assert.equal(state.calls.length, 2);
  assert.deepEqual(state.calls[1].args, ['run', RECHECK_MATHX]);
  assert.equal(state.calls[1].options.cwd, PROJECT);
  // The closed file is cleared; the file its closure painted is not.
  assert.equal(state.diagnostics.has(MAIN), false);
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// That run publishes its answer like any other, which is the whole of what the
// client decides about such a file (`spec/toolchain.md` 9.2).
test('the answer a close asked for is published like any other', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MAIN);
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

// A file broken in its own right keeps a squiggle of its own, which is the
// other direction of the same rule: the file shows its own truth.
test('a file a close departed keeps a squiggle of its own', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MAIN);
  fake.complete(state.calls[1], { stdout: MATHX_BROKEN, code: 1 });
  const items = state.diagnostics.get(MATHX);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, 'the error mathx.ft has of its own');
});

// The closed file is not asked about: its paint is gone and the reader has shut
// it. `by` alone would not say so here, because the file is opened again while
// its own re-check would still be in the queue, which would put `by` back on it.
test('closing a file asks nothing about the file itself', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MAIN);
  fake.open(state, MAIN);
  fake.complete(state.calls[2], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  // The re-check of mathx.ft, and the check the open started. No third run.
  assert.equal(state.calls.length, 3);
  assert.deepEqual(state.calls[1].args, ['run', RECHECK_MATHX]);
  assert.equal(state.calls[2].args[1], GUEST);
});

// A run asks again about its own paint and about no other. Another check has
// painted mathx.ft since, so the close of main.ft has nothing to ask about.
test('a close asks nothing about a file another check painted', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MATHX);
  fake.complete(state.calls[1], { stdout: MATHX_BROKEN, code: 1 });
  fake.close(state, MAIN);
  assert.equal(state.calls.length, 2);
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// Two closes in a row ask once. The first empties the closure of the file it
// closes, and the second finds nothing left in it. The closure is three files
// wide, so the queue is still busy at the second close: a queue of one drains
// before any later event can reach it, and the duplicate would be invisible.
test('two closes in a row ask once', () => {
  const harness = open();
  const state = harness.state;
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: THREE });
  fake.close(state, MAIN);
  // mathx.ft is running and other.ft waits for it.
  assert.equal(state.calls.length, 2);
  assert.equal(harness.extension.bookkeeping().pending, 1);
  fake.close(state, MAIN);
  assert.equal(state.calls.length, 2);
  assert.equal(harness.extension.bookkeeping().pending, 1);
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  assert.equal(state.calls.length, 3);
  assert.equal(state.calls[2].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
});

// The fifth situation: a re-open while the queue still holds a run the close
// asked for. A queued run takes its number when the queue starts it, which is
// after the re-open, so `mayReplace` calls it the newer event and lets it paint.
// The check the re-open started has meanwhile reported an error in other.ft, and
// that error would be gone until main.ft is saved again. The queued run is
// therefore dropped: its paint is not the paint that was on the file when it was
// queued (T-106).
test('a re-open drops the runs a close left in the queue', () => {
  const harness = open();
  const state = harness.state;
  const OTHER = path.join(PROJECT, 'other.ft');
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: THREE });
  fake.close(state, MAIN);
  // mathx.ft is running; other.ft waits.
  assert.equal(state.calls.length, 2);
  assert.equal(harness.extension.bookkeeping().pending, 1);
  fake.open(state, MAIN);
  fake.complete(state.calls[2], { stdout: THREE_WITH_BROKEN_OTHER, code: 1 });
  assert.equal(state.diagnostics.get(OTHER).length, 1);
  // The run about mathx.ft answers and releases the queue.
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  // Nothing goes out about other.ft, and the error the open reported stands.
  assert.equal(state.calls.length, 3);
  assert.equal(harness.extension.bookkeeping().pending, 0);
  assert.equal(state.diagnostics.get(OTHER).length, 1);
});

// Closing a file that is itself inside another file's closure asks nothing. It
// has walked no closure of its own, so it has painted nobody. Its own squiggles
// go, as a close has always taken them.
test('closing a file of another closure asks nothing', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MATHX);
  assert.equal(state.calls.length, 1);
  assert.equal(state.diagnostics.has(MATHX), false);
});

// A close while a check of the same closure is in flight. The close drops that
// run whole, and the re-checks it asks for come from the closure that answered
// and is on the screen, which is the paint that has lost its support.
test('a close during a check of its closure asks about the paint on screen', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.save(state, MAIN);
  fake.close(state, MAIN);
  // The close asks about mathx.ft, and the run it interrupted paints nothing.
  assert.equal(state.calls.length, 3);
  assert.deepEqual(state.calls[2].args, ['run', RECHECK_MATHX]);
  fake.complete(state.calls[1], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.has(MAIN), false);
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
});

// A run a close asked for takes its number when it starts, like every other
// run, so a save of that file made after it is the live one and the older
// answer is dropped whole.
test('a save supersedes the run a close asked for', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MAIN);
  fake.save(state, MATHX);
  assert.equal(state.calls.length, 3);
  fake.complete(state.calls[2], { stdout: MATHX_BROKEN, code: 1 });
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  const items = state.diagnostics.get(MATHX);
  assert.equal(items.length, 1);
  assert.equal(items[0].message, 'the error mathx.ft has of its own');
});

// The file is opened again and checked before the run the close asked for
// answers. That answer is the older one and paints nothing over the newer.
test('a re-open before the answer of a close keeps the newer paint', () => {
  const { state } = open();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: MAIN_ALONE });
  fake.close(state, MAIN);
  // main.ft alone painted mathx.ft nothing, so the close asks about main.ft's
  // own file only, which it never does. A second closure sets the case up.
  assert.equal(state.calls.length, 1);
  fake.open(state, MAIN);
  fake.complete(state.calls[1], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  fake.close(state, MAIN);
  assert.equal(state.calls.length, 3);
  fake.open(state, MAIN);
  fake.complete(state.calls[3], { stdout: MAIN_WITH_STALE_MATHX, code: 1 });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
  // The run the close asked for answers last and is the older event.
  fake.complete(state.calls[2], { stdout: MATHX_CLEAN });
  assert.equal(state.diagnostics.get(MATHX).length, 1);
});

// One run for each file the closed file painted, one at a time, as a departure
// does. The queue is empty again when they have answered.
test('a close of a wide closure asks one run at a time', () => {
  const harness = open();
  const state = harness.state;
  const OTHER = path.join(PROJECT, 'other.ft');
  const three = JSON.stringify({
    version: 1,
    files: ['main.ft', 'mathx.ft', 'other.ft'],
    diagnostics: [],
    symbols: [],
  });
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: three });
  fake.close(state, MAIN);
  // mathx.ft goes out and other.ft waits for it.
  assert.equal(state.calls.length, 2);
  assert.equal(harness.extension.bookkeeping().pending, 1);
  fake.complete(state.calls[1], { stdout: MATHX_CLEAN });
  assert.equal(state.calls.length, 3);
  assert.equal(state.calls[2].args[1], COMPILER + " --check --json -I '.' 'other.ft'");
  fake.complete(state.calls[2], { stdout: JSON.stringify({
    version: 1,
    files: ['other.ft'],
    diagnostics: [],
    symbols: [],
  }) });
  assert.equal(state.calls.length, 3);
  assert.equal(harness.extension.bookkeeping().pending, 0);
  assert.deepEqual(state.diagnostics.get(OTHER), []);
});

// A file outside every workspace folder is never checked and never painted, so
// its close asks nothing, there being no folder to run a check from.
test('closing a file no workspace folder holds asks nothing', () => {
  const { state } = open();
  fake.open(state, LEXICAL_FT);
  fake.close(state, LEXICAL_FT);
  assert.equal(state.calls.length, 0);
});

// ---- what the bookkeeping costs ---------------------------------------------

// Every key of the three maps is a file path. The ordering therefore costs one
// entry per file the session has checked or published about, and nothing per
// event. Fifty opens and fifty closes of one file leave the entries its one
// closure has. `deactivate` drops all three with the window.
//
// Each cycle costs two runs, not one: the close asks again about the mathx.ft
// its closure painted (T-106). That is one run per departed file per close, the
// cost a departure has, and it adds no key to any map.
test('opening and closing a file fifty times leaves five entries', () => {
  const harness = open();
  const last = () => harness.state.calls[harness.state.calls.length - 1];
  for (let i = 0; i < 50; i += 1) {
    fake.open(harness.state, MAIN);
    fake.complete(last(), { stdout: DOCUMENT, code: 1 });
    fake.close(harness.state, MAIN);
    fake.complete(last(), { stdout: MATHX_CLEAN });
  }
  assert.equal(harness.state.calls.length, 100);
  // main.ft and mathx.ft are published about and each has walked a closure;
  // the run of main.ft is forgotten by the close, and the run of mathx.ft is
  // the one the last close asked for.
  assert.deepEqual(harness.extension.bookkeeping(), {
    runOf: 1,
    publishedAt: 2,
    closureOf: 2,
    pending: 0,
  });
  harness.extension.deactivate();
  assert.deepEqual(harness.extension.bookkeeping(), {
    runOf: 0,
    publishedAt: 0,
    closureOf: 0,
    pending: 0,
  });
});

// A file outside every workspace folder is never checked and never painted, so
// its close has nothing to clear and nothing to order. It must leave no entry
// behind: such a file is the one whose closes could pile up unseen.
test('closing a file no workspace folder holds records nothing', () => {
  const harness = open();
  for (let i = 0; i < 50; i += 1) {
    fake.open(harness.state, LEXICAL_FT);
    fake.close(harness.state, LEXICAL_FT);
  }
  assert.equal(harness.state.calls.length, 0);
  assert.deepEqual(harness.extension.bookkeeping(), {
    runOf: 0,
    publishedAt: 0,
    closureOf: 0,
    pending: 0,
  });
});

test('deactivation forgets the files that were checked', () => {
  const harness = open();
  fake.save(harness.state, MAIN);
  harness.extension.deactivate();
  fake.complete(harness.state.calls[0], { stdout: DOCUMENT, code: 1 });
  // The run was started before deactivation and its file is no longer known,
  // so its answer is dropped rather than published into a disposed collection.
  assert.equal(harness.state.diagnostics.size, 0);
});
