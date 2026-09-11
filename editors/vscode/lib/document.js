'use strict';

// The check document and what a run of the compiler means.
//
// `fort --check --json --index` writes one JSON document to stdout and nothing
// else, and stdout is a complete document or empty and never a truncated one,
// so a client tells a crash (exit 2, empty stdout) from a verdict (exit 0 or 1,
// one document) by the pair (D20.1, D20.2).

const DOCUMENT_VERSION = 1;

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

function isSymbol(value) {
  if (!isPosition(value)) return false;
  if (typeof value.name !== 'string' || typeof value.kind !== 'string') return false;
  if (!(value.type === null || typeof value.type === 'string')) return false;
  if (typeof value.is_decl !== 'boolean') return false;
  return value.decl === null || isPosition(value.decl);
}

// The document of D20.2, or null when stdout is not one. Every member is
// checked, because an editor that trusts a half-shaped document renders
// nonsense in the file the user is looking at.
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
  if (!Array.isArray(value.symbols) || !value.symbols.every(isSymbol)) return null;
  return {
    version: value.version,
    files: value.files,
    diagnostics: value.diagnostics,
    symbols: value.symbols,
  };
}

// What one run of the command means. `result` is `{status, stdout, stderr,
// error}`: `error` is set when the process could not be spawned or died by a
// signal, `status` is its exit status otherwise. The three verdicts are:
//
//   verdict      exit 0 or 1 and one document: publish it.
//   crash        exit 2, or any status with no document: the compiler or its
//                arguments are wrong (D20.1), so keep the diagnostics already
//                published and say so once.
//   unavailable  ssh could not be spawned or could not reach the VM (status
//                255 is ssh's own failure), so the same, with its own message.
function classifyResult(result) {
  if (result.error) {
    return { kind: 'unavailable', message: sshMessage(result.error.message) };
  }
  if (result.status === 255) {
    return { kind: 'unavailable', message: sshMessage(firstLine(result.stderr)) };
  }
  const document = parseDocument(result.stdout);
  if (document === null) {
    return { kind: 'crash', message: crashMessage(result.status, result.stderr) };
  }
  if (result.status !== 0 && result.status !== 1) {
    return { kind: 'crash', message: crashMessage(result.status, result.stderr) };
  }
  return { kind: 'verdict', document };
}

function firstLine(text) {
  if (typeof text !== 'string') return '';
  const trimmed = text.trim();
  if (trimmed === '') return '';
  return trimmed.split('\n')[0];
}

function sshMessage(detail) {
  return detail === '' ? 'fort: the VM is unreachable' : 'fort: the VM is unreachable: ' + detail;
}

function crashMessage(status, stderr) {
  const detail = firstLine(stderr);
  const head = 'fort: check failed (exit ' + String(status) + ')';
  return detail === '' ? head : head + ': ' + detail;
}

module.exports = { DOCUMENT_VERSION, classifyResult, parseDocument };
