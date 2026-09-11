'use strict';

const path = require('path');

// A file path out of the check document, made absolute. The compiler echoes
// each path as it read it (D14.2), which is absolute when the command line was,
// and the extension always passes the host path of the saved file; a relative
// path is still resolved against the directory of the file that was checked, so
// a document produced by some other invocation is usable too.
function resolveDocumentPath(file, baseDir) {
  return path.isAbsolute(file) ? path.normalize(file) : path.resolve(baseDir, file);
}

// Whether a path is a fort source, which is what a save triggers a check for.
function isFortPath(file) {
  return path.extname(file) === '.ft';
}

module.exports = { isFortPath, resolveDocumentPath };
