'use strict';

// The document of `fort --check --json` (D20.1, D20.2) turned into plain
// diagnostic records: for every file the compiler read, a list of
// `{range, severity, message, notes}` with a 0-based range, each note carrying a
// file, a range and a message of its own. This is the only part of the extension
// with a case analysis in it, so it lives here, away from the VS Code API, and
// `node --test` reaches it with no editor.
//
// Two conversions are what make it more than a JSON parse. A position in the
// document is a 1-based byte column of the line it names, with the end of a
// range exclusive (D20.2, D20.4), while VS Code counts 0-based UTF-16 code
// units, so a line holding a tab, an `é` or an emoji converts only against its
// own text. And an empty range is a lexical error's position rather than a
// zero-width construct (D14.2), so it is expanded to the word at that position:
// a zero-width squiggle is an error the reader cannot see.

const path = require('path');

const DOCUMENT_VERSION = 1;

// ---- the document -----------------------------------------------------------

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function isPosition(value) {
  if (!isObject(value)) return false;
  const numbers = ['line', 'col', 'end_line', 'end_col'];
  return typeof value.file === 'string' && numbers.every((k) => Number.isInteger(value[k]));
}

function isNote(value) {
  return isPosition(value) && typeof value.message === 'string';
}

function isDiagnostic(value) {
  if (!isNote(value)) return false;
  if (typeof value.severity !== 'string') return false;
  return Array.isArray(value.notes) && value.notes.every(isNote);
}

// The document, or null when stdout is not one -- a compiler that crashed, one
// that could not be spawned, and a truncated read all land here, since stdout
// is one complete document or nothing (D20.1). Every member is checked, because
// an editor that trusts a half-shaped document paints nonsense over the file
// the user is looking at.
function parseDocument(stdout) {
  if (typeof stdout !== 'string' || stdout.trim() === '') return null;
  let value = null;
  try {
    value = JSON.parse(stdout);
  } catch (error) {
    return null;
  }
  if (!isObject(value) || value.version !== DOCUMENT_VERSION) return null;
  if (!Array.isArray(value.files) || !value.files.every((f) => typeof f === 'string')) return null;
  if (!Array.isArray(value.diagnostics) || !value.diagnostics.every(isDiagnostic)) return null;
  return { version: value.version, files: value.files, diagnostics: value.diagnostics };
}

// ---- positions --------------------------------------------------------------

// The lines of a file's text. The document counts lines as the lexer does
// (D2.9), so a `\r\n` file keeps its `\r` out of the line text.
function splitLines(text) {
  return text.split('\n').map((line) => (line.endsWith('\r') ? line.slice(0, -1) : line));
}

// Word characters are the identifier characters of D2.3, which is what an empty
// range is expanded over.
function isWordCharacter(ch) {
  return /^[A-Za-z0-9_]$/.test(ch);
}

// The number of UTF-8 bytes the code point at `offset` occupies.
function utf8LengthAt(text, offset) {
  const codePoint = text.codePointAt(offset);
  if (codePoint < 0x80) return 1;
  if (codePoint < 0x800) return 2;
  if (codePoint < 0x10000) return 3;
  return 4;
}

// The UTF-16 length of the code point at `offset`.
function codePointLengthAt(text, offset) {
  return text.codePointAt(offset) > 0xFFFF ? 2 : 1;
}

// The 0-based UTF-16 offset of the 1-based byte column `byteColumn` of
// `lineText`. A column past the end of the line clamps to the end, which is
// what the exclusive end of a range on the last token needs; a column inside a
// multi-byte character clamps to the start of that character, since no offset
// can name a position inside one.
function byteColumnToCharacter(lineText, byteColumn) {
  if (byteColumn <= 1) return 0;
  let byte = 1;
  let offset = 0;
  while (offset < lineText.length) {
    if (byte >= byteColumn) return offset;
    const next = byte + utf8LengthAt(lineText, offset);
    if (next > byteColumn) return offset;
    byte = next;
    offset += codePointLengthAt(lineText, offset);
  }
  return lineText.length;
}

// The text of the 1-based line `line`, or the empty string when the file is
// shorter than the compiler's copy of it was.
function lineTextAt(lines, line) {
  if (!lines || line < 1 || line > lines.length) return '';
  return lines[line - 1];
}

// The 0-based line of a 1-based one, clamped to the file.
function clampLine(lines, line) {
  const last = lines && lines.length > 0 ? lines.length : 1;
  if (line < 1) return 0;
  if (line > last) return last - 1;
  return line - 1;
}

// The UTF-16 length of the code point that ends at `offset`.
function codePointLengthBefore(text, offset) {
  const unit = text.charCodeAt(offset - 1);
  const isLowSurrogate = unit >= 0xDC00 && unit <= 0xDFFF;
  return isLowSurrogate && offset >= 2 ? 2 : 1;
}

// The word around the 0-based offset `character`, as a pair of offsets: the
// identifier under the position, or the single character there when that is not
// an identifier character, or the character before it at the end of a line. An
// empty line has nothing to underline and keeps the zero-width range.
function wordRangeAt(lineText, character) {
  const at = Math.max(0, Math.min(character, lineText.length));
  let start = at;
  let end = at;
  if (at < lineText.length && isWordCharacter(lineText[at])) {
    end = at + 1;
  } else if (at > 0 && isWordCharacter(lineText[at - 1])) {
    start = at - 1;
    end = at;
  } else if (at < lineText.length) {
    return { start: at, end: at + codePointLengthAt(lineText, at) };
  } else if (at > 0) {
    return { start: at - codePointLengthBefore(lineText, at), end: at };
  } else {
    return { start: 0, end: 0 };
  }
  while (start > 0 && isWordCharacter(lineText[start - 1])) start -= 1;
  while (end < lineText.length && isWordCharacter(lineText[end])) end += 1;
  return { start, end };
}

// The fallback for a file whose text the editor could not read: a byte column
// is taken for a character offset, which is right for a line of ASCII and the
// closest reading available without the bytes. An empty range becomes one
// character wide, since an error must still be visible.
function asciiRange(position) {
  const start = { line: Math.max(0, position.line - 1), character: Math.max(0, position.col - 1) };
  const end = {
    line: Math.max(0, position.end_line - 1),
    character: Math.max(0, position.end_col - 1),
  };
  if (start.line === end.line && start.character === end.character) end.character += 1;
  return { start, end };
}

// A document position `{file, line, col, end_line, end_col}` as a 0-based range
// `{start: {line, character}, end: {line, character}}`, converted against
// `lines`, the text of its file split into lines, or null when there is none.
function toEditorRange(position, lines) {
  if (!lines) return asciiRange(position);
  const startLine = clampLine(lines, position.line);
  const endLine = clampLine(lines, position.end_line);
  // A line past the end of the file converts against the line it clamped to,
  // so a document older than the text still underlines something.
  const startText = lineTextAt(lines, startLine + 1);
  const endText = lineTextAt(lines, endLine + 1);
  const start = { line: startLine, character: byteColumnToCharacter(startText, position.col) };
  const end = { line: endLine, character: byteColumnToCharacter(endText, position.end_col) };
  if (start.line === end.line && start.character === end.character) {
    const word = wordRangeAt(startText, start.character);
    return {
      start: { line: startLine, character: word.start },
      end: { line: startLine, character: word.end },
    };
  }
  return { start, end };
}

// ---- the records ------------------------------------------------------------

// A path out of the document, made absolute against `baseDir`. The compiler
// names each file the path it opened it by -- the entry file as given on the
// command line (`toolchain.md` 2, D14.2) -- and it is given a path relative to
// `baseDir`, so what comes back is relative to it too and no host-to-guest
// mapping is needed in either direction.
function resolveDocumentPath(file, baseDir) {
  return path.isAbsolute(file) ? path.normalize(file) : path.resolve(baseDir, file);
}

// Whether an absolute path lies inside `baseDir`, which is the workspace folder.
// The caller reads this as "the editor can open this file", and the two coincide
// only because of how the closure is reached. A module is searched for in the
// entry file's directory, in each `-I` directory and in the standard library's
// (D9.2). The client does pass `-I` since T-092: a file that leaves a closure is
// checked again with the directory of the entry that painted it. Every such root
// is `path.relative(folder, path.dirname(file))` of a file already under the
// folder, so the root is under the folder too. Every file of the closure is
// therefore still either under the folder or in the compiler's own `std`
// directory. That directory is the machine the compiler ran on, not this one.
//
// A search root outside the folder, were one ever passed, would start silently
// dropping files the editor could have opened. The reader of that day needs to
// know it was this predicate.
function isUnder(absolute, baseDir) {
  const relative = path.relative(baseDir, absolute);
  return relative !== '' && !relative.startsWith('..') && !path.isAbsolute(relative);
}

// The files of `document` that lie under `baseDir`, as absolute paths, in the
// order the compiler read them and with whatever repetition the document holds.
// `"files"` is the set a client may clear and not the set it publishes
// (`spec/toolchain.md` 9.2). A caller that remembers a closure therefore
// remembers this and not the keys of `diagnosticsByFile`. A diagnostic may name
// a file the compiler never read, and such a file is in no closure.
//
// The one rule here is the folder: a path outside it names a file the editor
// cannot open, the standard library above all. Repetition is the caller's to
// deal with, and the caller builds a `Set`.
function filesUnder(document, baseDir) {
  const files = [];
  for (const file of document.files) {
    const absolute = resolveDocumentPath(file, baseDir);
    if (isUnder(absolute, baseDir)) files.push(absolute);
  }
  return files;
}

// The diagnostics of `document`, keyed by absolute file path, with the 0-based
// range of each. Every file the compiler read gets an entry, empty when it has
// no diagnostic left, so a client publishes the errors of the files that have
// them and clears the files that no longer do (D20.2); a file outside `baseDir`
// gets none, since it is not the reader's to open. A note stays with the error
// it follows, carrying the file and range of the second place it names (D20.2),
// and a note whose file is outside `baseDir` is dropped while its error stands.
// `lineSource` answers a file path with the lines of its text, or null when the
// text cannot be read.
//
// A dropped error takes its notes with it. That holds for a note that points
// into `baseDir`, and it is not an oversight. `spec/toolchain.md` 9.2 licenses
// a client that cannot open a file to drop that file's diagnostics. It licenses
// nothing else. D20.2 gives the severity `note` to a note that follows no
// error. A nested note carries no severity of its own.
// Publishing it alone would invent a record the document does not hold. The
// reader would see `previous declaration here`, with its error nowhere on the
// screen.
//
// The asymmetry with the other direction is real. An error in `baseDir` keeps
// its place when its note is dropped. The error says what is wrong, and the
// note only says where else to look. The direction that loses everything is
// the one where the compiler put the error in the standard library. That is the
// compiler's to change, and not the editor's to paper over (T-092).
function diagnosticsByFile(document, baseDir, lineSource) {
  const byFile = new Map();
  const texts = new Map();
  const linesOf = (file) => {
    if (!texts.has(file)) texts.set(file, lineSource(resolveDocumentPath(file, baseDir)));
    return texts.get(file);
  };
  const put = (file, record) => {
    const absolute = resolveDocumentPath(file, baseDir);
    if (!isUnder(absolute, baseDir)) return;
    if (!byFile.has(absolute)) byFile.set(absolute, []);
    byFile.get(absolute).push(record);
  };
  for (const file of document.files) {
    const absolute = resolveDocumentPath(file, baseDir);
    if (isUnder(absolute, baseDir)) byFile.set(absolute, []);
  }
  for (const raw of document.diagnostics) {
    const notes = [];
    for (const note of raw.notes) {
      const absolute = resolveDocumentPath(note.file, baseDir);
      if (!isUnder(absolute, baseDir)) continue;
      notes.push({
        file: absolute,
        range: toEditorRange(note, linesOf(note.file)),
        message: note.message,
      });
    }
    put(raw.file, {
      range: toEditorRange(raw, linesOf(raw.file)),
      severity: raw.severity,
      message: raw.message,
      notes,
    });
  }
  return byFile;
}

module.exports = {
  DOCUMENT_VERSION,
  diagnosticsByFile,
  filesUnder,
  isUnder,
  parseDocument,
  resolveDocumentPath,
  splitLines,
  toEditorRange,
};
