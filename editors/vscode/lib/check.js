'use strict';

/** Converts compiler check documents into editor diagnostic records. */

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

/** Parses a complete check document. Returns null for missing or invalid output. */
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

/** Splits text into lines and removes the carriage return from each CRLF. */
function splitLines(text) {
  return text.split('\n').map((line) => (line.endsWith('\r') ? line.slice(0, -1) : line));
}

// Empty diagnostic ranges expand across these identifier characters.
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

// Converts a 1-based byte column to a 0-based UTF-16 offset.
// Values after the line clamp to its end. Values inside a character clamp to its start.
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

// Returns the identifier or character at an empty diagnostic range.
// At line end, it returns the preceding character. An empty line stays empty.
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

// Uses byte columns as character offsets when file text is unavailable.
// Expand an empty range so the diagnostic stays visible.
function asciiRange(position) {
  const start = { line: Math.max(0, position.line - 1), character: Math.max(0, position.col - 1) };
  const end = {
    line: Math.max(0, position.end_line - 1),
    character: Math.max(0, position.end_col - 1),
  };
  if (start.line === end.line && start.character === end.character) end.character += 1;
  return { start, end };
}

/** Converts a compiler position to a 0-based editor range. */
function toEditorRange(position, lines) {
  if (!lines) return asciiRange(position);
  const startLine = clampLine(lines, position.line);
  const endLine = clampLine(lines, position.end_line);
  // Clamp stale positions so the editor can still show the diagnostic.
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

function resolveDocumentPath(file, baseDir) {
  return path.isAbsolute(file) ? path.normalize(file) : path.resolve(baseDir, file);
}

function isUnder(absolute, baseDir) {
  const relative = path.relative(baseDir, absolute);
  return relative !== '' && !relative.startsWith('..') && !path.isAbsolute(relative);
}

/** Returns document files inside `baseDir` in compiler read order. */
function filesUnder(document, baseDir) {
  const files = [];
  for (const file of document.files) {
    const absolute = resolveDocumentPath(file, baseDir);
    if (isUnder(absolute, baseDir)) files.push(absolute);
  }
  return files;
}

/**
 * Groups diagnostics by absolute file path and converts their ranges.
 * Each document file inside `baseDir` gets an entry, including files with no diagnostics.
 * Files outside `baseDir` are omitted. A dropped error also drops its notes.
 */
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
