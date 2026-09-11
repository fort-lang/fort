'use strict';

// The VM directory a workspace folder belongs to, which is where
// `.vagrant/ssh-config` is, and the settings that are resolved against it.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const commands = require('../lib/command');
const vmdir = require('../lib/vmdir');

// A `.git` that is a worktree's file, as git writes it.
function worktreeGit(gitDir) {
  return (file) => (file === '/repo/.worktrees/fort-t061/.git' ? 'gitdir: ' + gitDir + '\n' : null);
}

test('a worktree resolves to the main checkout its .git points into', () => {
  const readFile = worktreeGit('/repo/.git/worktrees/fort-t061');
  assert.equal(vmdir.vmDirectory('/repo/.worktrees/fort-t061', {}, readFile), '/repo');
});

test('a relative gitdir is resolved against the worktree', () => {
  const readFile = worktreeGit('../../.git/worktrees/fort-t061');
  assert.equal(vmdir.vmDirectory('/repo/.worktrees/fort-t061', {}, readFile), '/repo');
});

test('a main checkout, whose .git is a directory, is its own VM directory', () => {
  assert.equal(vmdir.vmDirectory('/repo', {}, () => null), '/repo');
});

test('a .git file that is not a worktree leaves the folder alone', () => {
  const readFile = () => 'gitdir: /elsewhere/.git\n';
  assert.equal(vmdir.vmDirectory('/repo/sub', {}, readFile), '/repo/sub');
  assert.equal(vmdir.vmDirectory('/repo/sub', {}, () => 'nonsense\n'), '/repo/sub');
  assert.equal(vmdir.vmDirectory('/repo/sub', {}, () => ''), '/repo/sub');
});

test('FORT_VM_DIR wins, as it does for tools/vm', () => {
  const readFile = worktreeGit('/repo/.git/worktrees/fort-t061');
  const env = { FORT_VM_DIR: '/other/vm' };
  assert.equal(vmdir.vmDirectory('/repo/.worktrees/fort-t061', env, readFile), '/other/vm');
  assert.equal(vmdir.vmDirectory('/repo', { FORT_VM_DIR: '  ' }, () => null), '/repo');
  assert.equal(vmdir.vmDirectory('/repo', { FORT_VM_DIR: 5 }, () => null), '/repo');
});

test('no workspace folder and no FORT_VM_DIR is no VM directory', () => {
  assert.equal(vmdir.vmDirectory('', {}, () => null), '');
  assert.equal(vmdir.vmDirectory(undefined, undefined, () => null), '');
});

const WORKSPACE = path.join(__dirname, '..', '..', '..');

function readFile(file) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (error) {
    return null;
  }
}

// This one test reads the tree it is running in, so it is skipped where that
// tree is not a checkout -- a copy of the sources, or a packaged extension.
const NOT_A_CHECKOUT = !fs.existsSync(path.join(WORKSPACE, '.git'));

// The end of the chain: the default of `fort.vm.sshConfig` names the ssh
// configuration of the real VM whether this checkout is a worktree or not.
test('the default ssh config resolves to the VM directory of this checkout', {
  skip: NOT_A_CHECKOUT ? 'not a git checkout' : false,
}, () => {
  const workspaceFolder = WORKSPACE;
  const directory = vmdir.vmDirectory(workspaceFolder, {}, readFile);
  assert.ok(fs.existsSync(path.join(directory, 'Vagrantfile')), directory);
  assert.ok(fs.statSync(path.join(directory, '.git')).isDirectory(), directory);
  const settings = commands.resolveSettings(
    { sshConfig: '${fortVmDir}/.vagrant/ssh-config' },
    workspaceFolder,
    directory
  );
  assert.equal(settings.sshConfig, path.join(directory, '.vagrant', 'ssh-config'));
});

test('both variables are substituted, and a missing VM directory is the folder', () => {
  assert.equal(commands.resolveSetting('${fortVmDir}/x', '/w', '/vm'), '/vm/x');
  assert.equal(commands.resolveSetting('${workspaceFolder}/x', '/w', '/vm'), '/w/x');
  assert.equal(commands.resolveSetting('${fortVmDir}/x', '/w'), '/w/x');
  assert.equal(commands.resolveSetting('${fortVmDir}/x', '/w', null), '/w/x');
  assert.equal(commands.resolveSetting('${fortVmDir}:${workspaceFolder}', '/w', '/vm'), '/vm:/w');
});

test('include directories take the VM directory too', () => {
  const settings = commands.resolveSettings(
    { includeDirs: ['${fortVmDir}/std', '${workspaceFolder}/lib'] },
    '/w',
    '/vm'
  );
  assert.deepEqual(settings.includeDirs, ['/vm/std', '/w/lib']);
});
