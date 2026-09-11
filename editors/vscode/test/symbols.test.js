'use strict';

// The identifier index (D20.3): the record under the cursor, the hover text it
// produces and the declaration it points at.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const documents = require('../lib/document');
const positions = require('../lib/positions');
const symbols = require('../lib/symbols');

const FIXTURES = path.join(__dirname, 'fixtures');
const DOCUMENT = documents.parseDocument(
  fs.readFileSync(path.join(FIXTURES, 'check-document.json'), 'utf8')
);

function projectLines(file) {
  return positions.splitLines(fs.readFileSync(path.join(FIXTURES, 'project', file), 'utf8'));
}

function recordAt(file, line, character) {
  const records = symbols.recordsForFile(DOCUMENT.symbols, file);
  return symbols.recordAtPosition(records, { line, character }, projectLines(file));
}

test('the records of a file are those of that file, in document order', () => {
  const main = symbols.recordsForFile(DOCUMENT.symbols, 'main.ft');
  const mathx = symbols.recordsForFile(DOCUMENT.symbols, 'mathx.ft');
  assert.ok(main.every((record) => record.file === 'main.ft'));
  assert.equal(main.length + mathx.length, DOCUMENT.symbols.length);
  assert.deepEqual(
    mathx.map((record) => record.name),
    ['LIMIT', 'add', 'a', 'b', 'a', 'b']
  );
});

test('a position inside a name on a multi-byte line finds its record', () => {
  // `total` is at characters 20 to 25 of line 5, and at byte column 24.
  for (const character of [20, 22, 24]) {
    const record = recordAt('main.ft', 4, character);
    assert.equal(record.name, 'total');
    assert.equal(record.kind, 'local');
    assert.equal(record.type, 'i32');
    assert.equal(record.is_decl, false);
  }
});

test('a position just after a name still finds it, and one before does not', () => {
  assert.equal(recordAt('main.ft', 4, 25).name, 'total');
  const before = recordAt('main.ft', 4, 19);
  assert.equal(before, null);
});

test('a position on no name at all finds nothing', () => {
  assert.equal(recordAt('main.ft', 5, 4), null);
  assert.equal(recordAt('main.ft', 1, 0), null);
});

test('the module qualifier and the name after it are separate records', () => {
  // Line 4 is `\ti32 total = mathx.add(1, mathx.LIMIT);`, all ASCII.
  assert.equal(recordAt('main.ft', 3, 14).name, 'mathx');
  assert.equal(recordAt('main.ft', 3, 19).name, 'add');
  assert.equal(recordAt('main.ft', 3, 32).name, 'LIMIT');
});

test('a record without lines is read as ASCII', () => {
  const records = symbols.recordsForFile(DOCUMENT.symbols, 'main.ft');
  const found = symbols.recordAtPosition(records, { line: 4, character: 24 }, null);
  assert.equal(found.name, 'total');
});

// The lookup bisects, which is only sound because the document orders the
// records of a file by the start of the occurrence (D20.3).
test('the records of a file are sorted by the start of the occurrence', () => {
  for (const file of ['main.ft', 'mathx.ft']) {
    const records = symbols.recordsForFile(DOCUMENT.symbols, file);
    for (let i = 1; i < records.length; i += 1) {
      const before = records[i - 1];
      const here = records[i];
      const ordered =
        before.line < here.line || (before.line === here.line && before.col <= here.col);
      assert.ok(ordered, file + ' ' + before.name + ' before ' + here.name);
    }
  }
});

test('every record is found from its own start, and from each of its columns', () => {
  for (const file of ['main.ft', 'mathx.ft']) {
    const records = symbols.recordsForFile(DOCUMENT.symbols, file);
    const lines = projectLines(file);
    for (const record of records) {
      for (let column = record.col; column < record.end_col; column += 1) {
        const character = positions.byteColumnToCharacter(lines[record.line - 1], column);
        const at = { line: record.line - 1, character };
        const found = symbols.recordAtPosition(records, at, lines);
        assert.equal(found.name, record.name, file + ':' + record.line + ':' + column);
        assert.equal(found.col, record.col);
      }
    }
  }
});

test('a position after the last record of a line finds nothing', () => {
  const records = symbols.recordsForFile(DOCUMENT.symbols, 'mathx.ft');
  const lines = projectLines('mathx.ft');
  assert.equal(symbols.recordAtPosition(records, { line: 4, character: 40 }, lines), null);
  assert.equal(symbols.recordAtPosition([], { line: 0, character: 0 }, lines), null);
});

test('a position on a line before every record finds nothing', () => {
  const records = symbols.recordsForFile(DOCUMENT.symbols, 'mathx.ft');
  const lines = projectLines('mathx.ft');
  assert.equal(symbols.recordAtPosition(records, { line: 0, character: 0 }, lines), null);
});

test('hover shows kind, name and the type as a declaration spells it', () => {
  assert.equal(symbols.hoverText(recordAt('main.ft', 4, 21)), '```fort\nlocal total: i32\n```');
  assert.equal(
    symbols.hoverText(recordAt('main.ft', 3, 19)),
    '```fort\nfn add: fn i32(i32, i32)\n```'
  );
});

test('a name with no value type hovers as itself and a poisoned one as unknown', () => {
  const module = recordAt('main.ft', 3, 14);
  assert.equal(module.type, '');
  assert.equal(symbols.hoverText(module), '```fort\nmodule mathx\n```');
  const poisoned = { kind: 'local', name: 'broken', type: null };
  assert.equal(symbols.hoverText(poisoned), '```fort\nlocal broken: unknown\n```');
});

// The index is a batch answer about the file as it was saved, and a client
// says so rather than guessing (toolchain.md 9.2).
test('a hover the check did not answer about says so', () => {
  const record = recordAt('main.ft', 4, 21);
  const fresh = symbols.hoverText(record, false);
  const stale = symbols.hoverText(record, true);
  assert.equal(fresh, '```fort\nlocal total: i32\n```');
  assert.equal(stale, fresh + '\n\n' + symbols.STALE_NOTE);
  assert.match(symbols.STALE_NOTE, /earlier check/);
  assert.equal(symbols.hoverText(record), fresh);
  const module = recordAt('main.ft', 3, 14);
  const moduleStale = '```fort\nmodule mathx\n```\n\n' + symbols.STALE_NOTE;
  assert.equal(symbols.hoverText(module, true), moduleStale);
  const poisoned = { kind: 'local', name: 'broken', type: null };
  assert.equal(
    symbols.hoverText(poisoned, true),
    '```fort\nlocal broken: unknown\n```\n\n' + symbols.STALE_NOTE
  );
});

test('definition of a use in another module lands on the declaring name', () => {
  const target = symbols.definitionLocation(recordAt('main.ft', 3, 19), projectLines);
  assert.equal(target.file, 'mathx.ft');
  assert.deepEqual(target.range, {
    start: { line: 4, character: 7 },
    end: { line: 4, character: 10 },
  });
  const line = projectLines('mathx.ft')[4];
  assert.equal(line.slice(target.range.start.character, target.range.end.character), 'add');
});

test('definition of a module opens its file at the top', () => {
  const target = symbols.definitionLocation(recordAt('main.ft', 0, 8), projectLines);
  assert.equal(target.file, 'mathx.ft');
  assert.deepEqual(target.range, {
    start: { line: 0, character: 0 },
    end: { line: 0, character: 0 },
  });
});

test('definition of a builtin is nowhere', () => {
  const builtin = recordAt('main.ft', 4, 3);
  assert.equal(builtin.name, 'println');
  assert.equal(builtin.decl, null);
  assert.equal(symbols.definitionLocation(builtin, projectLines), null);
  assert.equal(symbols.definitionLocation(null, projectLines), null);
});

test('definition of a declaration is its own range', () => {
  const declaration = recordAt('main.ft', 3, 6);
  assert.equal(declaration.name, 'total');
  assert.equal(declaration.is_decl, true);
  const target = symbols.definitionLocation(declaration, projectLines);
  assert.equal(target.file, 'main.ft');
  assert.deepEqual(target.range.start, { line: 3, character: 5 });
});

test('a declaration in a file the editor cannot read still has a location', () => {
  const target = symbols.definitionLocation(recordAt('main.ft', 3, 19), () => null);
  assert.deepEqual(target.range, {
    start: { line: 4, character: 7 },
    end: { line: 4, character: 10 },
  });
});
