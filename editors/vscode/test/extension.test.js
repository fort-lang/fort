'use strict';

// The glue: a save runs one check in a child process, its document is
// published, and hover and definition are answered from it (D20.1, D20.2,
// D20.3). The editor and the spawn are faked (test/fake_vscode.js), so what is
// exercised here is the extension's own case analysis and not VS Code.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const commands = require('../lib/command');
const fake = require('./fake_vscode');
const symbols = require('../lib/symbols');

const PROJECT = path.join(__dirname, 'fixtures', 'project');
const MAIN = path.join(PROJECT, 'main.ft');
const MATHX = path.join(PROJECT, 'mathx.ft');
const DOCUMENT = fs.readFileSync(path.join(__dirname, 'fixtures', 'check-document.json'), 'utf8');
const NOTE = symbols.STALE_NOTE;
// main.ft with the multi-byte characters of line 5 replaced, so a byte column
// converts to a different character offset against it than against the file.
const PLAIN_MAIN = fs
  .readFileSync(MAIN, 'utf8')
  .replace('héllo ☃', 'hello xy')
  .replace('\t', '    ');
const CLEAN = JSON.stringify({
  version: 1,
  files: ['main.ft', 'mathx.ft'],
  diagnostics: [],
  symbols: [],
});

// A successful check of main.ft, with the fixture document as its answer.
function checked() {
  const harness = fake.install();
  fake.save(harness.state, MAIN);
  fake.complete(harness.state.calls[0], { stdout: DOCUMENT });
  return harness;
}

test('a save spawns one ssh and publishes nothing until it answers', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  assert.equal(state.calls.length, 1);
  assert.equal(state.calls[0].command, 'ssh');
  assert.equal(state.calls[0].args[state.calls[0].args.length - 3], '--');
  assert.match(state.calls[0].args[state.calls[0].args.length - 1], /--check --json --index/);
  assert.equal(state.diagnostics.size, 0);
  assert.ok(state.calls[0].options.timeout > 0);
  // The logged line is the one the README tells the user to paste, so the
  // remote command is one quoted word there rather than loose text.
  assert.equal(state.output[0], 'ssh ' + commands.shellJoin(state.calls[0].args));
  assert.match(state.output[0], /^ssh -F \/tmp\/fort-ssh-config /);
  assert.match(state.output[0], / -- default '\/vagrant\/build\/debug\/fort --check/);
});

// The regression this closes shipped once: `os.tmpdir()` is
// `/var/folders/<...>/T` on macOS, the control path overflowed the Unix domain
// socket limit, and every check failed with ssh's exit 255.
test('the control socket fits a socket path even under a macOS tmpdir', () => {
  const macos = '/var/folders/6y/jxxbq7y547z69nkt88pbdr5m0000gn/T';
  const { extension, state } = fake.install({ tmpdir: macos });
  fake.save(state, MAIN);
  const argument = state.calls[0].args[5];
  assert.match(argument, /^ControlPath=/);
  const controlPath = argument.slice('ControlPath='.length);
  assert.ok(
    commands.controlPathLength(controlPath) <= commands.SOCKET_PATH_MAX,
    controlPath + ' is ' + commands.controlPathLength(controlPath) + ' bytes'
  );
  // Nobody may have owned the directory first: it is made by mkdtemp, so its
  // name is unpredictable, it is this user's, it is 0700 and it is no symlink.
  const directory = path.dirname(controlPath);
  const info = fs.lstatSync(directory);
  assert.equal(info.isSymbolicLink(), false);
  assert.equal(info.isDirectory(), true);
  assert.equal(info.mode & 0o777, 0o700);
  if (typeof process.getuid === 'function') assert.equal(info.uid, process.getuid());
  assert.equal(directory.startsWith('/tmp/fort-'), true);
  assert.notEqual(directory, '/tmp/fort-XXXXXX');
  // One directory per activation, and it is gone afterwards.
  fake.save(state, MATHX);
  assert.equal(state.calls[1].args[5], argument);
  extension.deactivate();
  assert.equal(fs.existsSync(directory), false);
});

test('the control socket stays under the temporary directory when it fits', () => {
  // Short enough for a socket path, which `os.tmpdir()` itself may not be.
  const base = fs.mkdtempSync('/tmp/fort-test-');
  const { extension, state } = fake.install({ tmpdir: base });
  fake.save(state, MAIN);
  const controlPath = state.calls[0].args[5].slice('ControlPath='.length);
  assert.equal(path.dirname(path.dirname(controlPath)), base);
  extension.deactivate();
  fs.rmSync(base, { recursive: true, force: true });
});

test('a file that is not fort is not checked', () => {
  const { state } = fake.install();
  fake.save(state, path.join(PROJECT, 'notes.txt'));
  assert.equal(state.calls.length, 0);
});

test('the document is published for every file of the closure', () => {
  const { state } = checked();
  assert.deepEqual([...state.diagnostics.keys()].sort(), [MAIN, MATHX].sort());
  const errors = state.diagnostics.get(MAIN);
  assert.equal(errors.length, 1);
  assert.equal(errors[0].message, "unknown name 'nope'");
  assert.equal(errors[0].source, 'fort');
  // Byte columns 31 to 35 of a line holding a tab, `é` and `☃`.
  assert.deepEqual(errors[0].range.start, { line: 4, character: 27 });
  assert.deepEqual(errors[0].range.end, { line: 4, character: 31 });
  assert.deepEqual(state.diagnostics.get(MATHX), []);
  assert.equal(state.status.visible, false);
});

// A note that follows no error stands as a diagnostic of its own, whose
// severity is `note` (D20.2); painting it red would make it read as an error.
test('a note is not painted as an error', () => {
  const withNote = JSON.stringify({
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'main.ft',
        line: 5,
        col: 31,
        end_line: 5,
        end_col: 35,
        severity: 'note',
        message: 'declared here',
        notes: [],
      },
      {
        file: 'main.ft',
        line: 4,
        col: 2,
        end_line: 4,
        end_col: 5,
        severity: 'error',
        message: 'unknown name',
        notes: [],
      },
    ],
    symbols: [],
  });
  const { state } = fake.install();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: withNote, code: 1 });
  const items = state.diagnostics.get(MAIN);
  assert.equal(items[0].severity, 2, 'a note is Information');
  assert.equal(items[1].severity, 0, 'an error is Error');
});

// The columns of the document are byte columns of what the compiler read, so a
// buffer some window has edited since is the wrong text to convert against.
test('a published range converts against the file, not an edited buffer', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  // The user keeps typing while ssh runs, so the buffer is no longer the text
  // the compiler read by the time the answer arrives.
  fake.open(state, fake.document(MAIN, { version: 9, dirty: true, text: PLAIN_MAIN }));
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  // Byte column 31 is character 27 of the file and character 30 of that buffer.
  assert.deepEqual(state.diagnostics.get(MAIN)[0].range.start, { line: 4, character: 27 });
});

// Showing a position to the reader is the other way round: the buffer is what
// is on screen, so that is what a jump target converts against.
test('a definition converts against the buffer the reader is looking at', () => {
  const { state } = checked();
  const shifted = ['', '', '', '', 'é' + 'fn i32 add(i32 a, i32 b) {', ''].join('\n');
  fake.open(state, fake.document(MATHX, { version: 2, text: shifted }));
  const target = state.definitionProvider.provideDefinition(fake.document(MAIN), {
    line: 3,
    character: 19,
  });
  assert.equal(target.uri.fsPath, MATHX);
  // Byte column 8 is character 7 of the file and character 6 of that buffer,
  // where a two-byte `é` stands before the name.
  assert.deepEqual(target.range.start, { line: 4, character: 6 });
});

test('a crash keeps the diagnostics already published and says so', () => {
  const { state } = checked();
  fake.save(state, MAIN);
  state.published.length = 0;
  fake.complete(state.calls[1], { code: 2, stderr: 'fort: no such file\n' });
  assert.deepEqual(state.published, []);
  assert.equal(state.diagnostics.get(MAIN).length, 1);
  assert.equal(state.status.visible, true);
  assert.match(state.status.tooltip, /last successful check/);
  assert.ok(state.output.some((line) => line.includes('check failed (exit 2)')));
});

test('a dead ssh reads as the VM being unreachable', () => {
  const dead = [
    { code: 255, stderr: 'ssh: connect failed' },
    { spawnFailure: 'ENOENT' },
    { timeout: true },
  ];
  for (const result of dead) {
    const { state } = fake.install();
    fake.save(state, MAIN);
    fake.complete(state.calls[0], result);
    assert.equal(state.diagnostics.size, 0);
    assert.equal(state.status.visible, true);
    assert.ok(state.output.some((line) => line.includes('the VM is unreachable')));
  }
});

test('a successful check hides the failure of the one before it', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { code: 255 });
  assert.equal(state.status.visible, true);
  fake.save(state, MAIN);
  fake.complete(state.calls[1], { stdout: DOCUMENT });
  assert.equal(state.status.visible, false);
});

const AT_TOTAL = { line: 4, character: 21 };

test('hover answers from the last check of the text on screen', () => {
  const { state } = checked();
  const hover = state.hoverProvider.provideHover(fake.document(MAIN), AT_TOTAL);
  assert.equal(hover.contents.value, '```fort\nlocal total: i32\n```');
  assert.deepEqual(hover.range.start, { line: 4, character: 20 });
  const blank = state.hoverProvider.provideHover(fake.document(MAIN), { line: 5, character: 4 });
  assert.equal(blank, null);
});

// `isDirty` says there are unsaved edits, which is not the question: the
// question is whether the answer came from a check of this text (toolchain.md
// 9.2), and these are the four ways it did not.
test('an answer that is not about the text on screen says so', () => {
  const { state } = checked();
  const edited = fake.document(MAIN, { version: 2, dirty: true });
  assert.ok(state.hoverProvider.provideHover(edited, AT_TOTAL).contents.value.endsWith(NOTE));
  // Saved since, but the check of that save never produced a document: the
  // buffer is clean and the answer is still two saves old.
  fake.save(state, MAIN, { version: 3 });
  fake.complete(state.calls[1], { code: 255 });
  const saved = fake.document(MAIN, { version: 3 });
  assert.ok(state.hoverProvider.provideHover(saved, AT_TOTAL).contents.value.endsWith(NOTE));
  // A file of the closure that no window had open when it was checked.
  const other = fake.document(MATHX);
  const hover = state.hoverProvider.provideHover(other, { line: 4, character: 8 });
  assert.ok(hover.contents.value.endsWith(NOTE));
});

test('a file checked while a window held unsaved edits is marked too', () => {
  const { state } = fake.install();
  fake.open(state, fake.document(MATHX, { version: 4, dirty: true }));
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  const clean = fake.document(MATHX, { version: 4 });
  const hover = state.hoverProvider.provideHover(clean, { line: 4, character: 8 });
  assert.ok(hover.contents.value.endsWith(NOTE));
  // The file that was saved is the one the check is about, and it is fresh.
  const main = state.hoverProvider.provideHover(fake.document(MAIN), AT_TOTAL);
  assert.equal(main.contents.value.includes(NOTE), false);
});

test('a file no check has covered has no answer', () => {
  const { state } = fake.install();
  const other = fake.document(path.join(PROJECT, 'unchecked.ft'));
  assert.equal(state.hoverProvider.provideHover(other, { line: 0, character: 0 }), null);
  assert.equal(state.definitionProvider.provideDefinition(other, { line: 0, character: 0 }), null);
});

test('definition jumps into the module that declares the name', () => {
  const { state } = checked();
  const target = state.definitionProvider.provideDefinition(fake.document(MAIN), {
    line: 3,
    character: 19,
  });
  assert.equal(target.uri.fsPath, MATHX);
  assert.deepEqual(target.range.start, { line: 4, character: 7 });
  assert.deepEqual(target.range.end, { line: 4, character: 10 });
  const module = state.definitionProvider.provideDefinition(fake.document(MAIN), {
    line: 0,
    character: 8,
  });
  assert.equal(module.uri.fsPath, MATHX);
  assert.deepEqual(module.range.start, { line: 0, character: 0 });
  const builtin = state.definitionProvider.provideDefinition(fake.document(MAIN), {
    line: 4,
    character: 3,
  });
  assert.equal(builtin, null);
});

test('a save while a check is in flight queues exactly one rerun', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  fake.save(state, MAIN);
  fake.save(state, MAIN);
  assert.equal(state.calls.length, 1);
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  assert.equal(state.calls.length, 2);
  fake.complete(state.calls[1], { stdout: DOCUMENT });
  assert.equal(state.calls.length, 2);
});

// The file must be released whatever happens, or one failure would leave it
// unchecked forever and silently.
test('a throw while publishing still lets the file be checked again', () => {
  const { state } = fake.install();
  state.throwOnPublish = true;
  fake.save(state, MAIN);
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  assert.ok(state.output.some((line) => line.includes('could not publish')));
  fake.save(state, MAIN);
  assert.equal(state.calls.length, 2);
  fake.complete(state.calls[1], { stdout: DOCUMENT });
  assert.equal(state.diagnostics.get(MAIN).length, 1);
});

// Two saves whose closures overlap can finish in either order.
test('an older run does not overwrite the answer of a newer one', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  fake.save(state, MATHX);
  assert.equal(state.calls.length, 2);
  fake.complete(state.calls[1], { stdout: CLEAN });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
});

test('the newer run does overwrite the answer of an older one', () => {
  const { state } = fake.install();
  fake.save(state, MAIN);
  fake.save(state, MATHX);
  fake.complete(state.calls[0], { stdout: DOCUMENT });
  assert.equal(state.diagnostics.get(MAIN).length, 1);
  fake.complete(state.calls[1], { stdout: CLEAN });
  assert.deepEqual(state.diagnostics.get(MAIN), []);
});

test('deactivate forgets what the last check answered', () => {
  const { extension, state } = checked();
  extension.deactivate();
  const hover = state.hoverProvider.provideHover(fake.document(MAIN), { line: 4, character: 21 });
  assert.equal(hover, null);
});
