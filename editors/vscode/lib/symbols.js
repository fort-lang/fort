'use strict';

// The identifier index: what the name under the cursor means, and where it is
// declared (D20.3). Every record repeats the type and the declaration range of
// the name it resolves, so hover and definition are answered from the record
// under the cursor alone.

const positions = require('./positions');

// Whether `record` starts at or before the 1-based position `(line, column)`.
function startsAtOrBefore(record, line, column) {
  return record.line < line || (record.line === line && record.col <= column);
}

// The record whose own range covers the 0-based editor position `position`, or
// null when the position is on no indexed name. `records` are the records of
// one file, which the document orders by the start of the occurrence, so the
// only candidate is the last one starting at or before the position and this
// bisects rather than scanning (D20.3). The position is converted to a byte
// column against the line's text, because a record's columns are byte columns
// (D20.2). A position exactly on the exclusive end of a name -- the caret just
// after it, where VS Code still reports the word -- matches too, since no
// record covers it.
function recordAtPosition(records, position, lines) {
  const line = position.line + 1;
  const lineText = positions.lineTextAt(lines, line);
  const column = lines
    ? positions.characterToByteColumn(lineText, position.character)
    : position.character + 1;
  let low = 0;
  let high = records.length;
  while (low < high) {
    const mid = Math.floor((low + high) / 2);
    if (startsAtOrBefore(records[mid], line, column)) low = mid + 1;
    else high = mid;
  }
  if (low === 0) return null;
  const record = records[low - 1];
  if (record.line !== line) return null;
  if (column >= record.col && column <= record.end_col) return record;
  return null;
}

// What a stale answer says about itself. The index is a batch answer about a
// file as some check read it, and a client says so rather than guessing
// (toolchain.md 9.2). The wording names no cause, because the extension knows
// only that the answer did not come from a check of this text: an unsaved edit,
// a check that failed since, and a file reloaded from disk all read the same
// from here.
const STALE_NOTE = '_Answered from an earlier check of this file._';

// The hover text of a record: `kind name: type` in a fort code block, followed
// by the note above when `stale` is true. The type is the declaration's own
// spelling (D5.2, D5.3); it is empty for a name that denotes no value type,
// which then reads as `struct vec` with nothing after it, and null when the
// declaration failed to check, which reads as unknown (D20.3).
function hoverText(record, stale) {
  const head = record.kind + ' ' + record.name;
  let line = head;
  if (record.type === null) line = head + ': unknown';
  else if (record.type !== '') line = head + ': ' + record.type;
  const block = '```fort\n' + line + '\n```';
  return stale ? block + '\n\n' + STALE_NOTE : block;
}

// Where going to the definition of a record lands: the range of the declaring
// name token, converted against the text of the declaring file. It is null for
// a builtin, which no source declares, and the empty range at 1:1 of a
// module's file, which opens that file at the top (D20.3). `lineSource`
// answers a file path with its lines, or null.
function definitionLocation(record, lineSource) {
  if (!record || record.decl === null) return null;
  const decl = record.decl;
  const lines = lineSource(decl.file);
  const isModuleFile =
    decl.line === 1 && decl.col === 1 && decl.end_line === 1 && decl.end_col === 1;
  const range = isModuleFile
    ? { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } }
    : positions.toEditorRange(decl, lines);
  return { file: decl.file, range };
}

// The records of one file, in the document's order (D20.3).
function recordsForFile(symbols, file) {
  return symbols.filter((record) => record.file === file);
}

module.exports = { STALE_NOTE, definitionLocation, hoverText, recordAtPosition, recordsForFile };
