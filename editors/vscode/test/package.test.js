'use strict';

// The manifest: the activation script, the settings the ticket fixes and the
// grammar contribution that was there before it.

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
});

test('the settings are the five the extension reads, with their defaults', () => {
  const properties = MANIFEST.contributes.configuration.properties;
  assert.deepEqual(Object.keys(properties), [
    'fort.vm.sshConfig',
    'fort.vm.host',
    'fort.compiler',
    'fort.stdDir',
    'fort.includeDirs',
  ]);
  // `.vagrant/` exists only in the VM directory, which is the main checkout
  // when VS Code is opened on a worktree (AGENTS.md, Environment).
  assert.equal(properties['fort.vm.sshConfig'].default, '${fortVmDir}/.vagrant/ssh-config');
  // `vagrant ssh-config` names its one entry `default`; `fort-dev-fort` is
  // the VirtualBox machine name, which ssh -F knows nothing about.
  assert.equal(properties['fort.vm.host'].default, 'default');
  assert.equal(properties['fort.compiler'].default, '/vagrant/build/debug/fort');
  assert.equal(properties['fort.stdDir'].default, '/vagrant/build/debug/std');
  assert.deepEqual(properties['fort.includeDirs'].default, []);
  for (const property of Object.values(properties)) {
    assert.equal(typeof property.description, 'string');
  }
});

// Everything else under editors/ wraps at 100 columns, and a JSON string
// cannot be wrapped, so the descriptions are kept short instead and the long
// form lives in editors/README.md.
test('no line of the manifest is wider than the rest of editors/', () => {
  const text = fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8');
  for (const line of text.split('\n')) assert.ok(line.length <= 100, line);
});
