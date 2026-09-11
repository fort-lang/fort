'use strict';

// Converting the compiler's positions to the editor's.
//
// A position in the check document is a 1-based byte column of the line it
// names, with the end of a range exclusive (D20.2, D20.4). VS Code counts
// 0-based UTF-16 code units from the start of the line, so a line holding a
// multi-byte character or a tab converts wrongly if a byte is taken for a
// character: a tab is one byte and one column (D20.4), while `é` is two bytes
// and one code unit and an emoji is four bytes and two code units.

// Word characters are the identifier characters of D2.3, which is what an
// empty range is expanded over.
function isWordCharacter(ch) {
  return /^[A-Za-z0-9_]$/.test(ch);
}

// The number of UTF-8 bytes the given code point occupies.
function utf8Length(codePoint) {
  if (codePoint < 0x80) return 1;
  if (codePoint < 0x800) return 2;
  if (codePoint < 0x10000) return 3;
  return 4;
}

// The UTF-8 byte length of one code point of `text` starting at `offset`.
function utf8LengthAt(text, offset) {
  return utf8Length(text.codePointAt(offset));
}

// The UTF-16 length of one code point of `text` starting at `offset`.
function codePointLengthAt(text, offset) {
  return text.codePointAt(offset) > 0xFFFF ? 2 : 1;
}

// The 0-based UTF-16 offset of the 1-based byte column `byteColumn` in
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

// The inverse: the 1-based byte column of the 0-based UTF-16 offset
// `character`, so a position the editor reports can be compared with the
// columns of the document.
function characterToByteColumn(lineText, character) {
  let byte = 1;
  let offset = 0;
  while (offset < lineText.length && offset < character) {
    byte += utf8LengthAt(lineText, offset);
    offset += codePointLengthAt(lineText, offset);
  }
  return byte + Math.max(0, character - offset);
}

// The text of the 1-based line `line` of `lines`, or the empty string when the
// document is shorter than the compiler's copy of it was.
function lineTextAt(lines, line) {
  if (!lines || line < 1 || line > lines.length) return '';
  return lines[line - 1];
}

// The 0-based line of a 1-based one, clamped to the document.
function clampLine(lines, line) {
  const last = lines && lines.length > 0 ? lines.length : 1;
  if (line < 1) return 0;
  if (line > last) return last - 1;
  return line - 1;
}

// The word around the 0-based offset `character`, as a pair of offsets. An
// empty range is a lexical error's position rather than a zero-width construct
// (D14.2), so it is expanded here: over the identifier under the position, or
// over the single character there when that is not an identifier character, or
// over the character before it at the end of a line. An empty line has nothing
// to underline and keeps the zero-width range.
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

// The UTF-16 length of the code point that ends at `offset`.
function codePointLengthBefore(text, offset) {
  const unit = text.charCodeAt(offset - 1);
  const isLowSurrogate = unit >= 0xDC00 && unit <= 0xDFFF;
  return isLowSurrogate && offset >= 2 ? 2 : 1;
}

// A document position `{file, line, col, end_line, end_col}` as a 0-based VS
// Code range `{start: {line, character}, end: {line, character}}`, converted
// against `lines`, the text of its file split into lines. An empty range is
// expanded to the word at its position.
function toEditorRange(position, lines) {
  if (!lines) return asciiRange(position);
  const startLine = clampLine(lines, position.line);
  const endLine = clampLine(lines, position.end_line);
  // A line past the end of the buffer converts against the line it clamped to,
  // so a document older than the buffer still underlines something.
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

// The lines of a file's text, for the conversions above. The document counts
// lines as the lexer does (D2.9), so a `\r\n` file keeps its `\r` out of the
// line text.
function splitLines(text) {
  return text.split('\n').map((line) => (line.endsWith('\r') ? line.slice(0, -1) : line));
}

module.exports = {
  asciiRange,
  byteColumnToCharacter,
  characterToByteColumn,
  clampLine,
  isWordCharacter,
  lineTextAt,
  splitLines,
  toEditorRange,
  utf8Length,
  wordRangeAt,
};
