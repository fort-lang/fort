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

test('deactivation forgets the files that were checked', () => {
  const harness = open();
  fake.save(harness.state, MAIN);
  harness.extension.deactivate();
  fake.complete(harness.state.calls[0], { stdout: DOCUMENT, code: 1 });
  // The run was started before deactivation and its file is no longer known,
  // so its answer is dropped rather than published into a disposed collection.
  assert.equal(harness.state.diagnostics.size, 0);
});
