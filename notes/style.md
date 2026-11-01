# fort style: comments, code conventions and text

This document holds the conventions for the text the project writes: comments in C and in fort,
the naming and formatting rules of each language, markdown, and commit messages. It is not
normative about the language; `spec/decisions.md` and `spec/grammar.md` win over it (D1.2).

Section 1 holds the comment policy. T-099 moved the convention
bullets of the `## Technical Standards` section of `AGENTS.md` into the headings after it. The
test bullets of that section went to `notes/testing.md` and the JavaScript conventions of the
extension went to `editors/README.md`, as the routing table of `AGENTS.md` sends them.
Section 7 holds the ASD-STE100 rules. They moved from `AGENTS.md` on 2026-10-10.

## 1. Comments and documentation

### 1.1 Content

Document the module purpose. Add a module guarantee only when readers need it.
Do not list the declarations in a module comment.

Document an API only when its contract is not clear from its name and types. State non-obvious
behavior, preconditions, side effects, and failures. Do not document self-explanatory code.

Use comments inside a function only when the code does not make the logic clear. A comment can
explain an order, an invariant, a special case, or a required workaround. Delete a comment that
only restates the code.

Use ASD-STE100 as practical writing guidance. Write concise, direct, and natural sentences. Use
the exact technical term when it is the clearest term. Do not remove a necessary condition to
make a sentence shorter. Section 7 gives the rules; they apply to all text.

Code documentation explains the code. It does not record project history. Remove decision tags,
ticket tags, numbered specification items, and similar provenance. Keep useful references to code
symbols, source files, platform APIs, and technical standards.

Harness directives are not documentation. Keep their required syntax and position. Do not change
fixed-position fixtures during a documentation sweep. These fixtures include `test/lang/**/*.ft`,
`editors/vscode/test/fixtures/**`,
`test/highlight/scopes.ft`, and generated fixtures. A test pins a position in such a file:
commit 332e5fa7 removed one comment line from the allocation-failure fixture (then under
`test/darwin/`, now `run/errors/034`) and the Darwin gate exited 1 until T-183 repinned the test.

### 1.2 fort

Use `///` only for text intended for future published documentation. The project does not publish
documentation yet.

Use `///` for module documentation. Also use it for needed documentation on intended public
functions, types, constants, and fields. A technical export does not make a declaration an
intended public API.

Use `//` for repository-only details about a documented API. Put `///` text first and `//` text
below it. Use `//` for internal declarations, implementation invariants, function-body comments,
trailing comments, and section banners.

Do not state ownership or lifetime in fort API comments. The type system states this information.
An implementation comment can explain how the compiler applies an ownership rule.

Put harness directives before module documentation. Fort uses no block comments.

### 1.3 C

Use `//` for all C comments. This rule applies to module, API, internal, and function-body
comments. Put needed API documentation on the header declaration. Do not repeat it on the
definition.

State ownership or lifetime only when the C type cannot express it. Do not write a C block
comment.

### 1.4 JavaScript

Use JSDoc `/** ... */` for module documentation and needed public API documentation. Use `//` for
internal details and function-body comments. Use a JSDoc tag only when it adds contract
information.

### 1.5 Other languages

Use docstrings for Python module documentation and needed public API documentation. Use `#` for
internal details and function-body comments.

Use `#` for shell, CMake, and configuration comments. Put script documentation after the
shebang.

### 1.6 Agent prompts

State the compiler task first. Name the source files, compiler invariant, and expected diagnostics.
Name the required test cases, targets, commands, and evidence.

Describe changed FIR edges as altered compiler test inputs.
For a test that removes a compiler check, name the removed check and the expected assertion.
Use *source correspondence*, *control flow*, *allocation cleanup*, and *ownership analysis*
precisely.
Keep the required case counts and validation scope when you revise a prompt.
Keep the review depth.
Keep exact identifiers and diagnostic text.

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
  `_POSIX_C_SOURCE` does not set), and `bootstrap0/src/modules.c` declares it the same way;
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
  `tools/vm format` reformats. Do not write a C block comment (`/* */`), in the test helpers
  under `bootstrap0/test/` and `test/lang/ffi/` too; no tool rejects one since 2026-09-25. Two gaps
  of clang-tidy 18 are covered by review: `bugprone-unused-return-value` takes function names, not
  patterns (patterns arrive in clang-tidy 19), so `.clang-tidy` lists the C library and POSIX
  functions and the project's own functions are unchecked; and **clang-tidy 18's macro-argument
  blind spot covers `readability-identifier-naming` as well as `readability-magic-numbers`**, so
  nothing inside a `TEST(name, { ... })` body is checked for either -- a `static const char
  program[]` there draws no `invalid case style for static constant` while the same declaration at
  ordinary source location does. An experiment that renames an identifier *inside* a `TEST` body and
  sees no complaint has measured nothing; move the declaration out, or read the convention off a
  comparable one in a `main` (`SOURCE` in `bootstrap0/test/gen_control_test.c`).
  **A suite's `main` trips `readability-function-size` at 61 tests**, because `TEST_RUN` expands
  to thirteen statements and the check's default threshold is 800. The number measures the macro
  and the size of the suite, not the function, so the suite that passes it takes a
  `// NOLINTNEXTLINE(readability-function-size)` on its `main` with that reason written above it;
  `bootstrap0/test/check_stmt_test.c` is the first, at 63 tests and 825 statements (T-075). Do not
  raise the threshold in `.clang-tidy`: that check reads `src/` too, where the count means what it
  says. The bootstrap must also stay transliterable into fort: no unions, no macro tricks, and no
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
  `std/`, `src/fort/` and `src/lsp/`, nested directories included; nothing formats `.ft`, so
  indentation and spacing are still written by hand and read by review. Run it with
  `tools/vm lint`, or by hand as
  `tools/vm run 'python3 tools/fort_lint.py --fort build/Linux/debug/fort <file.ft>'`.

### 3.1 Names of modules and functions

The user approved these rules on 2026-10-07. The principle: a reader reads a name as
`<module>.<name>`, and the pair says each idea one time. Each rule gives one good (+) and one
bad (-) example from `src/fort` at `d9595ac4`. An example marked (invented) does not exist at
`d9595ac4`: `src/fort` has no directory yet, so a good example that names a directory gives the
form that a move takes. The flat modules keep their
names until a later change moves them; new code obeys the rules from its first commit.

Modules:

1. A directory groups one subsystem (`fir/`, `owner/`, `lsp/`). Do not give files a shared
   name prefix: use `owner/state.ft`, not `ownership_state.ft`.
   (+) `src/fort/fir/verify.ft` (invented). (-) `src/fort/fir_verify.ft` beside six more
   `fir_*.ft`.
   A directory `fir/` beside `fir.ft` makes `import fir.flow;` ambiguous when `fir.ft` declares
   `flow` (D9.3), so give no top-level declaration the name of a module in the directory.
2. A module name is one noun. Prefer a subject noun: what the module is about (`ast`, `token`,
   `state`, `region`). A module that runs one process over many subjects takes a doer noun
   (`lexer`, `parser`, `printer`, `verifier`). Use a gerund where the doer noun is not a usual
   word (`lowering`, not `lowerer`). Do not use verbs (`lower`, `check`) or vague words (`api`,
   `util`, `common`, `model`, `work`).
   (+) `lexer`, `types`. (-) `fir_lower` (a verb; use `fir/lowering`), `ownership_api`.
3. A leaf does not repeat its directory: `owner/state`, not `owner/owner_state`.
   (+) `fir/flow.ft` (invented). (-) `fir/fir_flow.ft` (invented), a move of `fir_flow.ft` that
   keeps the old file name.
4. One module has one job. A group of functions with a shared prefix becomes a module
   (`ownership_state.region_*` becomes `region.*`).
   (+) `place.field(f, p, k)` (invented). (-) `fir.place_local`, `fir.place_field` and nine more
   `fir.place_*`.
5. When two imports have one leaf, rename one as `<dir>_<leaf>` (`import fir.flow as fir_flow;`).
   `src/fort/json.ft` and `src/lsp/json.ft` both have the leaf `json`.
   (+) `import lsp.json as lsp_json;` (invented). (-) `import lsp.json as ljson;` (invented).

Functions:

6. A function name starts with a verb: `state.free`, `region.merge`, `lexer.next`.
   (+) `lexer.advance(lx)`. (-) `check.sized_reserve(ck)`.
7. A pure query names what it returns: `types.size(t)`, `region.count(r)`. A boolean query
   starts with `is_` or `has_`. Do not use `get_`.
   (+) `types.is_u64(t)`. (-) `fir_flow.bits_get(s, i)`, which returns `bool`.
8. Do not repeat a word of the module name. One exception: the functions of a doer or gerund
   module start with its verb (`parser.parse_place`, `lowering.lower_block`).
   (+) `fir_parse.parse_place`. (-) `check.check_sym_count`, `ast.ast_push`.
9. Do not name a thing that has its own module: `region.charge`, not `state.region_spend`.
   (+) `index.free(ix)`. (-) `ownership_state.region_spend`, beside the module
   `ownership_regions`.
10. Use one to three words. A short name is good when the module makes it exact: `state.free`.
    (+) `index.free`. (-) `parser.parse_module_compile_if_into`, six words.
11. Types and constants obey the same no-repeat rule: `rpc.conn`, not `rpc.rpc_conn`.
    (+) `ast.node`. (-) `gen.gen_val`, `fir_parse.fir_parser`.

Words (the glossary: one word has one meaning in all of the compiler):

12. `make` allocates and returns a value (not `create`; `new` is a keyword). `init` fills storage
    that the caller owns. `free` ends a value from `make` or `init` (not `destroy` or `deinit`).
    `drop` ends a value or an allocation that the caller moves in. `find` searches and can fail (not
    `lookup`, `locate` or `search`). `work` is the W amount (not `cost` or `price`); `charge` takes
    W (not `spend`). `release` is only the ownership term: the program gives up storage.
    (+) `index.free(ix)`. (-) `diag.create()`; use `diag.make()` (invented).
13. Abbreviations come from a closed list: `ast`, `fir`, `llvm`, `lsp`, `rpc`, `json`, `uri`.
    Write every other word in full (`float`, not `flt`).
    (+) `index.write_json(ix, j)`. (-) `flt.ft` and `flt.flt_parse`.

Enforcement:

14. `tools/fort_lint.py` makes three checks and cites this section in each message.
    - The prefix check reads each directory of `src/fort`, `src/lsp` and `std`. It flags each
      module whose first word 3 or more files of its directory share. A file named as the prefix,
      `rpc.ft`, does not count. `LEGACY_PREFIXES` exempts `fir_` and `ownership_` of `src/fort`
      until they move.
      (+) `src/fort/library/interface.ft` (invented). (-) `src/fort/library_interface.ft` beside
      `library_artifact.ft` and `library_cache.ft` (invented).
    - The function check flags a function whose first word is a word of its module path. The
      check splits each directory and leaf at `_`, so `owner/heap_state.ft` has the words
      `owner`, `heap` and `state`. The verb of a doer module is not its module word, so rule 8's
      exception passes.
      (+) `fir.parser.parse_place` (invented). (-) `owner.heap_state.heap_free` (invented).
    - The glossary check flags a declared name or a module path that holds a word that rule 12
      bans, and names the word to use.
      (+) `fir.flow.make` (invented). (-) `fir.flow.create` (invented).

    The function and glossary checks apply to a nested module only: a file in a subdirectory of
    `src/fort`, `src/lsp` or `std` (`std/<target>/` stands for `std/`). On `d9595ac4` the 90
    flat modules of `src/fort`, `src/lsp`, `std` and `std/linux` hold 187 functions whose first
    word is the module name and 55 function names with a word that the glossary bans. So the
    checks wait until those modules move. The lint does not check rules 2, 3, 5, 7, 9, 10, 11
    and 13; review checks them.

## 4. Markdown and specification text

- **Markdown**: Line-wrap at 100 characters, including tables and code blocks. Check with
  `awk 'length > 100 {print FILENAME": "FNR}' <files>`. Code fences use `fort`, `c`, `sh`,
  `llvm`, `json`, `ebnf` or `fir` as the language tag. `fir` is the textual form of
  `spec/fir.md` 13 (T-209).
- **Language changes**: any change to the language is recorded in `spec/decisions.md` first
  (new decision number or amended decision with a note), then in the specification document that
  owns the topic, then in the tests under `test/lang/`. Specification text never contains "TBD",
  "pending" or "not finalized"; deferred features live only in decision D15.
- **The shape of a decision entry** (T-100): `### Dn.m Title`, then `- owner:` (the specification
  document that carries the full prose), then `- rule:` (the rule itself), then the optional
  `- rationale:` (why the rule is this one) and `- history:` (one dated note for each amendment,
  oldest first, and for each reading of the rule that a ticket had to settle -- D4.4 carries such a
  note from T-041, which changes no word of the rule and records how an `f32` context reads it).
  An entry always has an owner and a rule. `agents/check_decisions.py` checks that
  shape when run by hand. An amendment rewrites the `rule` so
  that it states the rule in force, and appends a note to `history` that says what the rule said
  before: `Amended YYYY-MM-DD (T-nnn): <what changed>`. A note that changes no word of the rule
  carries `Note YYYY-MM-DD (T-nnn): <what the reading settles>` instead. Use that marker for a
  reading a ticket had to settle, as D4.4 does for T-041, and for a date put on a sentence that
  reports another entry, as D17.4 does for T-122. Do not invent a third word for either: the log
  holds these two and no other, which `grep -cE 'Note 20..-..-.. \(T-' spec/decisions.md` counted
  as 2 on 2026-09-13, against 80 for `Amended`. Never leave a stale sentence in the rule for the
  history to correct, and never delete the note that records the old text. **One rule a note of
  its own entry corrects** is the one property the `history` field decides, and the sweep
  for it reads the entries that carry that field: `grep -c '^- history:' spec/decisions.md`
  counted 49 of 144 on 2026-09-13, of which 48 hold the word "Amended" and D4.4 alone does not;
  the command that counts them is in T-109's Notes, Design. One of the 49 was stale, D19.5, which
  T-039 corrected in its note and T-109 rewrote. That sweep says nothing about the other 95
  entries, and nothing about a rule that goes stale against the tree or against another entry;
  those need their own reading, and reporting them as swept would say a thing was tested that was
  not. A note that reports another entry in the present tense goes stale the same way: two of the
  49 held one on 2026-09-13 (D19.5's and D20.5's, both about D19.5's rule). Append a dated note
  that says what has changed since, and leave the words of the old note alone, because the
  sentence above forbids deleting them (T-109). The `history` rule of `agents/knowledge_lint.py`
  checks this one when run by hand: it reports a sentence of a `history`
  field whose subject names a rule (`the rule`, `its rule`, `this decision`, `D19.5's rule`, or a
  bare tag at the head of the sentence, as in `D3.10 decides`) and whose verb is a present-tense
  verb of saying. Two things silence it, and only the second is a repair: a quotation of the old
  words, and the dated note this paragraph asks for, which the rule reads as a sentence carrying a
  date and one of `as it stood` or `from this date`. Writing the note in the past tense is not a
  third: `until then the rule read: this decision requires two runs` is still red, because the
  clause after the colon is a sentence of its own and it is present-tense. Quote the old words,
  and the rule reads them as the old rule. The rule states its blind spots in its own docstring,
  `agents/knowledge_lint_test.py` measures each of them and counts them, and no number for them
  stands here, because a number in prose rots and this paragraph is one file away from the check.
  The two widest gaps: a bare tag is a subject only at the head of a sentence, because inside one
  it is as often a relative clause that reports nothing (T-122 dated D17.4's `D3.10 decides
  function types` on 2026-09-13 and then widened the subject, and the gap that is left holds
  D9.1's `every test file D14.4 names NNN_name.ft` and a report one subordinator from the head),
  and one dated note excuses its whole field wherever the report stands in it (T-121).
- **Moving text inside the decision log**: the risk of an edit to `spec/decisions.md` is a changed
  meaning, which no build and no test can see. `agents/check_decisions.py --against <old file>`
  compares the words of every entry between two revisions of the log. It fails on an entry whose
  words changed, and on an entry whose words only moved, because a permutation keeps every word and
  can still invert a rule: "widening extends, narrowing truncates" and "narrowing extends, widening
  truncates" are one multiset and two rules. Run it over a copy of the file before the edit, and
  name each entry the ticket edits on purpose: `--allow Dn.m` for a changed word and
  `--allow-move Dn.m` for words that only move between the fields. The flags are the record of
  what the ticket did, and the tool prints the difference each one excused. **The tool compares two
  revisions and never a field against itself**, so it cannot see a `rule` that now states two
  rules: T-096 added a sentence to D18.1 saying that a compiler loads `std.rt_float` into a closure
  that holds a float, while the sentence three lines above it still said "as a root of every
  closure", and the run exited 0 because `--allow D18.1` excused the whole entry. Read the whole
  `rule` field after an edit, not the diff alone, and repair the sentence the new one contradicts
  in the same commit (T-096). The tool has no flag for a new entry: `--against` reports a new
  entry as "in one revision only" and exits 1, as for D17.20. `--allow` takes only a fielded
  entry, so `--allow D15` fails, because D15 is a section. Name the new entry in the Log instead.
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
  it); and the coordinator's `CORPUS_FILES = 604` (a constant removed on 2026-10-07), read off
  `main` while it believed the number came from a branch. So write the command that produced a
  number beside the number, and re-run the command rather than copy the number when the text moves.
  A number with no command is a
  claim, not a measurement. *A command written beside a number can count the comment that holds
  it.* T-120 wrote `grep -c 'm->method == ' src/lsp/server.ft` beside the number 9, in a header
  block of that same file, and the grep then read 10: the comment line carrying the command
  matched the pattern. Write the pattern so that the line holding it cannot match, and run the
  command after the edit rather than before it.
  `grep -cE '^ +if \(m->method == ' src/lsp/server.ft` reads 9, because a comment line opens with
  `//` and no `if` (T-120). *A `|` inside a code span still splits a table cell.* Markdown splits
  a table row on `|` even inside backticks, so a row that quotes source holding `||`, `|=` or a
  bare `|` renders with extra columns. T-077 found 11 of its own 88 audit rows malformed this way,
  and they had been malformed since it generated the table. Escape every `|` inside a code span in
  a table cell as `\|`, and check by re-parsing: each row must split into the number of cells the
  header names. The deliverable of an audit ticket **is** its table -- T-077, T-078, T-115 and
  T-116 are all tables -- so a malformed table is a deliverable nobody can read (T-077, T-078,
  T-111). *A number a tool can re-derive belongs to the tool and not to prose.* Six tickets found
  the three counts of the `--check-json` comment in `CMakeLists.txt` stale (T-042,
  T-043, T-124, T-126, T-128, T-157). T-133 deleted those counts. It named
  `run_tests.py --list` and `run_tests.py --check-json --list` as the way to read them. Five more
  places still set the corpus target at three lines for each source line, after the user dropped
  it to above 2.0 on 2026-09-14. Each now points at `TARGET_RATIO`, or states the words of D14.6
  (`spec/toolchain.md` 7.6), or names the command that measures one branch
  (`.claude/agents/rev-quick.md`). Three further places gave the number where they mean the
  ratio, and now name the ratio. One file keeps its stale numbers: `PROMPT.md` is the user's
  opening request and a record of one day. It holds three statements that are stale today: the
  3x coverage ratio, the x86-64 Linux target, and the assembly output. The user ruled on
  2026-09-17 that the file stays unchanged. A record is corrected by the documents that
  superseded it, and not by an edit (T-133).

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

## 7. Writing: ASD-STE100 everywhere

- **Write in ASD-STE100**, the ASD Simplified Technical English specification. Name it in
  full: the standard is ASD-STE100, not "ASD-STE". The user asked for this on 2026-09-11 and
  then said "use it everywhere", so it governs **all** the text this project writes: replies to
  the user, code comments, commit messages, ticket text, the prompts that go to agents, and the
  specification documents in `spec/`. There is no exempt category.
- The rules:
  - Write short sentences. Put one idea in each. Keep an instruction to 20 words and a
    description to 25.
  - Use the active voice. Say who does the action: "the agent refused the instruction", not
    "the instruction was refused".
  - Use the simple present where it works.
  - Give a word one meaning, and use the same word for that thing every time. A gate is a gate.
    Do not call it "the check" in the next sentence.
  - Use plain words: *use*, not *utilise*; *start*, not *initiate*; *before*, not *prior to*;
    *about*, not *approximately*.
  - Do not use idiom or metaphor. "The test went red" is jargon this project keeps, because it
    names one thing exactly; "bitten by", "load-bearing" and "throat-clearing" are not.
  - Cut the opening noise: "it is worth noting that", "as we can see", "interestingly".
  - Write a number, a command or a file path instead of an adjective. "The gate exited 1" says
    more than "the gate had a problem". "395 of 666 files" says more than "most files".
- **Exactness wins over simplicity.** A rule in `spec/` must stay exact. If a short sentence
  would change what a decision means, write two short sentences. Do not drop a condition to make
  a sentence shorter.
- The rule applies to text you write or change. It does not ask anyone to rewrite the documents
  that are already there; that sweep is its own ticket if the user wants it.
