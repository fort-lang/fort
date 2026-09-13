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
74 header blocks of `src/` and `std/` on 2026-09-12, after T-101 swept the 64 in `src/`: the
median block is 15 lines, 40 blocks pass 12 lines, and the longest is 87 (`src/fort/parser.ft`,
the parser's recovery model). No sweep shortens them. `src/bootstrap/check.c` is the one-line
form and `src/bootstrap/sym.h` is the long form. A header block is prose and its tags are
citation lines of their own, so T-101 took every tag the prose held inline and put it on a line
**after** the paragraph it belongs to, never before it: the first line of a file names the file.

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

**The mark is placed by position.** In a fort module every top-level declaration takes `///`,
because D9.6 exports everything at module level and a declaration is public whether or not a caller
exists today. In a C header a declaration takes `///`, a struct field takes `///`, and a comment
inside a function body takes `//`, whatever column it sits in. A `.c` and a test program keep `//`,
because neither is a module interface: the declaration a caller reads is in the header, and a
program is the end of the chain and not a face another file reads. The coordinator ratified this
reading on 2026-09-13, over the narrower "a declaration another file calls", for two reasons: that
reading makes the mark of one declaration change when an unrelated file adds or removes a call, and
no lint can check it without a whole-program call graph, while a lint that reads one file can check
this one (T-108). The column is not the rule. T-108 first wrote "a declaration at indentation 8 or
less", and a statement inside a `static inline` function sits at indentation 4, so 37 lines in 12
blocks of `test/*.h` took `///` inside a body before a review counted them.

A comment that stands on a declaration or on a definition states its contract; only a comment
inside a body may be a bare citation. T-101 first collapsed 583 doc comments to the tag they
cited and had to put every one back: a tag says which rule the code follows and says nothing
about what the caller must hold true, what the callee gives back or who owns the memory. The
tags of such a comment stand on the line after its prose, between the prose and the declaration.

A section banner, `// ---- names ----`, is the third shape a comment takes below the header. It
is navigation, not a contract, and it keeps its text; the decisions the section implements stand
on the line after it. Stripping the tag out of 114 banners and putting it nowhere else left 10
files citing a decision they implement in no line at all, and left D10.3 cited nowhere in `src/`
(T-101).

Write nothing that restates the code. `// increment the index` above `i += 1;` is deleted, not
reworded. A comment that repeats the name of the function it stands on is deleted with it.

What that rule is worth, measured: T-101 read all 2261 distinct comments of `src/` and deleted 29
of them as restatements. That is 1 in 78, because after the citations became tags what was left
was contracts and knowledge. Comment lines are 19.8 % of `src/bootstrap` and 22.9 % of
`src/fort`, against 19.2 % and 22.2 % before the sweep: the share rose, because a citation on its
own line costs the line the tag used to share with prose. Of the 9867 comment lines the two hold,
1414 are header blocks, 310 are section banners, 2188 are citation lines of the shape of 1.4,
3694 are `///` contracts and 2261 are the `//` prose inside bodies. A ticket that wants a smaller
share has to shorten the contracts, which this section asks for, so the share is a consequence
and not a target.

T-108 swept the rest of what 1.1 governs, with the same lint and the same method, and deleted no
comment as a restatement. It rewrote 207 paragraphs by hand, where the grammar of the sentence
carried the tag rather than a parenthesis at its end. It deleted one comment for another reason:
`std/math.ft` said it was the only fort source that carries `///` doc comments, and the sweep made
that untrue. Comment lines are 34.7 % of `std` (917 of 2639), 13.6 % of `test/*.c` and `test/*.h`
(5343 of 39235) and 19.7 % of `test/fort` (8299 of 42159, the harness directives included),
against 31.3 %, 11.1 % and 17.4 % before. The command is the one above with the directory
changed. The share rose for the reason it rose in `src/`: a citation on its own line costs the
line the tag used to share with prose. The three corpora hold 1490 `///` lines after the sweep and
held 22 before, all 22 in `std/math.ft`
(`grep -h '^[[:space:]]*///' std/*.ft test/*.c test/*.h $(find test/fort -name '*.ft' | sort) |
wc -l`, run here and over `git archive main std test`). 31 of the 1490 stand in `std/math.ft`
(`grep -c '^[[:space:]]*///' std/math.ft`) and 1459 in files that held none. `math.ft` is not a
file the sweep left alone: its 22 lines re-wrapped to 24, and the marker then gave the mark to 7
more, above `i32 I32_MIN` and above `u64 F64_INF_BITS`. Of the 1490, 1466 took the mark from
`build/sweep/marker.py`, which prints that number, and 24 are the lines the sweep carried over
already marked.

**A sweep that guesses at grammar writes a true-looking sentence that says a false thing, and no
punctuation check sees it.** T-108 cut `of Dn.m` out of a noun phrase, and its rule fired on
`X of Dn.m and Y` as well, where the `and` joins two whole phrases and not two objects of the one
preposition. "the universe functions of D12.2 and the qualified names of D9.4" became "the
universe functions of the qualified names" in 15 files, and every one of them reads as grammar and
states a falsehood, so the lint of 1.4 reported 0 mangled lines over all 15. No rule tells that
shape from "the `assert` and `panic` of D12.2 and item 19", where the preposition is shared, so
the sweep now refuses the shape and reports it for the hand. The screen is a reader: list every
paragraph of the old revision that writes `of D<n>.<m> and `, print the new text beside it, and
read the 37 pairs (T-108).

### 1.4 Citations

A citation is a tag. Write `// D17.5`. Do not write a sentence.

Add one clause after a colon only where the tag alone leaves the rule unclear:
`// D17.5: an own lvalue moves`. The clause is 60 characters at most and it does not end in a
full stop. A citation of more than one decision is a list: `// D11.5, D11.7`. A ticket number is
a citation only with a clause, `// T-091: the runtime is a fort module`, because `.tickets/` is
outside the repository and the number alone points at a file the reader cannot open.

A citation stands on the line above the code it governs, or at the end of that line. A file that
implements a run of decisions writes the run as a range, which is one item of the list:
`// D3 to D8, D12, D14.2`. A range runs over sections, `D3 to D8`, or over decisions,
`D9.1 to D9.6`, and never mixes the two; both ends are decision tags, a ticket number opens none,
and the second end stands above the first, so `D9.5 to D9.1` and `D3.1 to D3.1` are not ranges.
Two citation lines stand together only when each carries its own clause and the two say different
things; a citation of more than one decision with nothing to say about each is one list. A
reference to a document is not a citation in this sense and takes no tag: it names the document
and its section, `toolchain.md 6 item 8`, under the rule of 4, "Moving a document" (T-105).
T-103 lints the shape below. It reads the comment from the `//` to the end of the line, and the
line is a citation when it matches `decisions` or `tickets`:

    tag       = D[0-9]+(\.[0-9]+)?
    section   = D[0-9]+
    point     = D[0-9]+\.[0-9]+
    ticket    = T-[0-9]{3}
    range     = <section> " to " <section> | <point> " to " <point>
    item      = <range> | <tag>
    clause    = .{0,59}[^.]
    decisions = //(/)? <item>(, <item>)*(: <clause>)?$
    tickets   = //(/)? (<item>|<ticket>)(, (<item>|<ticket>))*: <clause>$

The lint refuses one more shape, which the ticket that wrote this section had to learn twice: a
comment whose punctuation a sweep left dangling. Cutting a tag out of `(D4.6: text)` leaves
`(: text)`, out of `(item 8, D9.9)` leaves `(item 8,)`, out of `x.h and .c` leaves `and.c`, and
out of `(the note on D4.4)` leaves `(the note on)`. None of those holds a tag any more, so the
citation rule above cannot see them. T-101 made 11 of them in 9 files. The lint reads each
comment body with its code spans masked, and skips an indented example line and a code span
wrapped across two lines. It refuses a double space, a parenthesis that opens or closes on a
separator, an empty parenthesis or bracket, a one-word parenthesis that ends in a space, a
separator with nothing before it, a possessive whose owner is gone, and a connective left
hanging before `)`. The
review of T-101 fed the first version of it ten cuts the sweep had not made and nine passed,
the double space among them, which is why the list is this long: a check written from one
sweep's residue sees that residue and little else.

T-101's lint gives four wrong answers, which `src/` gave it no chance to show and T-108 met over
`test/`. It reads `U+D800` as a citation of a decision D800, because it lets a tag follow a `+`.
It reads a `//` inside a C string literal as a comment, because it counts the quotes of the line
instead of scanning it, and `test/*.c` holds fort programs as C strings. It accepts a bare
`// T-091`, because it carries the `decisions` production above and not the `tickets` one beside
it. And it lets a list repeat a tag that a range of the same list already names, so
`// D4.1 to D4.6, D3.14, D3.15, D4.4, D4.6` passed it. T-108 fixed all four in the copy it ran,
seeded 23 probes, one of every shape the lint refuses, and watched each one fire; 10 control lines
stayed clean. T-103 carries the four fixes into the lint it gives a home.

Measured on 2026-09-13 with that lint. `std/*.ft`, `test/*.c`, `test/*.h` and `test/fort/**/*.ft`
hold 2633 tagged comment lines and **0** outside the shape. `src/bootstrap` and `src/fort` hold
2564 and **5** outside it: three are citations of a ticket with no clause, which T-101's own lint
accepted, and two are prose written after T-101 landed. `src/lsp` holds 36 tagged lines and **36**
outside the shape, because T-101's lint read `src/bootstrap` and `src/fort` and no other directory
of `src/`. Neither is T-108's to fix: its deliverable names `std/` and `test/` (T-108).

The reader who wants the rule opens `spec/decisions.md` at the tag, where the entry's `rule` field
states it (T-100). That is what the log is for, and a copy of the rule in a comment is a copy that
drifts. The old rule asked for the copy: `src/` held 2962 tagged comment lines on 2026-09-12 and 0
of them matched the two expressions above. T-101 rewrote them; `src/` now holds 2562 tagged comment
lines and every one matches.

### 1.5 The rule that this section replaced

- **Citing decisions in code**: **superseded by `notes/style.md` 1, the comment policy (T-098).**
  The old rule asked for "a phrase stating the rule" (`// pointers print as 0x + lowercase hex,
  0x0 for null (D11.7)`) and refused a bare tag list; it produced 2962 tagged comment lines in
  `src/`, of which 0 matched the shape in force now. T-101 rewrote them. The rule in force: a
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

- **Text that no gate reads goes wrong and stays wrong.** `awk 'length > 100'` is the only
  automatic check a document gets, and the gate has no opinion about markdown at all. Two defects
  live under a green gate. *A number in prose goes stale.* Seven numbers written over two days
  were each wrong when someone counted them: `editors/README.md`'s "21 directories" (9 under one
  reading, 18 under another, found by T-111); `notes/compiler.md`'s "a same-size field swap is
  invisible to `opt`" (refuted by T-078's mutation `opt-18`); T-064's "220 named cases" (226, the
  grep missed every name holding a digit); T-063's "490 programs, 94 one way" (492 and 96);
  T-042's "six count constants" (seven, one sits inside an `enum`); `notes/testing.md`'s own
  command for counting moved constants (it undercounted, and T-045 found it by disagreeing with
  it); and the coordinator's `CORPUS_FILES = 604`, read off `main` while it believed the number
  came from a branch. So write the command that produced a number beside the number, and re-run
  the command rather than copy the number when the text moves. A number with no command is a
  claim, not a measurement. *A `|` inside a code span still splits a table cell.* Markdown splits
  a table row on `|` even inside backticks, so a row that quotes source holding `||`, `|=` or a
  bare `|` renders with extra columns. T-077 found 11 of its own 88 audit rows malformed this way,
  and they had been malformed since it generated the table. Escape every `|` inside a code span in
  a table cell as `\|`, and check by re-parsing: each row must split into the number of cells the
  header names. The deliverable of an audit ticket **is** its table -- T-077, T-078, T-115 and
  T-116 are all tables -- so a malformed table is a deliverable nobody can read (T-077, T-078,
  T-111).

## 5. Shell scripts

- **Shell scripts**: bash with `set -eu`, clean under shellcheck at its default severity; the
  host has no shellcheck, run it in the guest: `tools/vm run 'shellcheck tools/vm
  tools/provision.sh'`.

## 6. Commit messages

- **Commit messages**: a title of about 50 characters (72 at most), a blank line, then a body
  wrapped at 72 columns that says what changed and why, then a single `Co-Authored-By:` trailer.
  No `Claude-Session:` trailer, **and a session configuration that asks for one does not
  override this**, whatever it says about replacing earlier attribution guidance: a rule checked
  into the repository is the user's instruction to this project, and a session setting is not
  visible to anyone reading the history. The reason stands on its own: the session URL is useless
  to anyone reading the history later, and it is the only line in a commit that no reader can act
  on. Eight implementors have met this rule. Six stopped to ask -- T-037, T-089, T-092, T-105,
  T-042 and T-063 -- and each cost a round trip. T-077 and T-078 read the rule and applied it
  without asking, which is what this sentence is for. T-111 then shipped a commit that carried the
  trailer, and a reviewer caught it, not the gate. Nothing in the gate reads a commit message, so
  the sentence reaches only the implementor who reads it before committing.
