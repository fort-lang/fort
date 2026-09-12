# Editor support for fort

`editors/vscode/` is a complete, minimal VS Code extension with one job beyond colours: it shows
the compiler's diagnostics. It declares the language `fort` for the `.ft` extension, gives it `//`
comment toggling, bracket matching and auto-closing pairs (`language-configuration.json`), and
contributes the TextMate grammar in `syntaxes/fort.tmLanguage.json`; the colours appear the moment
a `.ft` file is opened, with no compiler involvement and no build step. `extension.js` adds the
diagnostics: it runs the compiler when a `.ft` file is opened and when it is saved, and paints what
it says. There is no language server, no npm dependency and no setting -- one run of `fort --check
--json` is the whole interface (D20.1, D20.2).

## Install

VS Code runs on the host, so the extension is installed on the host: symlink this directory into
the extensions directory of your own VS Code and reload the window.

```sh
ln -s "$(git rev-parse --show-toplevel)/editors/vscode" \
    ~/.vscode/extensions/fort.fort-syntax-<version>
```

`<version>` is the `version` field of `editors/vscode/package.json`, which is where the one copy of
it lives; VS Code reads the directory name and expects it to match.

That path is the usual one and it is yours to confirm rather than mine to state: VS Code Insiders
uses `~/.vscode-insiders/extensions`, a VSCodium build uses `~/.vscode-oss/extensions`, and a
portable installation puts it under the `data/` directory beside the application. `ls` the
directory `code --list-extensions --show-versions` is reading from if you are unsure -- the
extensions already installed sit there under exactly the `<publisher>.<name>-<version>` form the
symlink above uses.

Then run *Developer: Reload Window* from the command palette. It took if the status bar of an open
`.ft` file says `fort`; if it says `Plain Text`, the symlink is in the wrong directory. When
something is coloured wrongly, put the cursor on it and run *Developer: Inspect Editor Tokens and
Scopes*: the popup names the scope the grammar gave the token and the theme rule that painted it,
which is the shortest path from a wrong colour to the rule that produced it.

## Diagnostics

Nothing is built on the host, so the compiler that answers is the one in the development VM
(`notes/environment.md` 1), and the repository already owns the crossing. On every open and every
save of a `.ft` file the extension runs, in a child process and never on the UI thread:

```sh
<workspace>/tools/vm run '/vagrant/build/release/fort --check --json <path in the workspace>'
```

with the working directory set to the workspace folder the file belongs to. `tools/vm run` finds
the VM directory and its cached ssh configuration itself and runs the command in the guest
directory matching the host's, which is why the extension knows nothing about ssh, about
`/vagrant`, or about which directory the VM was brought up from. One run costs about a tenth of a
second, measured from the host over this repository.

The path is relative to the workspace folder and quoted for the shell `tools/vm run` hands its
argument to. That relative path is the whole of the host-to-guest mapping, in both directions: the
compiler echoes each file exactly as it was given it (D14.2), so the document that comes back names
the file the same way and it resolves against the same workspace folder on the host. A file that
belongs to no workspace folder is not checked, since `tools/vm run` works in the directory matching
its own and there is nothing to say about such a file.

The compiler needs no other argument. It finds its standard library in the `std` directory beside
the binary (D14.1), and `/vagrant/build/release/std` sits next to `/vagrant/build/release/fort`, so
`--std-dir` is unnecessary; imports resolve from the importing file's directory (D9.2), so `-I` is
too. The answers therefore come from the **release** build in the main checkout: run `tools/vm
build release` after a merge to keep them current. A window opened on a worktree gets that same
compiler, since every worktree shares one VM and `/vagrant/build/release` is the main checkout's.

The compiler answers with one JSON document (D20.2): the files it read and the diagnostics. A
diagnostic is an error, or a note that follows no error and so stands on its own, which is shown as
information rather than as another error. A note belonging to an error is attached to it as related
information naming the second place it points at: the two often carry the same range, and two
squiggles over one span would say nothing about being one diagnostic. The extension publishes the
diagnostics of every file of the closure that lies in the workspace folder, which clears the
squiggles of a file that is now clean, and drops the rest: the closure includes the standard
library, whose paths name files in the guest that the host cannot open (`spec/toolchain.md` 9.2). A
note whose file is dropped that way goes with it, while its error stands.

Two conversions matter and are what the unit tests are mostly about. The document counts 1-based
byte columns with a tab as one column (D20.2, D20.4) while VS Code counts 0-based UTF-16 units, so
a line holding `é` or `☃` converts only against its own text, which is read from disk -- the text
the compiler read, not a buffer edited since. And an empty range is a lexical error's position
rather than a zero-width construct, so it is expanded to the word at that position and the error is
visible.

## When there is no answer

A run that produces no JSON document is no answer, and never an answer of "no errors": the document
is complete or absent and never truncated (D20.1). That covers a VM that is down, a
`/vagrant/build/release/fort` nobody has built, and a compiler that died. The diagnostics already
on screen therefore stay, and the *fort* output channel gets the command that ran, the reason, and
the command's stderr. The command there is the line to paste into a terminal in the workspace
folder; running it by hand is the next step.

## Limitations of this path

Two of them meet a first-time user before anything else. The workspace folder must be a checkout
that holds `tools/vm` and lies inside the VM directory -- the main checkout or a worktree under
`.worktrees/` -- because that is the program the extension spawns and the directory it works in; a
window opened anywhere else spawns an ENOENT on every open and every save, which the output channel
reports. And a `.ft` file belonging to no workspace folder, the one opened straight from disk above
all, is skipped in silence: nothing is painted and nothing is written to the channel, since there
is no folder to run a check from.

The answers are otherwise a batch answer about the file as it was saved, so between two saves the
squiggles are stale by design and an unsaved buffer is never checked. The VM must be up (`tools/vm
up`) and the release build must exist (`tools/vm build release`); neither is the extension's to
arrange, and without them nothing is painted and the output channel says why. Diagnostics of a file
that leaves the closure -- an import that was removed, say -- are left alone rather than cleared:
the compiler said nothing about that file this run, and no information is not the same as no errors,
so the last thing it did say stands until that file is itself checked again. Save it to clear it. A
diagnostic reported inside the standard library is dropped rather than shown, since its path is the
guest's.

There is no hover, no go-to-definition, no rename, no completion, no semantic colouring and no
workspace symbol search: those need the language server of D20.5, which lands after the self-hosted
compiler.

## The manual smoke test

`node --test` drives `extension.js` itself against a fake editor, but nothing can check that VS
Code calls it the way its API is documented to, so after a change to `extension.js` open VS Code on
the repository and check these four steps, with `tools/vm up` having run and `tools/vm build
release` having built the compiler:

1. Open `test/lang/run/modules/twofile/main.ft`: no squiggle appears.
2. Break it -- rename `mathx.add` to `mathx.addd` -- and save: the squiggle covers `addd` and
   nothing else, and the message is the compiler's `unknown name`.
3. Undo and save: the squiggle disappears, in that file and in `mathx.ft`.
4. Halt the VM (`tools/vm halt`), save again: the squiggles that were on screen stay, and the
   *fort* output channel holds the `tools/vm run` line and the failure. Bring it back up
   (`tools/vm up`) and save once more: the answers return.

## Which editors this covers

This grammar is read as it stands by VS Code and by JetBrains IDEs, whose TextMate bundle support
loads a `tmLanguage.json`. It does not cover the others, each for its own reason. Sublime Text
reads the old plist `.tmLanguage` or its own `.sublime-syntax`, not the JSON form, so it needs a
conversion of this file. GitHub's file view highlights only the languages Linguist ships a grammar
for, and fort is not one of them; that takes a Linguist submission once fort is public. Zed,
Neovim and Helix are tree-sitter only. A tree-sitter grammar for fort is the follow-up and is a
transliteration of `spec/grammar.md`; it is deliberately not part of this extension.

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
grammar and compares them with the lists of D2.4 and D2.10 in `spec/decisions.md`, so a keyword
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

## Conventions for these sources

- The VS Code extension's sources are plain JavaScript wrapped at 100 columns, and no gate target
  lints them, so the conventions are here: `'use strict'` at the top of every file, CommonJS
  (`require`/`module.exports`, no ESM and no bundler), `//` comments only as in C and fort (D2.2),
  two-space indentation, single quotes, semicolons, `const` unless a binding is reassigned, no npm
  dependency and no devDependency, and no API beyond Node's standard library and `vscode` (which
  only `extension.js` may require). A file is tested by `node --test` or it is `extension.js`.
  `test/package.test.js` asserts the last two by reading the sources, so a second
  `require('vscode')` or a second module under `lib/` is a red test.
