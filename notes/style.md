# fort style: comments, code conventions and text

This document holds the conventions for the text the project writes: comments in C and in fort,
the naming and formatting rules of each language, markdown, and commit messages. It is not
normative about the language; `spec/decisions.md` and `spec/grammar.md` win over it (D1.2).

Section 1 holds the comment policy, which is in force now (T-098). T-099 moved the convention
bullets of the `## Technical Standards` section of `AGENTS.md` into the headings after it. The
test bullets of that section went to `notes/testing.md` and the JavaScript conventions of the
extension went to `editors/README.md`, as the routing table of `AGENTS.md` sends them.

## 1. Comments

### 1.1 What this section governs

The rules below govern the comments of the compiled sources: `src/**`, `std/*.ft`, `runtime/*`,
and the C and fort files of `test/` that are code (`test/*.c`, `test/*.h`, `test/fort/**/*.ft`).

They do not govern a harness directive, which is the third kind of comment and has a format of
its own: `spec/toolchain.md` 7 gives the `//!`, `//|` and `//<` of a language test, and
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

The reader who wants the rule opens `spec/decisions.md` at the tag. That is what the log is for,
and a copy of the rule in a comment is a copy that drifts. The old rule asked for the copy:
`src/` held 2962 tagged comment lines on 2026-09-12 and 0 of them match the two expressions
above. T-101 rewrites them; until it lands, a file holds citations of both shapes.

### 1.5 The rule that this section replaced

- **Citing decisions in code**: **superseded by `notes/style.md` 1, the comment policy (T-098).**
  The old rule asked for "a phrase stating the rule" (`// pointers print as 0x + lowercase hex,
  0x0 for null (D11.7)`) and refused a bare tag list; it produced 2962 tagged comment lines in
  `src/`, of which 0 match the shape in force now. T-101 rewrites them. The rule in force: a
  citation is a tag on the line or above the line it governs, `// D17.5`, and it takes one clause
  after a colon only where the tag alone leaves the rule unclear, `// D17.5: an own lvalue moves`.
  Write every new comment to `notes/style.md` 1.

## 2. C sources

- clang-tidy's `readability-inconsistent-declaration-parameter-name` fires only under
  `tools/vm tidy`, late in the loop: when a definition renames a parameter, for instance to stop
  it shadowing a new file-scope static, rename it in the header too, in the same edit.
- Running `run-clang-tidy` by hand: its positional arguments are regexes matched against the
  absolute paths in `compile_commands.json`, so a relative path such as `../../test` silently
  selects nothing and reports success. Use `tools/vm tidy` or absolute guest paths.
- **C sources**: C11 (`-std=c11`, `_POSIX_C_SOURCE=200809L`), no third-party code, warnings are
  errors under both clang (default) and gcc (`gcc` preset). `_POSIX_C_SOURCE` alone does not
  make glibc declare `environ`: `<unistd.h>` guards it with `#ifdef __USE_GNU`, so a file that
  passes an environment to `posix_spawn` declares `extern char** environ;` itself, as POSIX
  allows. `realpath` is the same case (`<stdlib.h>` guards it with `__USE_XOPEN_EXTENDED`, which
  `_POSIX_C_SOURCE` does not set), and `src/bootstrap/modules.c` declares it the same way;
  check for that guard before calling any POSIX function the headers seem to be missing, rather
  than widening the feature macros. Names: functions, variables, parameters, fields and
  struct/union/enum tags lower_case;
  typedefs lower_case with a `_t` suffix; enum constants, file-scope constants (static or not),
  function-scope static constants and macros UPPER_CASE (`enum { BYTE_MASK = 0xFFU }`); local
  constants lower_case; macros private to a header end with an underscore (`TEST_LOG_`). Every
  non-void call result is used or discarded with `(void)` (`TEST_UNUSED` in tests); no magic
  numbers (0 to 4, powers of two, `1.0` and `100.0` are allowed); uppercase literal suffixes;
  comments are `//` only, never `/* */`, as in fort (D2.2), so a region is commented out line by
  line; includes grouped as the file's own header, `<x.h>`, `<sys/x.h>`, project `"x.h"`, then
  `"test.h"`/`"common.h"`.
  `.clang-format` and `.clang-tidy` (clang 18) are the reference, and clang 18 in the guest is the
  only authority: the host's clang is newer and its editor diagnostics report checks the gate does
  not, so a warning that appears in an IDE and nowhere in `tools/vm tidy` is a version difference
  and not a finding. Seen with `bugprone-narrowing-conversions` on an `int`-to-`char` ternary in
  `test/lang/ffi/layout.c`, flagged on the host and silent under `clang-tidy-18` in the guest, where
  `WarningsAsErrors: '*'` means a real one would have failed the gate. Check in the guest before
  acting on an editor's warning, and never edit code to satisfy a check the project does not run.
  `tools/vm format` reformats,
  and `tools/check_comments.py` rejects a block comment (`tools/vm format-check` runs it; it
  skips a `/*` inside a string literal, a character literal or a `//` comment). This applies to
  test helpers under `test/` too. Two gaps of clang-tidy 18 are covered by review:
  `bugprone-unused-return-value` takes function names, not patterns (patterns arrive in
  clang-tidy 19), so `.clang-tidy` lists the C library and POSIX functions and the project's own
  functions are unchecked; and **clang-tidy 18's macro-argument blind spot covers
  `readability-identifier-naming` as well as `readability-magic-numbers`**, so nothing inside a
  `TEST(name, { ... })` body is checked for either -- a `static const char program[]` there draws
  no `invalid case style for static constant` while the same declaration at ordinary source
  location does. An experiment that renames an identifier *inside* a `TEST` body and sees no
  complaint has measured nothing; move the declaration out, or read the convention off a
  comparable one in a `main` (`SOURCE` in `test/gen_control_test.c`).
  The bootstrap must also stay transliterable into fort: no unions, no macro tricks, and no
  compiler builtin fort lacks (function-pointer tables are fine, the bootstrap subset has
  function pointers). `__builtin_clzll` was removed for that reason: `mag > (UINT64_MAX >> n)`
  says the same thing. The whole list, and what replaces each construct, is **Transliterating the
  bootstrap into fort** below; consult it before writing a C file the port will have to carry.

## 3. fort sources

- **fort sources**: identifier conventions per decision D1.4: everything is lower_case with
  underscores, struct and enum type names and enum members included; only module constants are
  UPPER_CASE. A variable never takes its type's name (`point p`, `box bx`, `list mut* mut l`); a
  field may (`node mut* own node`), since fields are not variables and are outside the module
  namespace (D7.9). `tools/fort_lint.py` enforces exactly that, and the 100-column wrap, over
  `std/` and `src/fort/`; nothing formats `.ft`, so indentation and spacing are still written by
  hand and read by review. Run it with `tools/vm fort-lint`, or by hand as
  `tools/vm run 'python3 tools/fort_lint.py --fort build/debug/fort <file.ft>'`.

## 4. Markdown and specification text

- **Markdown**: Line-wrap at 100 characters, including tables and code blocks. Check with
  `awk 'length > 100 {print FILENAME": "FNR}' <files>`. Code fences use `fort`, `c`, `sh`,
  `llvm`, `json` or `ebnf` as the language tag.
- **Language changes**: any change to the language is recorded in `spec/decisions.md` first
  (new decision number or amended decision with a note), then in the specification document that
  owns the topic, then in the tests under `test/lang/`. Specification text never contains "TBD",
  "pending" or "not finalized"; deferred features live only in decision D15.
- **The shape of a decision entry** (T-100): `### Dn.m Title`, then `- owner:` (the specification
  document that carries the full prose), then `- rule:` (the rule itself), then the optional
  `- rationale:` (why the rule is this one) and `- history:` (one dated note for each amendment,
  oldest first, and for each reading of the rule that a ticket had to settle -- D4.4 carries such a
  note from T-041, which changes no word of the rule and records how an `f32` context reads it).
  An entry always has an owner and a rule. `tools/check_decisions.py` checks that
  shape and the ctest `check_decisions` runs it over the log. An amendment rewrites the `rule` so
  that it states the rule in force, and appends a note to `history` that says what the rule said
  before: `Amended YYYY-MM-DD (T-nnn): <what changed>`. Never leave a stale sentence in the rule
  for the history to correct, and never delete the note that records the old text.
- **Moving text inside the decision log**: the risk of an edit to `spec/decisions.md` is a changed
  meaning, which no build and no test can see. `tools/check_decisions.py --against <old file>`
  compares the words of every entry between two revisions of the log. It fails on an entry whose
  words changed, and on an entry whose words only moved, because a permutation keeps every word and
  can still invert a rule: "widening extends, narrowing truncates" and "narrowing extends, widening
  truncates" are one multiset and two rules. Run it over a copy of the file before the edit, and
  name each entry the ticket edits on purpose: `--allow Dn.m` for a changed word and
  `--allow-move Dn.m` for words that only move between the fields. The flags are the record of
  what the ticket did, and the tool prints the difference each one excused.
- **Renaming a term**: sweep the stem, not the word. `grep -rni slice` never matches "Slicing",
  so a rename of `slice` to `span` must sweep `slic` (and any other inflected stem) with
  `grep -rni` before the criterion is ticked; markdown width is not covered by the gate, so run the
  `awk 'length > 100'` check over every file touched. The same holds for a change of syntax, and
  its hard half runs the other way: the decisions and sections that *define* the old form are the
  ones a sweep finds and the author rewrites, while the ones that merely *use* it -- an example, a
  table row, a history note quoting the wording it replaced -- keep the dead spelling and read as
  normative ever after. So sweep for the old form's *shape*, not for the name of the thing that
  changed, which the uses never mention: for the east-const amendment that search is
  `grep -rnE '\b(mut|own) ([iuf][0-9]+|bool|char|string)\b' notes/`, a qualifier standing before
  a base type. Read every hit, including those in decisions already marked amended -- T-069 found
  its leftovers inside decisions the original sweep had rewritten.
- **Moving a document**: cite a document by its section, never by a line number, and sweep for the
  old document's **name** as well as for the new path. T-099 moved the markdown width and the C
  comment rule out of `AGENTS.md` into this file. T-100 then swept `spec/` for the literal string
  `notes/` and saw neither D1.3 nor D2.2, because each cited `(AGENTS.md)` and named no path at
  all; `grep -rn 'AGENTS\.md' spec/` is the search that finds them, and it names the file the text
  left rather than the file it reached. A line number goes stale at the next edit of the file it
  points into, and it made these two citations stale twice, so `notes/style.md` 4 is a citation and
  `notes/style.md:158` is not (T-105).
- **Writing specification text**: cite the decision each rule implements as `(Dn.m)`. An agent
  that needs a rule the decision log does not settle uses the most conservative reading, marks it,
  and reports it to the lead for ratification; it never invents syntax or semantics. Every
  amendment to `spec/decisions.md` is relayed to agents still writing against the old text, and
  a separate audit pass reconciles the documents afterwards.

## 5. Shell scripts

- **Shell scripts**: bash with `set -eu`, clean under shellcheck at its default severity; the
  host has no shellcheck, run it in the guest: `tools/vm run 'shellcheck tools/vm
  tools/provision.sh'`.

## 6. Commit messages

- **Commit messages**: a title of about 50 characters (72 at most), a blank line, then a body
  wrapped at 72 columns that says what changed and why, then a single `Co-Authored-By:` trailer.
  No `Claude-Session:` trailer: the session URL is useless to anyone reading the history later
  and it is the only line in a commit that no reader can act on.
