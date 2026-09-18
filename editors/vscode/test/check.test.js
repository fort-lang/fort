'use strict';

/** Tests check document validation, position conversion, and file grouping. */

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const check = require('../lib/check');

const FIXTURES = path.join(__dirname, 'fixtures');
const PROJECT = path.join(FIXTURES, 'project');
const MAIN = path.join(PROJECT, 'main.ft');
const MATHX = path.join(PROJECT, 'mathx.ft');
const DOCUMENT = fs.readFileSync(path.join(FIXTURES, 'check-document.json'), 'utf8');
const LEXICAL = fs.readFileSync(path.join(FIXTURES, 'lexical-document.json'), 'utf8');
const NOTES = fs.readFileSync(path.join(FIXTURES, 'notes-document.json'), 'utf8');

// The lines of a file on disk, which is what the extension hands the module.
function linesOnDisk(absolutePath) {
  try {
    return check.splitLines(fs.readFileSync(absolutePath, 'utf8'));
  } catch (error) {
    return null;
  }
}

function noLines() {
  return null;
}

// ---- the document -----------------------------------------------------------

test('a real check document parses to its files and diagnostics', () => {
  const document = check.parseDocument(DOCUMENT);
  // The compiler reads the runtime and its imports before the entry file.
  assert.equal(document.files[0], '/vagrant/build/release/std/rt.ft');
  assert.equal(document.files[1], '/vagrant/build/release/std/libc.ft');
  assert.equal(document.files[2], 'main.ft');
  assert.equal(document.files[3], 'mathx.ft');
  assert.equal(document.version, check.DOCUMENT_VERSION);
  assert.equal(document.diagnostics.length, 1);
  assert.equal(document.diagnostics[0].message, "unknown name 'nope'");
  assert.equal(document.diagnostics[0].severity, 'error');
});

test('a document of a clean check parses with no diagnostic', () => {
  const clean = JSON.stringify({ version: 1, files: ['a.ft'], diagnostics: [], symbols: [] });
  assert.deepEqual(check.parseDocument(clean).diagnostics, []);
});

// The compiler writes one complete document or nothing, so everything
// else is "no answer" and never "no errors".
test('stdout that is not a document is no answer', () => {
  assert.equal(check.parseDocument(''), null);
  assert.equal(check.parseDocument('   \n'), null);
  assert.equal(check.parseDocument(undefined), null);
  assert.equal(check.parseDocument('fort: no such file'), null);
  assert.equal(check.parseDocument('{"version":1,"files":[]'), null);
  assert.equal(check.parseDocument('[1, 2]'), null);
  assert.equal(check.parseDocument('null'), null);
});

test('a document of another version is refused', () => {
  const other = JSON.stringify({ version: 2, files: [], diagnostics: [], symbols: [] });
  assert.equal(check.parseDocument(other), null);
});

test('a half-shaped document is refused member by member', () => {
  const position = { file: 'a.ft', line: 1, col: 1, end_line: 1, end_col: 2 };
  const good = Object.assign({ severity: 'error', message: 'boom', notes: [] }, position);
  const of = (value) => JSON.stringify(Object.assign({ version: 1, files: [] }, value));
  assert.notEqual(check.parseDocument(of({ diagnostics: [good] })), null);
  assert.equal(check.parseDocument(of({})), null);
  assert.equal(check.parseDocument(of({ diagnostics: {} })), null);
  assert.equal(check.parseDocument(of({ diagnostics: [Object.assign({}, good, { col: '1' })] })),
    null);
  assert.equal(check.parseDocument(of({ diagnostics: [Object.assign({}, good, { file: 1 })] })),
    null);
  assert.equal(
    check.parseDocument(of({ diagnostics: [Object.assign({}, good, { severity: 3 })] })),
    null
  );
  assert.equal(check.parseDocument(of({ diagnostics: [Object.assign({}, good, { notes: 0 })] })),
    null);
  assert.equal(
    check.parseDocument(of({ diagnostics: [Object.assign({}, good, { notes: [{}] })] })),
    null
  );
  const files = JSON.stringify({ version: 1, files: [7], diagnostics: [] });
  assert.equal(check.parseDocument(files), null);
});

// ---- the conversion ---------------------------------------------------------

// The target line contains tabs and multibyte characters before the diagnostic.
test('a byte column converts against the multi-byte line it names', () => {
  const document = check.parseDocument(DOCUMENT);
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  const items = byFile.get(MAIN);
  assert.equal(items.length, 1);
  assert.deepEqual(items[0].range, {
    start: { line: 5, character: 27 },
    end: { line: 5, character: 31 },
  });
  const line = linesOnDisk(MAIN)[5];
  assert.equal(line.slice(27, 31), 'nope');
});

test('without the text a byte column is taken for a character offset', () => {
  const document = check.parseDocument(DOCUMENT);
  const items = check.diagnosticsByFile(document, PROJECT, noLines).get(MAIN);
  assert.deepEqual(items[0].range, {
    start: { line: 5, character: 30 },
    end: { line: 5, character: 34 },
  });
});

// A lexical error is reported at a position and not over a range, which
// would be a squiggle of no width at all.
test('an empty range is expanded to the word at its position', () => {
  const document = check.parseDocument(LEXICAL);
  const file = path.join(FIXTURES, 'lexical.ft');
  const items = check.diagnosticsByFile(document, FIXTURES, linesOnDisk).get(file);
  assert.deepEqual(items[0].range, {
    start: { line: 2, character: 9 },
    end: { line: 2, character: 13 },
  });
  assert.equal(linesOnDisk(file)[2].slice(9, 13), '0755');
});

test('an empty range with no text at all is still one character wide', () => {
  const document = check.parseDocument(LEXICAL);
  const file = path.join(FIXTURES, 'lexical.ft');
  const items = check.diagnosticsByFile(document, FIXTURES, noLines).get(file);
  assert.deepEqual(items[0].range, {
    start: { line: 2, character: 9 },
    end: { line: 2, character: 10 },
  });
});

test('a position is converted against its own file, not the checked one', () => {
  const document = {
    version: 1,
    files: ['main.ft', 'mathx.ft'],
    diagnostics: [
      {
        file: 'mathx.ft',
        line: 5,
        col: 4,
        end_line: 5,
        end_col: 7,
        severity: 'error',
        message: 'unused',
        notes: [],
      },
    ],
  };
  const items = check.diagnosticsByFile(document, PROJECT, linesOnDisk).get(MATHX);
  assert.deepEqual(items[0].range, {
    start: { line: 4, character: 3 },
    end: { line: 4, character: 6 },
  });
  assert.equal(linesOnDisk(MATHX)[4].slice(3, 6), 'add');
});

// A stale document can name lines that the current file no longer has.
test('a range past the end of the file clamps to it', () => {
  const document = {
    version: 1,
    files: ['mathx.ft'],
    diagnostics: [
      {
        file: 'mathx.ft',
        line: 99,
        col: 200,
        end_line: 99,
        end_col: 300,
        severity: 'error',
        message: 'stale',
        notes: [],
      },
    ],
  };
  const items = check.diagnosticsByFile(document, PROJECT, linesOnDisk).get(MATHX);
  const lines = linesOnDisk(MATHX);
  assert.equal(items[0].range.start.line, lines.length - 1);
  assert.equal(items[0].range.end.line, lines.length - 1);
  assert.ok(items[0].range.end.character <= lines[lines.length - 1].length);
});

test('a `\\r\\n` file keeps its carriage return out of the line text', () => {
  assert.deepEqual(check.splitLines('a\r\nb\n'), ['a', 'b', '']);
  assert.deepEqual(check.splitLines('a\nb'), ['a', 'b']);
});

test('a file the document names with an absolute path inside the folder is kept', () => {
  const document = {
    version: 1,
    files: [MAIN],
    diagnostics: [
      {
        file: MAIN,
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 7,
        severity: 'error',
        message: 'absolute',
        notes: [],
      },
    ],
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.deepEqual([...byFile.keys()], [MAIN]);
  assert.equal(byFile.get(MAIN)[0].message, 'absolute');
});

// Guest standard-library paths are outside the workspace and cannot open in the editor.
test('a file outside the folder is dropped, diagnostic and all', () => {
  const document = check.parseDocument(DOCUMENT);
  assert.ok(document.files.some((file) => file.startsWith('/vagrant/')));
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.deepEqual([...byFile.keys()], [MAIN, MATHX]);
});

test('a diagnostic about a file outside the folder is dropped too', () => {
  const outside = {
    file: '/vagrant/build/release/std/io.ft',
    line: 1,
    col: 1,
    end_line: 1,
    end_col: 2,
    severity: 'error',
    message: 'inside the standard library',
    notes: [],
  };
  const document = { version: 1, files: ['main.ft'], diagnostics: [outside] };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.deepEqual([...byFile.keys()], [MAIN]);
  assert.deepEqual(byFile.get(MAIN), []);
});

test('a note about a file outside the folder is dropped and its error kept', () => {
  const document = {
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'main.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 7,
        severity: 'error',
        message: 'conflicting declarations',
        notes: [
          {
            file: '/vagrant/build/release/std/io.ft',
            line: 1,
            col: 1,
            end_line: 1,
            end_col: 2,
            message: 'previous declaration here',
          },
        ],
      },
    ],
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.deepEqual([...byFile.keys()], [MAIN]);
  assert.equal(byFile.get(MAIN).length, 1);
  assert.deepEqual(byFile.get(MAIN)[0].notes, []);
});

// An omitted error also omits its notes, including notes inside the workspace.
// Publishing a nested note alone would invent a standalone diagnostic.
test('a note in the folder is dropped with the outside error it followed', () => {
  const document = {
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
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  // Keep the empty entry from `files` so publishing clears main.ft.
  assert.deepEqual([...byFile.keys()], [MAIN]);
  assert.deepEqual(byFile.get(MAIN), []);
});

// The error in the reader's own file keeps its place when the note is the one
// outside. The error says what is wrong, and the note only says where else to
// look. The compiler produces that direction today: it checks the standard
// library before the module that imports it.
test('an error in the folder outlives the outside note it carried', () => {
  const document = {
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'main.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 7,
        severity: 'error',
        message: 'conflicting declarations of print',
        notes: [
          {
            file: '/vagrant/build/release/std/io.ft',
            line: 1,
            col: 1,
            end_line: 1,
            end_col: 2,
            message: 'previous declaration here',
          },
          {
            file: 'mathx.ft',
            line: 1,
            col: 1,
            end_line: 1,
            end_col: 4,
            message: 'and here',
          },
        ],
      },
    ],
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  const items = byFile.get(MAIN);
  assert.equal(items.length, 1);
  assert.equal(items[0].severity, 'error');
  // The note that points into the folder stays. The one that points out of it
  // goes, and no entry is made for the file it named.
  assert.deepEqual(items[0].notes.map((note) => note.message), ['and here']);
  assert.equal(items[0].notes[0].file, MATHX);
});

// A path that climbs out of the folder is outside it however it is spelled, and
// the folder itself is not a file.
test('being under the folder is answered on the resolved path', () => {
  assert.equal(check.isUnder(MAIN, PROJECT), true);
  assert.equal(check.isUnder(MAIN, FIXTURES), true);
  assert.equal(check.isUnder(path.join(PROJECT, 'a', 'b.ft'), PROJECT), true);
  assert.equal(check.isUnder(FIXTURES, PROJECT), false);
  assert.equal(check.isUnder(PROJECT, PROJECT), false);
  assert.equal(check.isUnder('/vagrant/build/release/std/io.ft', PROJECT), false);
  assert.equal(check.isUnder(path.join(PROJECT + 'x', 'main.ft'), PROJECT), false);
});

test('a relative path is resolved against the directory of the checked file', () => {
  assert.equal(check.resolveDocumentPath('mathx.ft', PROJECT), MATHX);
  assert.equal(check.resolveDocumentPath('./mathx.ft', PROJECT), MATHX);
  assert.equal(check.resolveDocumentPath(MAIN, PROJECT), MAIN);
});

// ---- the closure ------------------------------------------------------------

// `"files"` is what the compiler read, and the client remembers it as the
// closure. The standard library is in every closure and its paths name the
// guest, so the folder is what decides.
test('the closure holds the files of the document that lie in the folder', () => {
  const document = check.parseDocument(DOCUMENT);
  assert.ok(document.files.some((file) => file.startsWith('/vagrant/')));
  assert.deepEqual(check.filesUnder(document, PROJECT), [MAIN, MATHX]);
});

test('a file of the closure outside the folder is left out', () => {
  const document = {
    version: 1,
    files: ['main.ft', '/vagrant/build/release/std/io.ft', '../lexical.ft', 'mathx.ft'],
    diagnostics: [],
  };
  assert.deepEqual(check.filesUnder(document, PROJECT), [MAIN, MATHX]);
});

// Keep compiler read order. Resolve relative paths against the workspace folder.
test('the closure keeps the order the compiler read the files in', () => {
  const document = {
    version: 1,
    files: ['mathx.ft', './main.ft'],
    diagnostics: [],
  };
  assert.deepEqual(check.filesUnder(document, PROJECT), [MATHX, MAIN]);
});

// ---- the grouping -----------------------------------------------------------

// Empty entries clear fixed files while non-empty entries paint broken files.
test('a file of the closure with no diagnostic gets an empty entry', () => {
  const document = check.parseDocument(DOCUMENT);
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.ok(byFile.has(MATHX));
  assert.deepEqual(byFile.get(MATHX), []);
});

test('several diagnostics of one file keep the compiler order', () => {
  const at = (line, message) => ({
    file: 'main.ft',
    line,
    col: 1,
    end_line: line,
    end_col: 3,
    severity: 'error',
    message,
    notes: [],
  });
  const diagnostics = [at(1, 'first'), at(2, 'second')];
  const document = { version: 1, files: ['main.ft'], diagnostics };
  const items = check.diagnosticsByFile(document, PROJECT, linesOnDisk).get(MAIN);
  assert.deepEqual(items.map((item) => item.message), ['first', 'second']);
  assert.deepEqual(items.map((item) => item.notes), [[], []]);
});

// Keep a note as related information, even when it has the same range as its error.
test('a note is carried by the error it follows, with its own place', () => {
  const document = check.parseDocument(NOTES);
  const file = path.join(FIXTURES, 'notes.ft');
  const items = check.diagnosticsByFile(document, FIXTURES, linesOnDisk).get(file);
  assert.equal(items.length, 1);
  assert.equal(items[0].severity, 'error');
  assert.match(items[0].message, /module 'nothere' not found/);
  assert.equal(items[0].notes.length, 1);
  assert.equal(items[0].notes[0].file, file);
  assert.match(items[0].notes[0].message, /looked for/);
  assert.deepEqual(items[0].notes[0].range, items[0].range);
  assert.deepEqual(items[0].range, {
    start: { line: 3, character: 0 },
    end: { line: 3, character: 15 },
  });
});

test('a note about another file names that file and does not paint it', () => {
  const document = {
    version: 1,
    files: ['main.ft', 'mathx.ft'],
    diagnostics: [
      {
        file: 'main.ft',
        line: 1,
        col: 8,
        end_line: 1,
        end_col: 13,
        severity: 'error',
        message: 'conflicting declarations',
        notes: [
          {
            file: 'mathx.ft',
            line: 5,
            col: 8,
            end_line: 5,
            end_col: 11,
            message: 'previous declaration here',
          },
        ],
      },
    ],
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.equal(byFile.get(MAIN).length, 1);
  assert.deepEqual(byFile.get(MATHX), []);
  const note = byFile.get(MAIN)[0].notes[0];
  assert.equal(note.file, MATHX);
  assert.equal(note.message, 'previous declaration here');
  // Converted against mathx.ft, which is the file it names.
  assert.deepEqual(note.range, {
    start: { line: 4, character: 7 },
    end: { line: 4, character: 10 },
  });
});

// The compiler may report about a file it did not list, and a diagnostic with
// nowhere to go is a diagnostic the reader never sees.
test('a diagnostic about a file outside the list still gets an entry', () => {
  const document = {
    version: 1,
    files: ['main.ft'],
    diagnostics: [
      {
        file: 'other.ft',
        line: 1,
        col: 1,
        end_line: 1,
        end_col: 2,
        severity: 'error',
        message: 'elsewhere',
        notes: [],
      },
    ],
  };
  const byFile = check.diagnosticsByFile(document, PROJECT, linesOnDisk);
  assert.equal(byFile.get(path.join(PROJECT, 'other.ft'))[0].message, 'elsewhere');
  assert.deepEqual(byFile.get(MAIN), []);
});

// A standalone note uses information severity, not error severity.
test('a standalone note keeps its severity', () => {
  const document = {
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
  };
  const items = check.diagnosticsByFile(document, PROJECT, linesOnDisk).get(MAIN);
  assert.equal(items[0].severity, 'note');
});

test('the text of each file is read once however many diagnostics it has', () => {
  const reads = [];
  const counting = (file) => {
    reads.push(file);
    return linesOnDisk(file);
  };
  const at = (line) => ({
    file: 'main.ft',
    line,
    col: 1,
    end_line: line,
    end_col: 3,
    severity: 'error',
    message: 'x',
    notes: [{ file: 'main.ft', line, col: 1, end_line: line, end_col: 3, message: 'y' }],
  });
  const document = { version: 1, files: ['main.ft'], diagnostics: [at(1), at(2), at(3)] };
  check.diagnosticsByFile(document, PROJECT, counting);
  assert.deepEqual(reads, [MAIN]);
});
