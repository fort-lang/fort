'use strict';

// The check document of D20.2 and what a run of the command means: a verdict
// (exit 0 or 1 and one document), a crash (exit 2, or any status with no
// document) or an unreachable VM.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const documents = require('../lib/document');

const FIXTURES = path.join(__dirname, 'fixtures');

function fixture(name) {
  return fs.readFileSync(path.join(FIXTURES, name), 'utf8');
}

test('the fixture document of the compiler parses', () => {
  const document = documents.parseDocument(fixture('check-document.json'));
  assert.notEqual(document, null);
  assert.equal(document.version, 1);
  assert.deepEqual(document.files, ['main.ft', 'mathx.ft']);
  assert.equal(document.diagnostics.length, 1);
  assert.equal(document.diagnostics[0].message, "unknown name 'nope'");
  assert.ok(document.symbols.length > 0);
});

test('the lexical fixture parses and carries an empty range', () => {
  const document = documents.parseDocument(fixture('lexical-document.json'));
  const first = document.diagnostics[0];
  assert.equal(first.col, first.end_col);
  assert.equal(first.line, first.end_line);
  assert.deepEqual(document.symbols, []);
});

test('empty or unparseable stdout is not a document', () => {
  assert.equal(documents.parseDocument(''), null);
  assert.equal(documents.parseDocument('   \n'), null);
  assert.equal(documents.parseDocument('{"version":1,'), null);
  assert.equal(documents.parseDocument('[]'), null);
  assert.equal(documents.parseDocument('null'), null);
  assert.equal(documents.parseDocument(undefined), null);
});

test('a document of another version is refused', () => {
  const text = '{"version":2,"files":[],"diagnostics":[],"symbols":[]}';
  assert.equal(documents.parseDocument(text), null);
});

// One symbol record, well formed but for the member spelled out here.
function symbolDocument(member) {
  return (
    '{"version":1,"files":["a.ft"],"diagnostics":[],"symbols":[{"file":"a.ft","line":1,"col":1,' +
    '"end_line":1,"end_col":2,"name":"x","kind":"local","type":"i32","is_decl":true,' +
    member +
    '}]}'
  );
}

test('a half-shaped document is refused member by member', () => {
  const bad = [
    '{"version":1,"diagnostics":[],"symbols":[]}',
    '{"version":1,"files":[7],"diagnostics":[],"symbols":[]}',
    '{"version":1,"files":[],"diagnostics":{},"symbols":[]}',
    '{"version":1,"files":[],"diagnostics":[{"file":"a.ft"}],"symbols":[]}',
    '{"version":1,"files":[],"diagnostics":[' +
      '{"file":"a.ft","line":1,"col":1,"end_line":1,"end_col":2,"message":"m","notes":[]}],' +
      '"symbols":[]}',
    '{"version":1,"files":[],"diagnostics":[],"symbols":[{"file":"a.ft","line":1,"col":1,' +
      '"end_line":1,"end_col":2,"name":"x","kind":"local","type":"i32","is_decl":"yes",' +
      '"decl":null}]}',
    // A half-shaped `decl` is the one a client dereferences without asking:
    // go-to-definition reads `decl.file` the moment a record is found.
    symbolDocument('"decl":{"file":"a.ft"}'),
    symbolDocument('"decl":5'),
    symbolDocument('"decl":"a.ft"'),
    symbolDocument('"decl":{"file":"a.ft","line":1,"col":"1","end_line":1,"end_col":2}'),
  ];
  for (const text of bad) assert.equal(documents.parseDocument(text), null, text);
});

test('a well-formed declaration range is accepted', () => {
  const text = symbolDocument('"decl":{"file":"a.ft","line":1,"col":1,"end_line":1,"end_col":2}');
  const document = documents.parseDocument(text);
  assert.equal(document.symbols[0].decl.file, 'a.ft');
});

test('a symbol with a null type and a null declaration is a document', () => {
  const text =
    '{"version":1,"files":["a.ft"],"diagnostics":[],"symbols":[{"file":"a.ft","line":1,' +
    '"col":1,"end_line":1,"end_col":8,"name":"println","kind":"builtin","type":"",' +
    '"is_decl":false,"decl":null}]}';
  const document = documents.parseDocument(text);
  assert.equal(document.symbols[0].kind, 'builtin');
});

test('exit 0 and exit 1 with a document are verdicts', () => {
  for (const status of [0, 1]) {
    const verdict = documents.classifyResult({
      status,
      stdout: fixture('check-document.json'),
      stderr: '',
      error: null,
    });
    assert.equal(verdict.kind, 'verdict');
    assert.equal(verdict.document.files.length, 2);
  }
});

test('exit 2 with empty stdout is a crash, not a verdict', () => {
  const verdict = documents.classifyResult({
    status: 2,
    stdout: '',
    stderr: "fort: error: cannot read 'main.ft': No such file or directory\n",
    error: null,
  });
  assert.equal(verdict.kind, 'crash');
  assert.match(verdict.message, /exit 2/);
  assert.match(verdict.message, /cannot read/);
});

test('a truncated document at exit 0 is a crash too', () => {
  const verdict = documents.classifyResult({
    status: 0,
    stdout: '{"version":1,"files":["main.ft"]',
    stderr: '',
    error: null,
  });
  assert.equal(verdict.kind, 'crash');
  assert.equal(verdict.message, 'fort: check failed (exit 0)');
});

test('a document at an unexpected status is a crash', () => {
  const verdict = documents.classifyResult({
    status: 3,
    stdout: '{"version":1,"files":[],"diagnostics":[],"symbols":[]}',
    stderr: '',
    error: null,
  });
  assert.equal(verdict.kind, 'crash');
});

test('a dead ssh is unavailable, with its first line of stderr', () => {
  const verdict = documents.classifyResult({
    status: 255,
    stdout: '',
    stderr: 'ssh: connect to host 127.0.0.1 port 2222: Connection refused\nmore\n',
    error: null,
  });
  assert.equal(verdict.kind, 'unavailable');
  assert.equal(
    verdict.message,
    'fort: the VM is unreachable: ssh: connect to host 127.0.0.1 port 2222: Connection refused'
  );
});

test('a failed spawn is unavailable', () => {
  const verdict = documents.classifyResult({
    status: -1,
    stdout: '',
    stderr: '',
    error: new Error('spawn ssh ENOENT'),
  });
  assert.equal(verdict.kind, 'unavailable');
  assert.equal(verdict.message, 'fort: the VM is unreachable: spawn ssh ENOENT');
});

test('a failure with no output still names itself', () => {
  const quiet = documents.classifyResult({ status: 255, stdout: '', stderr: '', error: null });
  assert.equal(quiet.message, 'fort: the VM is unreachable');
  const crash = documents.classifyResult({ status: 2, stdout: '', stderr: '  \n', error: null });
  assert.equal(crash.message, 'fort: check failed (exit 2)');
});
