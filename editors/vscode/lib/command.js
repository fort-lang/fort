'use strict';

// The command the extension runs on save.
//
// Nothing is built on the host, so the compiler that answers is the one in the
// VM (AGENTS.md, Environment): the extension reaches it over ssh with the
// configuration `vagrant ssh-config` wrote, and passes the host path of the
// file, which resolves in the guest because provisioning symlinks the host
// repository path to /vagrant. One multiplexed connection is shared by every
// save, so a check costs one round trip rather than a new ssh handshake.

const CONTROL_PERSIST = '10m';

// A Unix domain socket path is limited to 104 bytes on macOS and 108 on Linux,
// and ssh binds the socket under a temporary name of its own (`.` and sixteen
// characters) before renaming it, so a control path that does not leave room
// for that makes ssh exit 255 and every check read as an unreachable VM. macOS
// puts `os.tmpdir()` under `/var/folders/<...>/T`, which is 49 bytes before
// anything is added, so the directory is measured rather than assumed.
const SOCKET_PATH_MAX = 100;
const CONTROL_HASH_LENGTH = 40;
const SSH_TEMPORARY_SUFFIX = 17;
const CONTROL_NAME = 'fort-%C';

// The length ssh ends up binding for a control path holding one `%C`, which it
// expands to a 40-character hash.
function controlPathLength(controlPath) {
  return controlPath.length - '%C'.length + CONTROL_HASH_LENGTH + SSH_TEMPORARY_SUFFIX;
}

function controlPathOf(controlDir) {
  return controlDir.replace(/\/+$/, '') + '/' + CONTROL_NAME;
}

// Whichever of the two directories the control socket fits in: the preferred
// one when it does, the fallback when that one does, and the shorter of the two
// when neither does -- a fallback that overflows too would fail exactly as the
// preferred one did, with the same misleading message, so the caller is at
// least given the best chance available. The caller creates the directory.
function controlDirectory(preferred, fallback) {
  if (controlPathLength(controlPathOf(preferred)) <= SOCKET_PATH_MAX) return preferred;
  if (controlPathLength(controlPathOf(fallback)) <= SOCKET_PATH_MAX) return fallback;
  return preferred.length <= fallback.length ? preferred : fallback;
}

// A word of the remote command line, quoted for the shell ssh runs it under. A
// word of the shell's safe alphabet is left alone, so the usual command reads
// exactly as it is documented; anything else is single-quoted.
function shellQuote(word) {
  if (/^[A-Za-z0-9_@%+=:,./-]+$/.test(word)) return word;
  return "'" + word.replace(/'/g, "'\\''") + "'";
}

function shellJoin(words) {
  return words.map(shellQuote).join(' ');
}

// The two variables a setting may hold, substituted: `${workspaceFolder}` is
// the folder the file belongs to and `${fortVmDir}` is the directory the VM was
// brought up from, which is the main checkout when the workspace is a worktree
// (lib/vmdir.js). A setting that names neither is returned unchanged, and a VM
// directory nobody supplied falls back to the workspace folder, which is what a
// main checkout is.
function resolveSetting(value, workspaceFolder, vmDir) {
  if (typeof value !== 'string') return '';
  const folder = workspaceFolder || '';
  const vm = vmDir === undefined || vmDir === null ? folder : vmDir;
  return value.split('${workspaceFolder}').join(folder).split('${fortVmDir}').join(vm);
}

// The settings of the extension, resolved: `fort.vm.sshConfig`, `fort.vm.host`,
// `fort.compiler`, `fort.stdDir` and `fort.includeDirs`.
function resolveSettings(raw, workspaceFolder, vmDir) {
  const includeDirs = Array.isArray(raw.includeDirs) ? raw.includeDirs : [];
  return {
    sshConfig: resolveSetting(raw.sshConfig, workspaceFolder, vmDir),
    host: typeof raw.host === 'string' ? raw.host : '',
    compiler: typeof raw.compiler === 'string' ? raw.compiler : '',
    stdDir: typeof raw.stdDir === 'string' ? raw.stdDir : '',
    includeDirs: includeDirs.map((dir) => resolveSetting(dir, workspaceFolder, vmDir)),
  };
}

// The remote words: the check mode of D20.1 with the index of D20.3, which
// implies --check and --json, over the file the editor saved.
function remoteWords(settings, filePath) {
  const words = [settings.compiler, '--check', '--json', '--index'];
  if (settings.stdDir !== '') words.push('--std-dir', settings.stdDir);
  for (const dir of settings.includeDirs) words.push('-I', dir);
  words.push(filePath);
  return words;
}

// The whole command: ssh with connection multiplexing, whose control socket
// lives under `controlDir` and outlives the run by CONTROL_PERSIST. The `--`
// before the host is load-bearing: a `.vscode/settings.json` travels with a
// cloned repository, so a `fort.vm.host` of `-oProxyCommand=...` would
// otherwise be read by ssh as an option and run a command on the host, outside
// the VM every other rule here exists to stay inside.
function checkCommand(settings, filePath, controlDir) {
  const args = [
    '-F', settings.sshConfig,
    '-o', 'ControlMaster=auto',
    '-o', 'ControlPath=' + controlPathOf(controlDir),
    '-o', 'ControlPersist=' + CONTROL_PERSIST,
    '--', settings.host,
    shellJoin(remoteWords(settings, filePath)),
  ];
  return { command: 'ssh', args };
}

module.exports = {
  CONTROL_PERSIST,
  SOCKET_PATH_MAX,
  checkCommand,
  controlDirectory,
  controlPathLength,
  controlPathOf,
  remoteWords,
  resolveSetting,
  resolveSettings,
  shellJoin,
  shellQuote,
};
