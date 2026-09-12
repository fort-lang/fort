'use strict';

// The check document of D20.2 turned into diagnostic records: what is accepted
// as a document, the conversion of its 1-based byte columns to 0-based UTF-16
// offsets, and the grouping by file that publishes and clears squiggles.
//
// The documents under `fixtures/` are real compiler output, regenerated with
// `fort --check --json` over the `.ft` file of the same name; the documents
// built here by hand are the shapes a compiler must not be trusted to produce
// (a truncated read, a wrong version, a foreign JSON document).

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
  // Every closure holds std.rt, which the compiler reads before the entry file,
  // and load_module resolves a module's imports before it returns, so std.libc
  // follows the runtime and the entry file comes third.
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

// The compiler writes one complete document or nothing (D20.1), so everything
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

// Line 6 of main.ft is `\tprintln("héllo ☃",\ttotal, nope);`: `é` is two bytes
// and one UTF-16 unit, `☃` is three and one, and the tabs are one each (D20.4).
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

// A lexical error is reported at a position and not over a range (D14.2), which
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
        col: 8,
        end_line: 5,
        end_col: 11,
        severity: 'error',
        message: 'unused',
        notes: [],
      },
    ],
  };
  const items = check.diagnosticsByFile(document, PROJECT, linesOnDisk).get(MATHX);
  assert.deepEqual(items[0].range, {
    start: { line: 4, character: 7 },
    end: { line: 4, character: 10 },
  });
  assert.equal(linesOnDisk(MATHX)[4].slice(7, 10), 'add');
});

// A document from an editor session names lines a file rewritten since may not
// have, and a range that cannot be shown is worse than one that is off.
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

// The closure holds the standard library, which the compiler read from its own
// directory in the guest: those paths name nothing the editor can open, and a
// squiggle it cannot show is worse than none.
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

// ---- the grouping -----------------------------------------------------------

// Every file of the closure gets an entry, so publishing the map clears the
// file that was fixed as well as painting the one that is broken (D20.2).
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

// A note stays with its error, carrying the second place it names. The real
// document is why: this note has the *same* range as its error, so publishing it
// separately would put two squiggles over one span and two rows in Problems with
// nothing saying they are one diagnostic.
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

// A standalone note is a diagnostic of severity `note` in the document itself
// (D20.2), and it must not be painted as an error.
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
