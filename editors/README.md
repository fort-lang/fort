# Editor support for fort

`editors/vscode/` is a complete, minimal VS Code extension: it declares the language `fort` for
the `.ft` extension, gives it `//` comment toggling, bracket matching and auto-closing pairs
(`language-configuration.json`), and contributes the TextMate grammar in
`syntaxes/fort.tmLanguage.json`. There is no language server, no compiler involvement and no
build step: the colours appear the moment a `.ft` file is opened.

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
