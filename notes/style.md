# fort style: comments, code conventions and text

This document holds the conventions for the text the project writes: comments in C and in fort,
the naming and formatting rules of each language, markdown, and commit messages. It is not
normative about the language; `notes/decisions.md` and `notes/grammar.md` win over it (D1.2).

Section 1 holds the comment policy, which is in force now (T-098). It supersedes the "Citing
decisions in code" bullet of `AGENTS.md`. The other headings are empty: T-099 moves the
convention bullets of the `## Technical Standards` section of `AGENTS.md` into them. The test
bullets of that section go to `notes/testing.md` and the JavaScript conventions of the extension
go to `editors/README.md`, as the routing table of `AGENTS.md` sends them.

## 1. Comments

### 1.1 What this section governs

The rules below govern the comments of the compiled sources: `src/**`, `std/*.ft`, `runtime/*`,
and the C and fort files of `test/` that are code (`test/*.c`, `test/*.h`, `test/fort/**/*.ft`).

They do not govern a harness directive, which is the third kind of comment and has a format of
its own: `notes/toolchain.md` 7 gives the `//!`, `//|` and `//<` of a language test, and
`test/lang/run_tests_test.py`, `test/highlight_test.py` and the corpora under `test/` give the
`//@` and the `//^`. Measured on 2026-09-12, 4581 lines in 701 files under `test/` hold one:
2247 `//!`, 2088 `//|`, 122 `//@`, 90 `//^` and 34 `//<`. T-103 skips a comment that opens with
any of those five markers, wherever the file stands.

Block comments do not exist in either language: a comment is `//` and nothing else (D2.2), and
`tools/check_comments.py` rejects a `/* */` in the C sources.

### 1.2 The header block

Each file starts with one header block, in the shape clang uses: `//` lines at line 1 and no
decoration. The first line names the file and says what it holds in one sentence. Lines after it,
separated by a bare `//`, say why the file is the way it is: what it owns, what reads it, and the
decision it implements, cited as a tag in the shape of 1.4. A blank line may follow the block.

There is no line limit, because the length follows the design the file carries. Measured over the
72 files of `src/`, `std/` and `runtime/` on 2026-09-12: the median block is 14 lines, 37 blocks
pass 12 lines, and the longest is 79 (`src/fort/parser.ft`, the parser's recovery model). No
sweep shortens them. `src/bootstrap/check.c` is the one-line form and `src/bootstrap/sym.h`, at
29 lines, is the long form.

The block does not list the functions of the file, because that list goes out of date and the
file below it does not.

### 1.3 The two kinds of comment below the header

There are two, and the directives of 1.1 are the third kind outside this section.

- `///` on a declaration that other code calls: a function in a header, a public function or type
  of a fort module, a struct field whose meaning is not in its name. It states the contract: what
  the caller must hold true, what the callee gives back, and who owns the memory. Write it on the
  declaration, not on the definition, when the two are apart.
- `//` inside a body, where the logic is not clear from the code: an order that matters, a case
  that looks impossible and is not, a workaround and the thing it works around.

Write nothing that restates the code. `// increment the index` above `i += 1;` is deleted, not
reworded. A comment that repeats the name of the function it stands on is deleted with it.

### 1.4 Citations

A citation is a tag. Write `// D17.5`. Do not write a sentence.

Add one clause after a colon only where the tag alone leaves the rule unclear:
`// D17.5: an own lvalue moves`. The clause is 60 characters at most and it does not end in a
full stop. A citation of more than one decision is a list: `// D11.5, D11.7`. A ticket number is
a citation only with a clause, `// T-091: the runtime is a fort module`, because `.tickets/` is
outside the repository and the number alone points at a file the reader cannot open.

A citation stands on the line above the code it governs, or at the end of that line. T-103 lints
the shape below. It reads the comment from the `//` to the end of the line, and the line is a
citation when it matches `decisions` or `tickets`:

    tag       = D[0-9]+(\.[0-9]+)?
    ticket    = T-[0-9]{3}
    clause    = .{0,59}[^.]
    decisions = //(/)? <tag>(, <tag>)*(: <clause>)?$
    tickets   = //(/)? (<tag>|<ticket>)(, (<tag>|<ticket>))*: <clause>$

The reader who wants the rule opens `notes/decisions.md` at the tag. That is what the log is for,
and a copy of the rule in a comment is a copy that drifts. The old rule asked for the copy:
`src/` held 2962 tagged comment lines on 2026-09-12 and 0 of them match the two expressions
above. T-101 rewrites them; until it lands, a file holds citations of both shapes.

## 2. C sources

C11, the warning set, the naming conventions, the include order, and the clang-format and
clang-tidy configuration that are the reference.

## 3. fort sources

The identifier conventions of D1.4, the 100-column wrap, `tools/fort_lint.py`, and the rules that
review holds because no formatter exists for `.ft`.

## 4. Markdown and specification text

The 100-column wrap, the code fence tags, how a rule cites its decision, and the sweep a rename
of a term needs.

## 5. Shell scripts

`set -eu` and shellcheck at its default severity for `tools/*.sh` and `tools/vm`.

## 6. Commit messages

The title length, the body wrap, and the one `Co-Authored-By:` trailer.
