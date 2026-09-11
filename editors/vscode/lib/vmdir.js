'use strict';

// Where the VM lives, seen from a workspace folder.
//
// Nothing is built on the host, and the one VM every worktree shares has its
// directory at `$FORT_VM_DIR` if that is set and at the main checkout of the
// repository otherwise (AGENTS.md, Environment). `vagrant ssh-config` writes
// `.vagrant/ssh-config` there and nowhere else, so a default that named
// `<workspace>/.vagrant/ssh-config` would name a file that does not exist
// whenever VS Code is opened on a worktree -- which is this project's normal
// working mode -- and every check would report an unreachable VM.

const path = require('path');

// The `gitdir:` line a worktree's `.git` file holds, resolved against the
// worktree; the empty string when `.git` is a directory, as it is in a main
// checkout, or cannot be read. `readFile` answers a path with its text or
// null.
function gitDirOf(workspaceFolder, readFile) {
  const text = readFile(path.join(workspaceFolder, '.git'));
  if (typeof text !== 'string') return '';
  const match = /^\s*gitdir:\s*(.+?)\s*$/m.exec(text);
  if (match === null) return '';
  return path.resolve(workspaceFolder, match[1]);
}

// The main checkout a workspace folder belongs to. A worktree's git directory
// is `<main>/.git/worktrees/<name>`, so the main checkout is the parent of the
// `.git` above `worktrees`; anything else is its own main checkout.
function mainCheckoutOf(workspaceFolder, readFile) {
  const gitDir = gitDirOf(workspaceFolder, readFile);
  if (gitDir === '') return workspaceFolder;
  const marker = path.sep + 'worktrees' + path.sep;
  const at = gitDir.lastIndexOf(marker);
  if (at < 0) return workspaceFolder;
  return path.dirname(gitDir.slice(0, at));
}

// The VM directory of a workspace folder: `$FORT_VM_DIR` wins, as it does for
// `tools/vm` itself, and the main checkout answers otherwise.
function vmDirectory(workspaceFolder, env, readFile) {
  const configured = env && typeof env.FORT_VM_DIR === 'string' ? env.FORT_VM_DIR.trim() : '';
  if (configured !== '') return configured;
  if (!workspaceFolder) return '';
  return mainCheckoutOf(workspaceFolder, readFile);
}

module.exports = { mainCheckoutOf, vmDirectory };
