'use strict';

// Publishing the diagnostics of a document: every file of the closure gets an
// entry, so a file that is now clean has its squiggles cleared (D20.2), and
// each position converts against the text of its own file.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const diagnostics = require('../lib/diagnostics');
const documents = require('../lib/document');
const positions = require('../lib/positions');

const FIXTURES = path.join(__dirname, 'fixtures');

function readDocument(name) {
  return documents.parseDocument(fs.readFileSync(path.join(FIXTURES, name), 'utf8'));
}

// The fixture files as the editor would have them.
function projectLines(file) {
  return positions.splitLines(fs.readFileSync(path.join(FIXTURES, 'project', file), 'utf8'));
}

function fixtureLines(file) {
  return positions.splitLines(fs.readFileSync(path.join(FIXTURES, file), 'utf8'));
}

test('every file of the closure gets an entry and the clean one is empty', () => {
  const document = readDocument('check-document.json');
  const byFile = diagnostics.diagnosticsByFile(document, projectLines);
  assert.deepEqual([...byFile.keys()], ['main.ft', 'mathx.ft']);
  assert.equal(byFile.get('main.ft').length, 1);
  assert.deepEqual(byFile.get('mathx.ft'), []);
});

test('the error lands on the name, past the multi-byte characters of its line', () => {
  const document = readDocument('check-document.json');
  const item = diagnostics.diagnosticsByFile(document, projectLines).get('main.ft')[0];
  assert.equal(item.message, "unknown name 'nope'");
  assert.equal(item.severity, 'error');
  assert.deepEqual(item.range, {
    start: { line: 4, character: 27 },
    end: { line: 4, character: 31 },
  });
  // The range really covers `nope` in the text of the file.
  const line = projectLines('main.ft')[item.range.start.line];
  assert.equal(line.slice(item.range.start.character, item.range.end.character), 'nope');
});

test('a lexical error, whose range is empty, covers the word at its position', () => {
  const document = readDocument('lexical-document.json');
  const item = diagnostics.diagnosticsByFile(document, fixtureLines).get('lexical.ft')[0];
  assert.deepEqual(item.range, {
    start: { line: 2, character: 9 },
    end: { line: 2, character: 13 },
  });
  const line = fixtureLines('lexical.ft')[2];
  assert.equal(line.slice(item.range.start.character, item.range.end.character), '0755');
});

test('a file with diagnostics but outside files still gets an entry', () => {
  const document = {
    version: 1,
    files: ['a.ft'],
    diagnostics: [
      {
        file: 'b.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 3,
        severity: 'error',
        message: 'boom',
        notes: [],
      },
    ],
    symbols: [],
  };
  const byFile = diagnostics.diagnosticsByFile(document, () => ['ab']);
  assert.deepEqual([...byFile.keys()], ['a.ft', 'b.ft']);
  assert.deepEqual(byFile.get('a.ft'), []);
  assert.equal(byFile.get('b.ft')[0].message, 'boom');
});

test('a note keeps its own file and converts against it', () => {
  const document = {
    version: 1,
    files: ['a.ft', 'b.ft'],
    diagnostics: [
      {
        file: 'a.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 2,
        severity: 'error',
        message: 'boom',
        notes: [
          { file: 'b.ft', line: 1, col: 4, end_line: 1, end_col: 7, message: 'declared here' },
        ],
      },
    ],
    symbols: [],
  };
  const lines = { 'a.ft': ['x;'], 'b.ft': ['// é x'] };
  const byFile = diagnostics.diagnosticsByFile(document, (file) => lines[file]);
  const note = byFile.get('a.ft')[0].notes[0];
  assert.equal(note.file, 'b.ft');
  assert.equal(note.message, 'declared here');
  // Byte column 4 is the character after `é`, which is character 3.
  assert.deepEqual(note.range, {
    start: { line: 0, character: 3 },
    end: { line: 0, character: 5 },
  });
});

test('a file the editor cannot read keeps a plausible range', () => {
  const document = readDocument('check-document.json');
  const byFile = diagnostics.diagnosticsByFile(document, () => null);
  assert.deepEqual(byFile.get('main.ft')[0].range, {
    start: { line: 4, character: 30 },
    end: { line: 4, character: 34 },
  });
});
