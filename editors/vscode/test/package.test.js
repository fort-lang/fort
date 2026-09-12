'use strict';

// The manifest: the activation script, the language and grammar contributions,
// and the settings there are none of.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const MANIFEST = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));

test('the manifest names the activation script, which loads', () => {
  assert.equal(MANIFEST.main, './extension.js');
  assert.ok(fs.existsSync(path.join(ROOT, 'extension.js')));
  assert.deepEqual(MANIFEST.activationEvents, ['onLanguage:fort']);
});

test('the extension has no dependency to install', () => {
  assert.equal(MANIFEST.dependencies, undefined);
  assert.equal(MANIFEST.devDependencies, undefined);
  assert.equal(fs.existsSync(path.join(ROOT, 'node_modules')), false);
});

test('the language and the grammar are still contributed', () => {
  assert.deepEqual(MANIFEST.contributes.languages[0].extensions, ['.ft']);
  assert.equal(MANIFEST.contributes.grammars[0].scopeName, 'source.fort');
  assert.equal(MANIFEST.contributes.languages[0].configuration, './language-configuration.json');
});

// The compiler is a constant of extension.js, so there is nothing to set: a
// manifest that offered a setting would be offering one nothing reads.
test('the extension contributes no configuration at all', () => {
  assert.equal(MANIFEST.contributes.configuration, undefined);
  assert.deepEqual(Object.keys(MANIFEST.contributes), ['languages', 'grammars']);
  const text = fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8');
  assert.equal(/fort\.(vm|compiler|stdDir|includeDirs|transport)/.test(text), false);
});

test('the version says this is the stripped extension', () => {
  assert.equal(MANIFEST.version, '0.3.0');
  assert.match(MANIFEST.description, /diagnostics/);
});

// Everything else under editors/ wraps at 100 columns, and a JSON string
// cannot be wrapped, so the descriptions are kept short instead and the long
// form lives in editors/README.md.
test('no line of the manifest is wider than the rest of editors/', () => {
  const text = fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8');
  for (const line of text.split('\n')) assert.ok(line.length <= 100, line);
});

// One job means one require of the editor API and one pure module beside it
// (CLAUDE.md, the VS Code extension bullets).
test('only the activation script knows about the editor', () => {
  const sources = [path.join(ROOT, 'extension.js')];
  for (const name of fs.readdirSync(path.join(ROOT, 'lib'))) {
    sources.push(path.join(ROOT, 'lib', name));
  }
  // Written as a pattern rather than as the text itself, so that a search for
  // the editor API over the sources does not find this test.
  const editorApi = /require\(['"]vscode['"]\)/;
  const requiring = sources.filter((file) => editorApi.test(fs.readFileSync(file, 'utf8')));
  assert.deepEqual(requiring, [path.join(ROOT, 'extension.js')]);
  assert.deepEqual(fs.readdirSync(path.join(ROOT, 'lib')), ['check.js']);
});
