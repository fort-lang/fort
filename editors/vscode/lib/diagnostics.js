'use strict';

// The document's diagnostics, grouped by file and converted to editor ranges.

const positions = require('./positions');

// Every file the compiler read gets an entry, empty when it has no diagnostic
// left, so a client publishes the errors of the files that have them and
// clears the files that no longer do (D20.2). `lineSource` answers a file path
// with the lines of its text, or null when the editor cannot read it.
function diagnosticsByFile(document, lineSource) {
  const byFile = new Map();
  for (const file of document.files) byFile.set(file, []);
  for (const raw of document.diagnostics) {
    const file = raw.file;
    if (!byFile.has(file)) byFile.set(file, []);
    byFile.get(file).push(convertDiagnostic(raw, lineSource));
  }
  return byFile;
}

function convertDiagnostic(raw, lineSource) {
  return {
    file: raw.file,
    range: positions.toEditorRange(raw, lineSource(raw.file)),
    severity: raw.severity,
    message: raw.message,
    notes: raw.notes.map((note) => ({
      file: note.file,
      range: positions.toEditorRange(note, lineSource(note.file)),
      message: note.message,
    })),
  };
}

module.exports = { diagnosticsByFile };
