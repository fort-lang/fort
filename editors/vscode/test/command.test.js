'use strict';

// The command the extension runs on save, and the settings it is built from.

const test = require('node:test');
const assert = require('node:assert/strict');

const commands = require('../lib/command');
const paths = require('../lib/paths');

const SETTINGS = {
  sshConfig: '/home/user/fort/.vagrant/ssh-config',
  host: 'default',
  compiler: '/vagrant/build/debug/fort',
  stdDir: '/vagrant/build/debug/std',
  includeDirs: [],
};

test('the command is ssh with a multiplexed connection and the check mode', () => {
  const command = commands.checkCommand(SETTINGS, '/home/user/fort/std/io.ft', '/tmp');
  assert.equal(command.command, 'ssh');
  assert.deepEqual(command.args, [
    '-F',
    '/home/user/fort/.vagrant/ssh-config',
    '-o',
    'ControlMaster=auto',
    '-o',
    'ControlPath=/tmp/fort-%C',
    '-o',
    'ControlPersist=10m',
    '--',
    'default',
    '/vagrant/build/debug/fort --check --json --index --std-dir /vagrant/build/debug/std' +
      ' /home/user/fort/std/io.ft',
  ]);
});

// A `.vscode/settings.json` travels with a cloned repository, so the host name
// is attacker-controlled input: without the terminator, ssh reads a host of
// `-oProxyCommand=...` as an option and runs it on the host machine.
test('the host is terminated, so a hostile one cannot become an ssh option', () => {
  const hostile = Object.assign({}, SETTINGS, { host: '-oProxyCommand=touch /tmp/pwned' });
  const command = commands.checkCommand(hostile, '/a/b.ft', '/tmp');
  const at = command.args.indexOf('-oProxyCommand=touch /tmp/pwned');
  assert.ok(at > 0);
  assert.equal(command.args[at - 1], '--');
  assert.equal(command.args.indexOf('--'), command.args.length - 3);
  for (const arg of command.args.slice(0, command.args.indexOf('--'))) {
    assert.notEqual(arg, hostile.host);
  }
});

test('every include directory becomes its own -I', () => {
  const settings = Object.assign({}, SETTINGS, { includeDirs: ['/vagrant/std', '/vagrant/lib'] });
  const words = commands.remoteWords(settings, '/a/b.ft');
  assert.deepEqual(words, [
    '/vagrant/build/debug/fort',
    '--check',
    '--json',
    '--index',
    '--std-dir',
    '/vagrant/build/debug/std',
    '-I',
    '/vagrant/std',
    '-I',
    '/vagrant/lib',
    '/a/b.ft',
  ]);
});

test('an empty standard library setting drops the option', () => {
  const settings = Object.assign({}, SETTINGS, { stdDir: '' });
  assert.deepEqual(commands.remoteWords(settings, '/a/b.ft'), [
    '/vagrant/build/debug/fort',
    '--check',
    '--json',
    '--index',
    '/a/b.ft',
  ]);
});

test('a word the shell would read is quoted, an ordinary one is not', () => {
  assert.equal(commands.shellQuote('/vagrant/build/debug/fort'), '/vagrant/build/debug/fort');
  assert.equal(commands.shellQuote('--std-dir'), '--std-dir');
  assert.equal(commands.shellQuote('/a b/c.ft'), "'/a b/c.ft'");
  assert.equal(commands.shellQuote('a;rm -rf /'), "'a;rm -rf /'");
  assert.equal(commands.shellQuote("it's.ft"), "'it'\\''s.ft'");
  assert.equal(commands.shellQuote('$HOME/x.ft'), "'$HOME/x.ft'");
  assert.equal(commands.shellJoin(['a', 'b c']), "a 'b c'");
});

test('a path with a space survives the remote command line', () => {
  const command = commands.checkCommand(SETTINGS, '/home/user/my fort/a.ft', '/tmp/x');
  assert.equal(
    command.args[command.args.length - 1],
    "/vagrant/build/debug/fort --check --json --index --std-dir /vagrant/build/debug/std" +
      " '/home/user/my fort/a.ft'"
  );
  assert.equal(command.args[5], 'ControlPath=/tmp/x/fort-%C');
  assert.equal(command.args[command.args.length - 3], '--');
});

// The bug this measures: on macOS `os.tmpdir()` is
// `/var/folders/<two>/<hash>/T`, and ssh binds the control socket under a
// temporary name, so `<tmpdir>/fort-%C` does not fit a Unix domain socket path
// and every check fails with ssh's exit 255 -- which the extension reports as
// an unreachable VM, hiding the cause completely.
test('a macOS temporary directory is too deep for the control socket', () => {
  const macos = '/var/folders/6y/jxxbq7y547z69nkt88pbdr5m0000gn/T';
  const linux = '/tmp/fort-1000';
  assert.ok(commands.controlPathLength(commands.controlPathOf(macos)) > commands.SOCKET_PATH_MAX);
  assert.ok(commands.controlPathLength(commands.controlPathOf(linux)) <= commands.SOCKET_PATH_MAX);
  assert.equal(commands.controlDirectory(macos, linux), linux);
  assert.equal(commands.controlDirectory(linux, '/tmp/other'), linux);
});

test('the control path holds one hash and no doubled separator', () => {
  assert.equal(commands.controlPathOf('/tmp/fort-501'), '/tmp/fort-501/fort-%C');
  assert.equal(commands.controlPathOf('/tmp/fort-501/'), '/tmp/fort-501/fort-%C');
  assert.equal(commands.controlPathOf('/tmp//'), '/tmp/fort-%C');
  // `%C` is a 40-character hash and ssh adds a dot and sixteen characters.
  assert.equal(commands.controlPathLength('/tmp/fort-%C'), '/tmp/fort-'.length + 40 + 17);
});

test('the command names the control socket of the directory it was given', () => {
  const command = commands.checkCommand(SETTINGS, '/a/b.ft', '/tmp/fort-501');
  assert.equal(command.args[5], 'ControlPath=/tmp/fort-501/fort-%C');
});

test('the workspace folder is substituted into a setting', () => {
  assert.equal(
    commands.resolveSetting('${workspaceFolder}/.vagrant/ssh-config', '/home/user/fort'),
    '/home/user/fort/.vagrant/ssh-config'
  );
  assert.equal(commands.resolveSetting('/absolute/config', '/home/user/fort'), '/absolute/config');
  assert.equal(commands.resolveSetting('${workspaceFolder}', ''), '');
  assert.equal(commands.resolveSetting(undefined, '/x'), '');
});

test('settings are resolved with the workspace folder and defaulted', () => {
  const settings = commands.resolveSettings(
    {
      sshConfig: '${workspaceFolder}/.vagrant/ssh-config',
      host: 'default',
      compiler: '/vagrant/build/debug/fort',
      stdDir: '/vagrant/build/debug/std',
      includeDirs: ['${workspaceFolder}/lib'],
    },
    '/home/user/fort'
  );
  assert.deepEqual(settings, {
    sshConfig: '/home/user/fort/.vagrant/ssh-config',
    host: 'default',
    compiler: '/vagrant/build/debug/fort',
    stdDir: '/vagrant/build/debug/std',
    includeDirs: ['/home/user/fort/lib'],
  });
});

test('a missing setting is empty rather than undefined', () => {
  const settings = commands.resolveSettings({}, '/home/user/fort');
  assert.deepEqual(settings, {
    sshConfig: '',
    host: '',
    compiler: '',
    stdDir: '',
    includeDirs: [],
  });
});

test('a document path is made absolute against the checked file', () => {
  assert.equal(paths.resolveDocumentPath('/a/b.ft', '/base'), '/a/b.ft');
  assert.equal(paths.resolveDocumentPath('mathx.ft', '/base'), '/base/mathx.ft');
  assert.equal(paths.resolveDocumentPath('../lib/mathx.ft', '/base/src'), '/base/lib/mathx.ft');
});

// The map of published diagnostics is keyed by path, so two spellings of one
// file must not become two keys.
test('an absolute path is normalized rather than kept as it was spelled', () => {
  assert.equal(paths.resolveDocumentPath('/a/./b/../b.ft', '/base'), '/a/b.ft');
  assert.equal(paths.resolveDocumentPath('/a//b.ft', '/base'), '/a/b.ft');
});

test('only a .ft file is checked on save', () => {
  assert.equal(paths.isFortPath('/a/b.ft'), true);
  assert.equal(paths.isFortPath('/a/b.c'), false);
  assert.equal(paths.isFortPath('/a/ft'), false);
});
