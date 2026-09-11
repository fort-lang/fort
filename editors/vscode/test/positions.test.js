'use strict';

// The conversion from the compiler's 1-based byte columns (D20.2, D20.4) to
// the 0-based UTF-16 offsets VS Code counts in, which is where a line holding a
// multi-byte character or a tab goes wrong if a byte is taken for a character.

const test = require('node:test');
const assert = require('node:assert/strict');

const positions = require('../lib/positions');

// `\tprintln("héllo ☃",\ttotal, nope);` as it stands in fixtures/project
// main.ft: the tab is one byte and one character, `é` is two bytes and one,
// `☃` is three bytes and one.
const MIXED = '\tprintln("héllo ☃",\ttotal, nope);';

test('a byte column on an ASCII line is the column minus one', () => {
  const line = 'fn i32 main() {';
  assert.equal(positions.byteColumnToCharacter(line, 1), 0);
  assert.equal(positions.byteColumnToCharacter(line, 8), 7);
  assert.equal(positions.byteColumnToCharacter(line, 12), 11);
});

test('a column at or before the start of a line is the start of the line', () => {
  assert.equal(positions.byteColumnToCharacter(MIXED, 1), 0);
  assert.equal(positions.byteColumnToCharacter(MIXED, 0), 0);
  assert.equal(positions.byteColumnToCharacter(MIXED, -3), 0);
});

test('a tab counts as one byte and one character', () => {
  assert.equal(positions.byteColumnToCharacter(MIXED, 2), 1);
  assert.equal(positions.byteColumnToCharacter('\t\t\tx', 4), 3);
  assert.equal(positions.characterToByteColumn('\t\t\tx', 3), 4);
});

test('a multi-byte character shifts every column after it', () => {
  // `total` is at byte column 24 and character 20: `é` adds one byte and `☃`
  // two, so the column runs three ahead of the character.
  assert.equal(MIXED.indexOf('total'), 20);
  assert.equal(positions.byteColumnToCharacter(MIXED, 24), 20);
  assert.equal(positions.byteColumnToCharacter(MIXED, 29), 25);
  // And `nope`, further along the same line, is at byte column 31.
  assert.equal(MIXED.indexOf('nope'), 27);
  assert.equal(positions.byteColumnToCharacter(MIXED, 31), 27);
  assert.equal(positions.byteColumnToCharacter(MIXED, 35), 31);
});

test('every byte column of a mixed line round-trips through the character', () => {
  const bytes = Buffer.byteLength(MIXED, 'utf8');
  for (let character = 0; character <= MIXED.length; character += 1) {
    const column = positions.characterToByteColumn(MIXED, character);
    assert.ok(column >= 1 && column <= bytes + 1, 'column ' + column + ' is on the line');
    assert.equal(positions.byteColumnToCharacter(MIXED, column), character);
  }
});

test('a column inside a multi-byte character clamps to its start', () => {
  // `é` occupies byte columns 12 and 13; both name the character before it.
  assert.equal(positions.byteColumnToCharacter(MIXED, 12), 11);
  assert.equal(positions.byteColumnToCharacter(MIXED, 13), 11);
  assert.equal(positions.byteColumnToCharacter(MIXED, 14), 12);
});

test('a column past the end of a line clamps to its end', () => {
  const line = 'i32 x;';
  assert.equal(positions.byteColumnToCharacter(line, 7), 6);
  assert.equal(positions.byteColumnToCharacter(line, 99), 6);
  assert.equal(positions.byteColumnToCharacter('', 1), 0);
  assert.equal(positions.byteColumnToCharacter('', 40), 0);
});

test('a character past the end of a line keeps counting in bytes', () => {
  assert.equal(positions.characterToByteColumn('ab', 2), 3);
  assert.equal(positions.characterToByteColumn('ab', 5), 6);
});

test('an astral character is four bytes and two UTF-16 units', () => {
  const line = '// \u{1F600} x';
  assert.equal(line.length, 7);
  assert.equal(Buffer.byteLength(line, 'utf8'), 9);
  assert.equal(positions.byteColumnToCharacter(line, 8), 5);
  assert.equal(positions.byteColumnToCharacter(line, 9), 6);
  assert.equal(positions.characterToByteColumn(line, 6), 9);
  assert.equal(positions.characterToByteColumn(line, 5), 8);
  // A column inside the surrogate pair names its start, never its second half.
  assert.equal(positions.byteColumnToCharacter(line, 5), 3);
  assert.equal(positions.byteColumnToCharacter(line, 6), 3);
  assert.equal(positions.byteColumnToCharacter(line, 7), 3);
});

test('utf8Length answers each of the four forms', () => {
  assert.equal(positions.utf8Length(0x41), 1);
  assert.equal(positions.utf8Length(0xE9), 2);
  assert.equal(positions.utf8Length(0x2603), 3);
  assert.equal(positions.utf8Length(0x1F600), 4);
});

test('a word range covers the identifier under the position', () => {
  const line = '    i32 total = 0;';
  assert.deepEqual(positions.wordRangeAt(line, 8), { start: 8, end: 13 });
  assert.deepEqual(positions.wordRangeAt(line, 10), { start: 8, end: 13 });
  // The position just after a word still names it, which is where a caret sits.
  assert.deepEqual(positions.wordRangeAt(line, 13), { start: 8, end: 13 });
});

test('a word range on no identifier covers one character', () => {
  const line = 'a + b';
  assert.deepEqual(positions.wordRangeAt(line, 2), { start: 2, end: 3 });
  const astral = '\u{1F600}+';
  assert.deepEqual(positions.wordRangeAt(astral, 0), { start: 0, end: 2 });
});

test('a word range at the end of a line covers the character before it', () => {
  assert.deepEqual(positions.wordRangeAt('x = (', 5), { start: 4, end: 5 });
  assert.deepEqual(positions.wordRangeAt('x = \u{1F600}', 6), { start: 4, end: 6 });
  assert.deepEqual(positions.wordRangeAt('', 0), { start: 0, end: 0 });
});

test('a range converts both ends against their own lines', () => {
  const lines = ['fn i32 main() {', MIXED, '}'];
  const range = positions.toEditorRange(
    { file: 'main.ft', line: 2, col: 31, end_line: 2, end_col: 35 },
    lines
  );
  assert.deepEqual(range, {
    start: { line: 1, character: 27 },
    end: { line: 1, character: 31 },
  });
});

// Each end converts against the text of its own line: the two lines here have
// different byte structures at the same byte column, so converting the end
// against the start's line, or either against the whole document, gives another
// answer.
test('a multi-line range converts each end against its own line', () => {
  const lines = ['fn i32 main() {', MIXED, 'i32 x = "héllo ☃";'];
  const range = positions.toEditorRange(
    { file: 'main.ft', line: 2, col: 24, end_line: 3, end_col: 18 },
    lines
  );
  assert.deepEqual(range, { start: { line: 1, character: 20 }, end: { line: 2, character: 15 } });
  // Converting either end against the other's line gives another answer, which
  // is what makes the assertion above say something.
  assert.equal(positions.byteColumnToCharacter(MIXED, 18), 16);
  assert.equal(positions.byteColumnToCharacter(lines[2], 24), 18);
  assert.equal(lines[2].slice(0, 15), 'i32 x = "héllo ');
  assert.equal(MIXED.slice(20, 25), 'total');
});

test('a multi-line range keeps each end on its own line', () => {
  const lines = ['fn i32 main() {', '\treturn 0;', '}'];
  const range = positions.toEditorRange(
    { file: 'main.ft', line: 1, col: 1, end_line: 3, end_col: 2 },
    lines
  );
  assert.deepEqual(range, { start: { line: 0, character: 0 }, end: { line: 2, character: 1 } });
});

test('an empty range is expanded to the word at its position', () => {
  const lines = ['fn i32 main() {', '\ti32 x = 0755;', '}'];
  const range = positions.toEditorRange(
    { file: 'main.ft', line: 2, col: 10, end_line: 2, end_col: 10 },
    lines
  );
  assert.deepEqual(range, { start: { line: 1, character: 9 }, end: { line: 1, character: 13 } });
});

test('a line past the end of the document clamps to the last line', () => {
  const lines = ['one', 'two'];
  const range = positions.toEditorRange(
    { file: 'main.ft', line: 9, col: 1, end_line: 9, end_col: 4 },
    lines
  );
  assert.deepEqual(range, { start: { line: 1, character: 0 }, end: { line: 1, character: 3 } });
  assert.equal(positions.clampLine(lines, 0), 0);
  assert.equal(positions.clampLine([], 4), 0);
  assert.equal(positions.lineTextAt(lines, 3), '');
  assert.equal(positions.lineTextAt(null, 1), '');
});

test('a file the editor cannot read falls back to byte-as-character', () => {
  const range = positions.toEditorRange(
    { file: 'gone.ft', line: 4, col: 7, end_line: 4, end_col: 11 },
    null
  );
  assert.deepEqual(range, { start: { line: 3, character: 6 }, end: { line: 3, character: 10 } });
  const empty = positions.asciiRange({ file: 'gone.ft', line: 1, col: 3, end_line: 1, end_col: 3 });
  assert.deepEqual(empty, { start: { line: 0, character: 2 }, end: { line: 0, character: 3 } });
});

test('splitLines drops the carriage return of a CRLF file', () => {
  assert.deepEqual(positions.splitLines('a\r\nb\n'), ['a', 'b', '']);
  assert.deepEqual(positions.splitLines('a\nb'), ['a', 'b']);
  assert.equal(positions.isWordCharacter('_'), true);
  assert.equal(positions.isWordCharacter('.'), false);
  assert.equal(positions.isWordCharacter('é'), false);
});
