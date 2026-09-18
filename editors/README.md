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
<workspace>/tools/vm run '/vagrant/build/release/fort --check --json [-I <root>]... <path>'
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

The compiler needs one other argument at most. It finds its standard library in the `std` directory
beside the binary (D14.1), and `/vagrant/build/release/std` sits next to
`/vagrant/build/release/fort`, so `--std-dir` is unnecessary. A `-I` is on the line only for a file
the window has already seen inside another file's closure, which the section below explains. The
answers therefore come from the **release** build in the main checkout: run `tools/vm build
release` after a merge to keep them current. A window opened on a worktree gets that same
compiler, since every worktree shares one VM and `/vagrant/build/release` is the main checkout's.

The compiler answers with one JSON document (D20.2): the files it read and the diagnostics. A
diagnostic is an error, or a note that follows no error and so stands on its own, which is shown as
information rather than as another error. A note belonging to an error is attached to it as related
information naming the second place it points at: the two often carry the same range, and two
squiggles over one span would say nothing about being one diagnostic. The extension publishes the
diagnostics of every file of the closure that lies in the workspace folder, which clears the
squiggles of a file that is now clean, and drops the rest: the closure includes the standard
library, whose paths name files in the guest that the host cannot open (`spec/toolchain.md` 9.2).
A dropped error takes its notes with it. That holds even for a note that points into the workspace
folder. 9.2 licenses a client to drop the diagnostics of a file it cannot open, and it licenses
nothing else. Publishing the note alone would show `previous declaration here` with its error
nowhere on the screen.

A check answers about a closure and not about one file, so the extension orders its events per
file. A check publishes, and a close clears the file and asks about the closure that file's check
painted. Each takes a number, and only a newer event writes over an older one. A check still in
flight therefore cannot repaint what a later check or a close has settled.

A file may leave a closure: delete the `import mathx;` of `main.ft` and save. The squiggles of
`mathx.ft` came from a walk of `main.ft`'s closure, and that walk no longer reaches them. The
extension does not decide what those squiggles should become. It asks the compiler about
`mathx.ft`, in a run like any other, and publishes the answer. Nothing is cleared and nothing is
guessed in the meantime: the old squiggles stand until the answer arrives, as they do after any
save.

Closing `main.ft` does the same thing to the whole of its closure. The walk that painted
`mathx.ft` is over, the file that carried it is shut, and no run of it will speak again, so every
file that closure painted is checked in its own right and publishes its own answer. The closed file
itself is not asked about: the close takes its squiggles, and the reader has shut it. Opening it
again drops whatever those runs have not yet started, since the check the open starts is the newer
word about every file of that closure (T-106).

That costs one run for each file the event painted and no longer names, and `by` keeps the set
small. A run asks again only about its own paint. An error that a check of `mathx.ft` itself
reported already has a run behind it. A file you have closed carries no paint at all. A close of
`mathx.ft` asks nothing, because `mathx.ft` has walked no closure of its own and so has painted
nobody.

The runs go out one at a time, because a closure can be wide. `src/fort/main.ft` reads 23 files of
this repository. Deleting one `import` departs 22 at once, and each run opens its own ssh
connection to the guest. An open and a save still go out at once. The chain such a run can start
is finite. Every answer rewrites the closure of the file that was checked and moves `by` to it. A
hop therefore destroys the condition that let it happen.

Each run carries one thing beyond the file. A module is not a file that checks the same way on its
own. The first search root is always the directory of the file the compiler was given (D9.2).
`util/strings.ft` imports its sibling as `util.chars`, and resolves that import only from the
directory its entry file sits in. A check therefore records, for every file of its closure, the
root of that closure. An open, a save and a re-check of any of those files pass it on as `-I`. A
module that a check of `main.ft` painted is checked the way that closure checked it, and not on its
own, so it gets the squiggles that closure sees (T-111). The directory of the file being checked is
never on the command line, since D9.2 puts it first in every case and no option adds it or removes
it.

That root is one directory and it does not grow. The run that records it is the one the reader
pointed the editor at, the run nothing had painted, and the root is its directory. Every run after
it hands the same root on. A run must not add its own directory, which is the first root of that
one run by D9.2 and of no other: `a/x.ft` and `b/y.ft` of one closure would then be checked with
each other's directory, and `b.z` in `y.ft` would read `a/b/z.ft`, an answer from a root the
closure never searched.

A file the window has published about carries that root whether it is open or not, and a close does
not take it away: the closure that searched the directory searched it either way. A file the window
has never published about carries no root, which is what an open and a save did before, and the
limitation below says what that costs.

The bookkeeping costs one entry per file the window has checked or published about. It never costs
one entry per event. The keys are file paths. Closing a file writes the entry it already had, and a
file outside every workspace folder gets no entry at all.

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
arrange, and without them nothing is painted and the output channel says why. A file that leaves a
closure is checked again only by the check or the close of the file that dropped it. That event
must have painted it, and the first check of a file in a session asks about nothing else. A module
you delete from disk keeps its last squiggles. The run that asks about it finds no file, so there
is no answer to publish. The output channel carries the compiler's `cannot read` line.

A module the window has never seen inside a closure is checked on its own, with its own directory
as the only search root. Open `test/lang/run/modules/nested/util/strings.ft` in a fresh window and
it paints `module 'util.chars' not found` on correct code, because nothing has told the extension
that the root of that project is `nested/`. Check the entry file once -- open or save
`nested/main.ft` -- and the module is checked that closure's way from then on. Only a project model
would know the root of a project the window has not walked. No decision states one: D20.5 fixes
what the compiler's modules must be for a language server to be possible and leaves the server's
own structure undecided.
This shape is rare in this repository and ordinary in a program laid out in directories. 191 of the
887 `.ft` files tracked here paint a `module ... not found` when the compiler is pointed at them
alone, and 1 of the 191 has this shape. Counted from the main checkout, with the release build
current:

```sh
tools/vm run 'for f in $(git ls-files "*.ft"); do
    out=$(/vagrant/build/release/fort --check --json "$f")
    case "$out" in *"not found"*) echo "$f $out" ;; esac
done' > /tmp/painted.txt
wc -l < /tmp/painted.txt                                        # 191
grep -c "^test/fort/" /tmp/painted.txt                          # 170
grep -v "^test/fort/" /tmp/painted.txt | grep -c "module 'std\." # 16
```

170 of them are under `test/fort/`, whose imports need the `-I ../../src/fort` of their own test
directive, which no search root taken from a closure recovers. 16 name a `std.` module and answer
to `tools/vm build release` instead, the `std/` beside the release binary being older than the
checkout. That leaves 5: `src/lsp/json.ft`, which imports `flt` from `src/fort`; two fail tests and
one fixture that import a module which does not exist on purpose; and
`test/lang/run/modules/nested/util/strings.ft`, the one file with this shape.

A check of a module is faithful to the closure whose root it carries wherever one directory is
enough. It is not faithful in general. D9.2 puts the directory of the file being checked first,
ahead of every `-I`, and no option removes it. Take a project that repeats a path prefix:
`util/util/chars.ft` beside `util/chars.ft`. The check reads the nearer file there, and reports an
error the closure never saw. No layout in this repository does that. 0 of the 887 `.ft` files it
tracks repeat a directory segment in their path, counted with

```sh
git ls-files '*.ft' | awk -F/ '{for(i=1;i<NF;i++)for(j=i+1;j<NF;j++)if($i==$j){print;next}}'
```

while 9 directories do hold both `.ft` files and a sub-directory of `.ft` files, so the
multi-directory case itself is ordinary here and the repeating one does not occur:

```sh
dirs=$(git ls-files '*.ft' | xargs -n1 dirname | sort -u)
for d in $dirs; do echo "$dirs" | grep -q "^$d/" && echo "$d"; done | wc -l
```

Only a project model would fix the repeating layout, and the extension has none.

A diagnostic reported inside the standard library is dropped rather than shown, since its path is
the guest's. Its notes go with it wherever they point. An error in the standard library that names
a line of your own file therefore paints nothing at all. The checker puts that error in your own
file in every case it produces today.

There is no hover, no go-to-definition, no rename, no completion, no semantic colouring and no
workspace symbol search: those need the language server of D20.5, which lands after the self-hosted
compiler. The extension calls no `fort --index`, keeps no cache and reads no setting (T-089).

## The manual smoke test

`node --test` drives `extension.js` itself against a fake editor, but nothing can check that VS
Code calls it the way its API is documented to, so after a change to `extension.js` open VS Code on
the repository and check these six steps, with `tools/vm up` having run and `tools/vm build
release` having built the compiler:

1. Open `test/lang/run/modules/twofile/main.ft`: no squiggle appears.
2. Break it -- rename `mathx.add` to `mathx.addd` -- and save: the squiggle covers `addd` and
   nothing else, and the message is the compiler's `unknown name`.
3. Undo and save: the squiggle disappears, in that file and in `mathx.ft`.
4. Break `mathx.ft`: delete the `;` after `i32 LIMIT = 5`. Save `main.ft`. The squiggle appears in
   `mathx.ft`, the file you did not save. Now comment out every line of `main.ft` that names
   `mathx`, the `import` included. Save `main.ft`. The squiggle stays, because `mathx.ft` is still
   broken on disk and the run the departure asked for says so. Undo in `mathx.ft` and save it: the
   squiggle goes. Undo in `main.ft` and save it.
5. Open `test/lang/run/modules/nested/main.ft` and leave `util/strings.ft` shut. Comment out
   `import util.strings;` and the two lines that use `strings`, and save. Read the Problems panel,
   which lists the files with diagnostics whether they are open or not: `util/strings.ft` is not
   there. A `module 'util.chars' not found` against it means the re-check lost the `-I` of D9.2.
   Undo and save. Now open `util/strings.ft` itself: it has no squiggle either, because the check
   of it carries the directory of `main.ft`, which the check of `main.ft` recorded. Close it,
   re-open it: still none. Open it in a window where `nested/main.ft` has never been checked and
   it does paint that message, which is the limitation above and not a fault of this step. This
   step needs a human at a VS Code window and T-111 could not run it: the unit tests drive the
   extension against the fake editor and say nothing about what VS Code itself does. Read it as a
   step to perform, not as a check that has passed.
6. Halt the VM (`tools/vm halt`), save again: the squiggles that were on screen stay, and the
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
D2.10, including `@`, `..`, and `...`. The rules put `...` before `..` before `.`.
They put `+%=` before `+%` before `+`. `mut` and `own` are keywords wherever they appear
(D5.3, D17.2) and get their own scopes,
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
  (`require`/`module.exports`, no ESM and no bundler), two-space indentation, single quotes,
  semicolons, `const` unless a binding is reassigned, no npm dependency and no devDependency, and
  no API beyond Node's standard library and `vscode` (which only `extension.js` may require).
  Use JSDoc `/** ... */` for module documentation and needed public API documentation. Use `//`
  for internal details and function-body comments. Use a JSDoc tag only when it adds contract
  information. A file is tested by `node --test` or it is `extension.js`.
  `test/package.test.js` asserts the last two by reading the sources, so a second
  `require('vscode')` or a second module under `lib/` is a red test.
