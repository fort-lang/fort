# Editor support for fort

`editors/vscode/` is a complete, minimal VS Code extension. It declares the language `fort` for
the `.ft` extension, gives it `//` comment toggling, bracket matching and auto-closing pairs
(`language-configuration.json`), and contributes the TextMate grammar in
`syntaxes/fort.tmLanguage.json`; the colours appear the moment a `.ft` file is opened, with no
compiler involvement and no build step. `extension.js` adds the three things a compiler can
answer: diagnostics when a file is saved, hover, and go-to-definition. There is no language
server and no npm dependency -- the extension is plain JavaScript on the VS Code API, and one
batch run of `fort --check --json --index` is the whole interface (D20).

## Install

```sh
ln -s "$(git rev-parse --show-toplevel)/editors/vscode" ~/.vscode/extensions/fort-syntax
```

Then run *Developer: Reload Window* from the command palette. It took if the status bar of an open
`.ft` file says `fort`; if it says `Plain Text`, the symlink is in the wrong directory (VS Code
Insiders uses `~/.vscode-insiders/extensions`, and a remote window reads the extensions of the
remote host, `~/.vscode-server/extensions`). When something is coloured wrongly, put the cursor on
it and run *Developer: Inspect Editor Tokens and Scopes*: the popup names the scope the grammar
gave the token and the theme rule that painted it, which is the shortest path from a wrong colour
to the rule that produced it.

## Diagnostics, hover and definition

Nothing is built on the host, so the compiler that answers is the one in the development VM
(`AGENTS.md`, Environment). On every save of a `.ft` file the extension runs, in a child process
and never on the UI thread:

```sh
ssh -F <fort.vm.sshConfig> -o ControlMaster=auto -o ControlPath=<tmp>/fort-%C \
    -o ControlPersist=10m -- <fort.vm.host> \
    <fort.compiler> --check --json --index --std-dir <fort.stdDir> [-I <dir>]... <path>
```

The `--` before the host is deliberate: a `.vscode/settings.json` travels with a cloned
repository, so without it a `fort.vm.host` of `-oProxyCommand=...` would be read by ssh as an
option and run a command on the host machine, outside the VM.

`<tmp>` is a fresh directory made by `mkdtemp` once per window and removed when the extension is
deactivated. It is made rather than reused because the socket name is a hash of the local host, the
remote host, the port and the user, and so is predictable: a local user who owned the directory
first could leave a socket there for `ControlMaster=auto` to attach to, which would leak the
command line and let forged JSON come back as diagnostics and jump targets. `mkdtemp` picks a name
nobody can guess, creates it 0700 and fails rather than accepting a path that is already there,
which `mkdir` would do whatever its mode, its owner or whether it is a symlink. It is placed under
`os.tmpdir()` when a socket path fits there and under `/tmp` when it does not, which is the case on
macOS: `os.tmpdir()` is `/var/folders/<...>/T`, and a Unix domain socket path is limited to 104
bytes, so the control path overflows it and ssh exits 255 -- which reads as an unreachable VM and
says nothing about the real cause.

`<path>` is the host path of the saved file, which resolves inside the guest because provisioning
symlinks the host repository path to `/vagrant`. The connection is multiplexed and outlives the
run by ten minutes, so the second save costs one round trip rather than a new handshake. The
settings, all of them workspace settings:

| setting             | default                               | what it names                 |
| ------------------- | ------------------------------------- | ----------------------------- |
| `fort.vm.sshConfig` | `${fortVmDir}/.vagrant/ssh-config`    | the file `tools/vm up` cached |
| `fort.vm.host`      | `default`                             | the host inside that file     |
| `fort.compiler`     | `/vagrant/build/debug/fort`           | the compiler, in the guest    |
| `fort.stdDir`       | `/vagrant/build/debug/std`            | `--std-dir`, in the guest     |
| `fort.includeDirs`  | `[]`                                  | one `-I` each, in the guest   |

A setting may hold `${workspaceFolder}`, the folder the saved file belongs to, and `${fortVmDir}`,
the directory the VM was brought up from: `$FORT_VM_DIR` if it is set and the main checkout of the
repository otherwise, which is what `tools/vm` itself uses (`AGENTS.md`, Environment). The second
is the default of `fort.vm.sshConfig` because `.vagrant/` exists only in that directory: a window
opened on a worktree under `.worktrees/`, which is how this project is normally worked in, has no
`.vagrant/` of its own, and `${workspaceFolder}/.vagrant/ssh-config` would name a file that does
not exist, so every check would report an unreachable VM. The main checkout is found through the
worktree's `.git` file, which holds `gitdir: <main>/.git/worktrees/<name>`.

`fort.vm.host` is the `Host` entry of that file, which `vagrant ssh-config` always names
`default`; `fort-dev-fort` is the VirtualBox machine name and means nothing to `ssh -F`. The
extension does not fall back to `vagrant ssh` as `tools/vm` does, so a configuration cached before
the VM was recreated is fixed by `tools/vm up`, which rewrites it.

The compiler answers with one JSON document (D20.2): the files it read, the diagnostics, and one
record per resolved identifier (D20.3). A diagnostic is an error, or a note that follows no error
and stands on its own, which is shown as information rather than as another error. Its columns are
byte columns of the text the compiler read, so they are converted against the file on disk and not
against a buffer edited since; a jump target, which is shown to the reader rather than read from
the compiler, converts against the buffer. The extension publishes the diagnostics of every file in
that closure, which clears the squiggles of a file that is now clean, and caches the records per
file. Hover shows the record under the cursor as `kind name: type` in a fort code block -- a name
that denotes no value type, such as a module or a struct name, shows no type, and a declaration
that failed to check shows `unknown`. Go-to-definition jumps to the declaring name token, which
for a module is the top of its file and for a builtin is nowhere.

Two conversions matter and are what the unit tests are mostly about. The document counts 1-based
byte columns with a tab as one column (D20.2, D20.4) while VS Code counts 0-based UTF-16 units, so
a line holding `é` or `☃` converts only against its own text. And an empty range is a lexical
error's position rather than a zero-width construct, so it is expanded to the word at that
position and the error is visible.

## When the VM does not answer

A run that exits 2 with an empty stdout is a crash and not a verdict: the document is complete or
absent and never truncated (D20.2), so there is nothing to publish. The diagnostics already on
screen therefore stay, the status bar shows `fort: check failed` once with the reason in its
tooltip, and the *fort* output channel keeps the command line and the compiler's stderr. A dead
ssh (exit 255, or a connection that could not be spawned) reads as `fort: the VM is unreachable`
in the same place. The message disappears at the next successful check.

## Limitations of this path

The answers are a batch answer about the file as it was saved, so between two saves hover,
definition and the squiggles are stale by design, and an unsaved buffer is never checked. A hover
whose answer did not come from a check of the text on screen says so under the type -- *Answered
from an earlier check of this file.* -- rather than passing it off as current, which is what a
client owes the reader (`notes/toolchain.md` 9.2). That covers every way it can be old and not only
an unsaved edit: a check that failed since, a buffer reloaded from disk, and a file that no window
had open when it was checked all carry the marker, because the extension cannot know the answer is
about this text. The squiggles and F12 carry no such marker and are stale in the same way. A file
is only known once a check whose closure contained it has succeeded, so hover over a file never
saved yet answers nothing. Diagnostics of a file that leaves the closure -- an
import that was removed, say -- are left alone rather than cleared: the compiler said nothing about
that file this run, and no information is not the same as no errors, so the last thing it did say
stands until that file is itself checked again. Save it to clear it.

There is no rename, no completion, no semantic colouring and no workspace symbol search: all of
those need the language server of D20.5, which lands after the self-hosted compiler.

## The manual smoke test

`node --test` drives `extension.js` itself against a fake editor, but nothing can check that VS
Code calls it the way its API is documented to, so after a change to `extension.js` open VS Code on
the repository and check the five steps, with `tools/vm up` having
run at least once so `.vagrant/ssh-config` exists and `tools/vm build` having built the compiler:

1. Open `test/lang/run/modules/twofile/main.ft` and save it: no squiggle appears.
2. Break it -- rename `mathx.add` to `mathx.addd` -- and save: the squiggle covers `addd` and
   nothing else, and the message is the compiler's `unknown name`.
3. Undo and save: the squiggle disappears, in that file and in `mathx.ft`.
4. Hover `v`, the local: the popup reads `local v: vec`.
5. Put the cursor on `add` in `mathx.add(1, 2)` and press F12: `mathx.ft` opens on the name `add`
   of its declaration. Do the same on `mathx` itself: the file opens at its top.

If nothing happens at all, the *fort* output channel holds the command that ran and its stderr;
running that command by hand in a terminal is the next step.

## Which editors this covers

This grammar is read as it stands by VS Code and by JetBrains IDEs, whose TextMate bundle support
loads a `tmLanguage.json`. It does not cover the others, each for its own reason. Sublime Text
reads the old plist `.tmLanguage` or its own `.sublime-syntax`, not the JSON form, so it needs a
conversion of this file. GitHub's file view highlights only the languages Linguist ships a grammar
for, and fort is not one of them; that takes a Linguist submission once fort is public. Zed,
Neovim and Helix are tree-sitter only. A tree-sitter grammar for fort is the follow-up and is a
transliteration of `notes/grammar.md`; it is deliberately not part of this extension.

## What the grammar knows

The grammar follows the lexical decisions directly: the keywords and the reserved words of D2.4 (a
reserved word is painted `invalid.illegal.reserved.fort`, so using one looks wrong on purpose), the
primitive type names, `//` comments as the only comment form and the adjacent pair `/*` as a
lexical error (D2.2), integer literals in all four bases with `_` separators and the leading-zero
rule (D2.5), float literals (D2.6), char and string literals with the D2.8 escapes (an unknown
escape, a char literal that is neither one printable byte nor one escape, and a string that runs to
the end of the line are painted invalid, D2.7 to D2.9), and every operator and punctuation mark of
D2.10, including `@` and `..`, split into rules ordered longest-first so that `+%=` wins over `+%`
and `+`. `mut` and `own` are keywords wherever they appear (D5.3, D17.2) and get their own scopes,
`storage.modifier.mut.fort` and `storage.modifier.own.fort`, so a theme can pick them out of a
declaration such as `node mut* own mut@ own kids`.

Two shapes in the file are load-bearing. A rule that names keywords writes them as one word group,
`\b(?:a|b)\b` or `\b(a|b)`, and a rule that names operators or punctuation writes them as one
alternation of literals, `(?:\+|-)`. `test/highlight_test.py` reads those two shapes out of the
grammar and compares them with the lists of D2.4 and D2.10 in `notes/decisions.md`, so a keyword
added to the decision log and not to the grammar fails the build (ctest `highlight_selftest`); a
rule whose scope is in a keyword or operator family but whose pattern matches neither shape is an
error too, so a rule cannot escape the comparison by being written some other way.

## What the grammar cannot do

A regular expression sees tokens, not declarations, so it cannot tell a user-defined name's role
apart. In `node mut* p` it cannot know that `node` is a type and `p` a variable, and in
`math.vector w` it cannot know that `math` is a module rather than a variable holding a struct
with a `vector` field. The grammar therefore guesses from shape: an identifier directly before `*`
or `@`, an identifier before `mut` or `own`, an identifier before a name that ends a declarator,
and the left half of `mod.name` in those same positions are painted as types and namespaces.
Those guesses sometimes misfire — a product written without spaces, `x*y`, paints `x` as a type,
`y as i32` paints `y` as a type, and a field access in a declarator-shaped context paints the
object as a module. Everything else is left as `variable.other.fort`, including the field of a
`.` access, which is why a declaration such as `point p;` colours `point` while the `p` of a call
`sum(p)` stays a variable. Only semantic tokens from a language server, which type-check the file,
settle these cases; a TextMate grammar structurally cannot.

One caveat about the test: the TextMate engine in `test/highlight_test.py` runs the rules through
Python's `re`, a stand-in for the Oniguruma engine VS Code uses, so a pattern the two read
differently would pass the test and still misbehave in the editor. The grammar therefore sticks to
constructs both engines share -- character classes, non-capturing groups, `\b`, and lookahead --
and uses no back-reference, no `\G` and no variable-width lookbehind.
