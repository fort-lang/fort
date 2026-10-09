# fort toolchain

This document specifies the `fort` compiler's command line, build pipeline, build modes,
diagnostics, runtime, code generation contract and test conventions for v1. It implements D14
together with D9.10, D10, D11, D12, D18 and the run-time side of D17. Where it disagrees with
`decisions.md` or `grammar.md`, those files win (D1.2).

Sections: 1 Command line; 2 Build pipeline; 3 Build modes; 4 Diagnostics; 5 The runtime;
6 Code generation contract; 7 Testing; 8 Compiler architecture sketch; 9 Not in v1.

## 1. Command line

```sh
fort [options] entry.ft
```

Exactly one entry file is given; the first argument that does not start with `-` is the entry
file (D14.1). Options and the entry file may appear in any order.

| Option              | Meaning                                                    | Default    |
|---------------------|------------------------------------------------------------|------------|
| `-o <file>`         | output path                                                | see below  |
| `-S`                | stop after emitting the LLVM IR module (D19.1)             | off        |
| `-c`                | stop after compiling it to an object file                  | off        |
| `-I <dir>`          | add a search root after the entry directory; repeatable    | none       |
| `--std-dir <dir>`   | standard library directory                                 | see below  |
| `--release`         | release mode (section 3, D11.1)                            | checked    |
| `--no-bounds-check` | remove index and span checks (D10.6); unsafe               | checks on  |
| `-l<lib>`           | passed to the linker as given; repeatable, in order        | none       |
| `--cc <path>`       | the clang that compiles and links the IR (D14.3)           | see below  |
| `--target <triple>` | select the IR target; pass it to `--cc` (D14.1)              | see below  |
| `--cfg <list>`      | add compile-time `key=value` pairs; repeatable (D21.1)      | none       |
| `-Xcc <arg>`        | passed to `--cc` verbatim, after the arguments below       | none       |
| `--check`           | check source and selected ownership proof (D20.1)          | off        |
| `--ownership-check` | request complete ownership proof (D19.8)                  | off        |
| `--ownership-report <file>` | write coverage JSON; needs ownership selection    | none       |
| `--json`            | write the check document to stdout (D20.2), needs `--check`| off        |
| `--index`           | fill the document's identifier index (D20.3)               | off        |
| `--tokens`          | write the entry file's tokens to stdout and stop (D14.1)   | off        |
| `--ast`             | write the entry file's tree to stdout and stop (D14.1)     | off        |
| `--fir`             | write the program's FIR to stdout and stop (D14.1)         | off        |
| `--fir-after=<pass>` | write the FIR after `<pass>` and stop (D14.1)              | none       |
| `--fir-test`        | run the passes of a FIR test and write the FIR (D14.1)     | off        |
| `--fir-verify-report` | write the first FIR violation of each function; stop (D14.1) | off   |
| `--fir-stats`       | after the build, write `lowered N of M` (D14.1)            | off        |
| `--help`            | print the usage line and exit 0                            |            |
| `--version`         | print the compiler version and exit 0                      |            |

Ownership delivery uses one temporary --ownership-check option (D19.8).
It always requests complete proof in check mode and build mode. There is no stage selection option.
Run all integrated analyses before build-mode transformations. Missing producers remain incomplete.
Complete proof with zero violations exits 0. Violations or incomplete proof exit 1.
Usage, tool, and internal failures exit 2. Incomplete selected proof prevents code generation.
Reject ownership selection with --tokens, --ast, --fir, --fir-after, --fir-test, or
--fir-verify-report. Selection permits --fir-stats in build mode.
It never exempts an operation, an imported fort module, or an available runtime body.
Ordinary unselected builds retain their behavior.

--ownership-report takes the following argument and requires --ownership-check (D19.8).
The last report path wins. It names a separate file, not stdout or stderr.
The report records coverage under section 1.1. It changes neither text diagnostics nor JSON version
1.
Incomplete proof uses the existing error diagnostic format. Report-writing failure exits 2.
Reject a report path that names an input or a compiler output. Preserve those files.
Check mode can create the report's temporary file. It still creates no build temporary directory.

Permit source audits, measured source repairs, and scoped CI enforcement during delivery (D19.8).
The first audit gate validates inventory, report integrity, and tool execution.
It accepts exit 0 or 1 with valid fresh evidence. Violations and incompleteness remain
informational.
Scoped enforcement rejects violations, missing bodies, and incomplete proof within its declared
scope.
Results outside that scope remain visible. Scoped acceptance never changes the compiler exit status.
Keep existing build and fixpoint gates. Use source and FIR fixtures during feature development.
Keep the conservative range-call guard until complete selected proof establishes loan preservation.
Complete feature qualification and source migration before default enablement.
Then require analysis by default and remove --ownership-check.
Incremental reports do not claim current compiler completion.
The proof adds no runtime ownership checks. It preserves the representations and ABI in section 6.

- `-o`, `-I`, `--std-dir`, `--cc`, `--target`, `--cfg`, `--ownership-report` and `-Xcc`
  take the following argument;
  `-l<lib>` is one argument. `-I` roots are searched in command-line order (D9.2) and `-Xcc`
  arguments are passed in command-line order. The last `-o`, `--std-dir`, `--cc` and `--target` win.
- `--cc` must name a clang, since nothing else reads LLVM IR (D14.1, D19.1).
  The compiler defaults to `clang` on both targets (D14.3).
- The default target is the compiler binary's built target (D14.1).
  Linux x86-64 stores `x86_64-linux-gnu`. Mac arm64 stores the fixed `arm64-apple-macosx11.0.0`.
  The value is `std.os.TARGET` of the standard root that the compiler is built with
  (`std/linux/os.ft` or `std/darwin/os.ft`). The build passes no `--target` to a compiler that
  it builds.
  An IR mode may select either target form with `--target`.
  An unsupported form exits 2 before the compiler creates output.
- The selected target supplies the three configuration values of D21.1. `--target` therefore
  remains active under `--check` and `--fir`. `--tokens` and `--ast` do not evaluate
  configuration values.
- The default output is `a.out`; with `-c` it is `<entry>.o` and with `-S` `<entry>.ll` (D14.1),
  where `<entry>` is the entry file's base name without `.ft`, placed in the current directory as
  `cc` does.
- The default standard library directory is `$FORT_STD_DIR` when set, else `std` beside
  the running `fort` binary. Linux reads its binary path from `/proc/self/exe`.
  Mac reads it from `_NSGetExecutablePath`, then uses `realpath` when it succeeds.
  If `realpath` fails, Mac uses the path `_NSGetExecutablePath` returned.
- `-S` and `-c` together stop at the IR. With `-S`, `-l`, `--cc` and `-Xcc` are unused.
  `--target` remains active and selects the IR triple, ABI and target standard root.
  A target other than the built target requires an explicit `--std-dir` option with `-S`.
  This rule also applies when `-c` appears with `-S`.
  The caller must select sources that match that target. The compiler checks option presence.
  It does not inspect the sources for C ABI agreement.
- `-c` and linking accept only the built target. A different target exits 2 before output.
- `--release` and `--no-bounds-check` are independent and may be combined.
- `--check` runs steps 1 to 3 of section 2 and stops there: no IR, no `--cc`, no temporary, and
  the entry module need not define `main`, since it is a module under inspection and not a
  program (D20.1, D8.6). With it, `-o`, `-S`, `-c`, `-l`, `--cc` and `-Xcc` are unused.
  `--target` and `--cfg` supply configuration values. `--json` replaces the text diagnostics with
  the document of section 4.1 on stdout. It is a usage error without `--check`, since a build spawns
  a `--cc` that inherits
  stdout and could not promise a complete document or nothing (D20.2). `--index` fills the
  document's `"symbols"` array with the identifier index of section 9.1 and implies `--check` and
  `--json`, so `fort --index main.ft` is the whole of what an editor runs (D20.3).
- `--tokens` runs the lexer over the entry file and stops there: it resolves no import, parses
  nothing and needs no standard library, so it is the one thing a compiler with a lexer and no
  parser can do (D14.1). It writes one line per token to stdout, ending with the `end of file`
  token, and is a usage error together with `--check`, `--json` or `--index`, which all need a
  front end; every other option is unused. A lexical error is reported on stderr in the form of
  section 4 and lexing resumes at the next line (D14.2), so the dump covers the whole file either
  way and the status is then 1. A line is

  ```sh
  <line>:<col>-<end_line>:<end_col> <value> "<spelling>" <kind>
  ```

  where the range is the token's (D20.4), 1-based with a tab counting as one column and its end
  exclusive; `<value>` is the magnitude of an integer literal (D2.5) or the byte of a char literal
  (D2.7) and 0 for every other kind; `<spelling>` is the token's source bytes, and for a string
  literal its decoded bytes (D2.9), with `"`, `\`, a newline, a tab, a carriage return and every
  byte outside printable ASCII written as `\"`, `\\`, `\n`, `\t`, `\r` and `\xHH` with
  uppercase hex digits, so that one token is one line; and `<kind>` is the token kind as a
  diagnostic names it -- the word for a keyword, the glyph for an operator, and one of
  `identifier`, `integer literal`, `float literal`, `char literal`, `string literal`, `$cfg`,
  `$if` and `end of file` -- which stands last because it is the only field that may hold a space.
  So
  `fort --tokens` on a file holding `x = 0x10;` writes

  ```sh
  1:1-1:2 0 "x" identifier
  1:3-1:4 0 "=" =
  1:5-1:9 16 "0x10" integer literal
  1:9-1:10 0 ";" ;
  2:1-2:1 0 "" end of file
  ```

  Compiler unit tests hold this format.
- `--ast` runs the lexer and the parser over the entry file and stops there: it resolves no
  import, checks nothing and needs no standard library, so it is what a compiler with a parser
  and no checker can do (D14.1). It writes the entry file's syntax tree to stdout as one
  S-expression followed by one newline, and is a usage error together with `--tokens`, which
  writes a dump of its own, or with `--check`, `--json` or `--index`, which all need a front end;
  every other option is unused, as under `--tokens`. A lexical error is reported and lexing
  resumes at the next line, and a syntax error is reported and the parser skips to the next
  boundary (D14.2), so a tree covering the whole file is written either way and the status is
  then 1. The form is `(kind field... child...)`, `nil` for an absent fixed child, so that the
  position of every child is visible. A kind is one of

  ```sh
  module import path item fn extern-fn param struct field-decl enum member var compile-if
  type prim string void noreturn name fn-type ptr span array
  block assign incdec call-stmt if while do for range-for switch case defer return
  break continue init designator
  int float char str bool null cfg ident unary binary ternary call index span field arrow
  cast sizeof new struct-lit array-lit error
  ```

  one per production of `grammar.md` in its order, with three spellings that are not
  productions: an `extern fn` (D9.8) prints as `extern-fn` rather than `fn`, a `default` clause
  (D7.6) as `(case default ...)`, and a type suffix prints as the suffix it is, `ptr`, `span` or
  `array`. Four things the form adds to a bare tree: a function's parameters and
  a clause's labels stand in a group of their own, `(params ...)` and `(labels ...)`, so that
  they are not read as the children after them; a type position prints its `own` and `mut` as
  words after what they qualify (D5.3, D17.2), and a type suffix prints as `(ptr ...)`,
  `(span ...)` or `(array <length> ...)` with its own markers; an operator prints as the token
  kind a diagnostic names it by (`+`, `<<=`, `++`); and a region a syntax error made the parser
  skip prints as `(error)`, with no children (D14.2). The decoded bytes of a string literal and
  the text of a float literal print between double quotes with `"` and `\` escaped and **every**
  byte outside printable ASCII written as `\xHH` with uppercase hex digits -- `\x0A` for a
  newline and `\x09` for a tab, where the token dump above writes `\n` and `\t` -- so that a
  tree is one line. So `fort --ast` on a file holding `fn f() void { i32 mut x = 1; }` writes

  ```sh
  (module (fn (type (void)) f (params) (block (var x (type (prim i32) mut) (int 1)))))
  ```

  Compiler unit tests hold this format.
- `--fir` runs steps 0 to 3 of section 2 over the closure of a program and stops there. It
  writes the FIR module of the program to stdout in the textual form of `spec/fir.md` 13 and
  exits 0 (D14.1). It writes no IR and no file and runs no `--cc`, so `-o`, `-S`, `-c`, `-l`,
  `--cc` and `-Xcc` are unused. It takes the target rule of `--check`: any target form that the
  compiler accepts, and no link restriction. The entry module must define `main`, as in a build.
  A compile error prints its diagnostics, writes no FIR and exits 1. `--fir` is a usage error
  together with `--tokens`, `--ast`, `--check`, `--json` or `--index`, which write another text
  or stop at another pass. `--fir-after=<pass>` is one argument. It implies `--fir` and writes
  the FIR at a named point of `spec/fir.md` 11: `lower`, the output of the lowering, or
  `build-mode`, the output of the build-mode pass. Any other name, the empty name and `verify`
  included, is a usage error. The last `--fir-after` wins. `build-mode` runs the pass for the
  mode that `--release` and `--no-bounds-check` select, and the verifier after it. `--fir` and
  `--fir-after=lower` read neither flag. The module holds each function with a body of the
  closure: the modules in the order of D9.10, and the functions of each in source order. A blank
  line stands between two functions. The verifier tests each function before the print, and a
  violation ends the compiler with a panic (`spec/fir.md` 3). The lowering does not lower every
  construct yet.
  For a function that holds such a construct, `--fir` writes the line
  `// not supported: <kind> at <line>:<col>` in place of the function (`spec/fir.md` 9.8). `<kind>`
  is the name of the tree node, as `--ast` writes it, and the location is the node's location in
  the tree (`spec/fir.md` 9.8): the operator of a binary, unary or field node, the `(` of a call.
- `--fir-test` reads the entry file as a FIR test of `spec/fir.md` 16.3, whose header
  `test/fir/README.md` describes. It checks the lines of the `//! prelude:` block as the module
  `main` through steps 0 to 3 of section 2, with no entry rule, so the prelude need not define
  `main`. A diagnostic of the prelude names the line and the column of the test file. The
  compiler then parses the FIR text of the file against that module, runs the passes that the
  `//! pass:` directives name, in their order, writes the module to stdout in the textual form of
  `spec/fir.md` 13 and exits 0 (D14.1). A diagnostic of the prelude or of the FIR text writes no
  FIR and exits 1. The pass `none` runs nothing. The pass `verify` runs the verifier of
  `spec/fir.md` 10 on each function, and the first violation ends the compiler with a panic.
  The pass `ownership-local` runs the local ownership analysis of `--ownership-check` on each
  function, in text order (D17.14, D17.18). It runs no other analysis, so it gives no closure
  proof. It first tests each function with the verifier in report mode. A broken rule writes
  `fort: error:` and the violation, with no panic, and exits 2, as a verification failure of
  `--ownership-check` does (1.1). Each validated violation is an error and a note at its
  location. A function with no violation and an incomplete local proof gets one
  `ownership proof is incomplete` error at its first incomplete reason (4.2). After an error
  the pass writes no FIR and exits 1. A complete local proof proves one function in the local
  scope of 1.1. The pass `ownership-local` must come before every pass `build-mode`, because
  the proof reads the FIR before that pass (D19.8); after one it exits 2.
  The pass `build-mode` runs the build-mode pass of `spec/fir.md` 11 on each function, and does
  not run the verifier. Its arguments `--release` and `--no-bounds-check` select the mode, and
  with no argument it runs the default mode. The compiler reads every directive before it runs
  a pass. An unknown pass, an argument of `none`, `verify` or `ownership-local`, and an
  argument of `build-mode` that is no mode exit 2. `--fir-test` takes the target rule of
  `--check` and is a usage error together with `--tokens`, `--ast`, `--check`, `--json`,
  `--index`, `--fir` or `--fir-after`.
- `--fir-verify-report` implies `--fir` and takes its target rule. It lowers each function of
  the closure as `--fir` does and runs the verifier of `spec/fir.md` 10 in report mode, first on
  the output of the lowering and then, when the function keeps every rule, on the output of the
  build-mode pass for the mode that `--release` and `--no-bounds-check` select. The verifier
  stops at the first violation of a function, so for each function that breaks a rule it writes
  one line to stdout, the first violation, `<file>:<line>:<col>: fir.verify: V<n> <what>:
  <function> bb<k> statement <i> (s<N>)`, where the location is the name of the function. It
  exits 0 whatever it found. A function that the lowering does not support writes nothing.
  `--fir-after` does not change the report (`spec/fir.md` 16.5).
- A build writes each function through FIR: the lowering, the verifier, the build-mode pass and
  the translator (`spec/fir.md` 3). Each of two cases is a compile error with exit 1 and no
  module (`spec/fir.md` 9.8): a function that the lowering does not support, and a function that
  needs a print function, `str_eq` or the entry of a check kind of `std.rt` that the closure
  lacks or declares in another form. For a print function or `str_eq`, another form is another
  signature (`spec/fir.md` 9.8). For the entry of a check kind, another form is other parameters
  than rule V7 requires. A check that the selected mode removes needs no entry.
  The compiler assumes the other `std.rt` functions that it calls (`alloc`, `free`, `args_init`,
  `args`, `shutdown`) and does not test them.
  `--fir-stats` then writes one line to stdout after the module is written, `lowered N of M`: N
  functions that the translator wrote of M definitions of fort functions in the closure, the
  compiler-emitted `main` not counted. A refusal is a compile error, so in a line that a build
  writes N equals M. A compile error writes
  no line. It combines with `-S`, `-c`, `--release` and `--no-bounds-check`, and it is a usage
  error together with `--tokens`, `--ast`, `--check`, `--index`, `--fir`, `--fir-after`,
  `--fir-verify-report` or `--fir-test`, which emit no module.
- In the fort compiler's `--ast` form, a C extern with `...` prints a bare `...` last in its
  `(params ...)` group. The mark is not a `(param ...)` child.
  A fixed extern prints no mark, and a fort definition cannot print this mark (D8.3, D9.8).
  Thus `extern fn c(i32 x, ...) void;` prints

  ```sh
  (module (extern-fn (type (void)) c (params (param (type (prim i32)) x) ...) nil))
  ```

  Bootstrap-0 need not parse or print this new C extern form.
- The entry file's directory is always a root and the current directory never is (D9.2).

Exit status (D14.1):

| Status | Meaning                                                                          |
|--------|----------------------------------------------------------------------------------|
| 0      | success                                                                          |
| 1      | at least one compile error was reported (section 4)                              |
| 2      | usage error, unreadable entry file, internal error, or failure of `--cc`         |

Usage errors, internal errors and failures of `--cc` are reported as `fort: error: <message>`
on stderr, for example `fort: error: cannot read 'x.ft': No such file or directory` or
`fort: error: cc failed with status 1`. `fort` with no arguments prints one usage line and exits
with 2; `--help` prints that line and then the table above, and exits 0.

These are all the `fort: error: <message>` texts. Each exits with status 2 (D14.1).
Twenty report a command line the compiler cannot use and then print the usage line:
`missing argument for option '<opt>'`, `unexpected argument '<arg>'` (a second entry file),
`unknown option '<opt>'`, `no entry file`, `--json requires --check` (D20.2),
`--tokens does not combine with --check, --json or --index`,
`--ast does not combine with --tokens, --check, --json or --index`,
`--fir does not combine with --tokens, --ast, --check, --json or --index`,
`unknown --fir-after pass '<name>'`,
`--fir-test does not combine with --tokens, --ast, --check, --json, --index or --fir`,
`--fir-stats does not combine with --tokens, --ast, --check, --index, --fir or --fir-test`,
`invalid --cfg assignment '<entry>'`, `invalid --cfg key '<key>'`, `empty --cfg list entry`,
`duplicate --cfg key '<key>'`, and `cannot override compiler configuration key '<key>'`,
`unsupported target '<triple>'`, `--std-dir is required for cross-target -S`,
`cannot compile object for target '<triple>' with a '<built target>' compiler`, and
`cannot link target '<triple>' with a '<built target>' compiler`.
The last four target errors stop before output. `<triple>` shows the selected target.
`<built target>` shows the binary's built target.
Four report an operation of section 2 that
failed, with the system's error text as `<reason>`: `cannot read '<file>': <reason>` (the entry
file), `cannot write '<file>': <reason>` (the LLVM IR module), `cannot create a temporary directory
in '<dir>': <reason>` (`mkdtemp` under `$TMPDIR`) and `cannot run '<cc>': <reason>` (`--cc` could
not be started). Two report the outcome of `--cc`: `cc failed with status <n>` and `cc failed with
signal <n>`. Four report a FIR test that `--fir-test` cannot run: `the .fir test has no '//!
pass:' directive`, `unknown pass '<name>'`, `the pass '<name>' takes no argument` and `unknown
build mode '<argument>'`. The last two are the compiler's own failures: `internal error: <what>`
and `out of memory`.

```sh
fort main.ft -o main                          # build ./main in checked mode
fort -S main.ft                               # write main.ll and stop
fort -c main.ft                               # write main.o and stop
clang --target=x86_64-linux-gnu -o main main.o          # link the -c object by hand
fort --release -o main main.ft                # release mode
fort --release --no-bounds-check -o bench main.ft
fort -I lib -I vendor -lm main.ft             # extra roots, link libm
fort --cc clang-18 --target x86_64-linux-gnu -Xcc -fuse-ld=lld main.ft
fort -S --target arm64-apple-macosx26.6.2 --std-dir mac-std main.ft
fort --check lib/util.ft                      # check that module and its imports, print nothing
fort --check --json main.ft                   # one JSON document on stdout, for an editor
fort --index main.ft                          # the same document with the identifier index
fort --tokens main.ft                         # one line per token of that file, nothing else
fort --fir main.ft                            # the FIR of the program, nothing else
FORT_STD_DIR=/opt/fort/std fort main.ft
```

The only environment variables read are `FORT_STD_DIR` (D14.1) and `TMPDIR`, which locates the
temporary directory for the intermediate IR file (D19.1).

### 1.1 Ownership coverage reports (D19.8)

The compiler report is a JSON version 5 document. The audit attestation is a JSON version 1
document.
Neither document changes the diagnostic JSON of section 4.1.
The compiler writes one report for one checked closure. The runner attests the invocation and bytes.
The compiler needs no git executable, revision option, or cryptographic hash implementation.
These versions require the members and types below. Reject duplicate JSON members and unknown
versions.
All count and key integers are nonnegative. Source lines and columns start at 1.
Integers exclude Boolean values and fit u64. Context-local function keys fit u32.
Count overflow gives report failure, not wrapped totals.

**Compiler report.** The top-level object has these members:

| Member | Type and meaning |
|---|---|
| `kind` | String `fort-ownership-report`. |
| `version` | Integer 5. Version 2 adds `limits.w_scale` and `fir_size`; 3 local; 4 raw; 5 stored. |
| `complete` | Boolean true; the report is complete, not necessarily its proof. |
| `compiler_version` | The string that --version identifies. |
| `invocation` | Entry, working directory, arguments, target, configuration, and mode. |
| `files` | Array of the loaded source files. |
| `enumeration` | Selected body count, inactive declaration count, and enumeration status. |
| `analyses` | Availability and closure outcome for each named analysis. |
| `limits` | The versioned numeric production limit table. |
| `meters` | Actual counted ledgers and their separate scopes. |
| `bodies` | One row for each discovered selected fort body. |
| `totals` | Counts derived from the body rows. |
| `first_incomplete` | The first incomplete reason, or null. |
| `failure` | The first front-end, lowering, verification, analysis, or tool failure, or null. |
| `verdict` | String `accepted`, `rejected`, or `failed`. |
| `exit_status` | Integer 0, 1, or 2; the compiler's final status. |

`invocation` contains `entry`, `cwd`, `argv`, `target`, `configuration`, and `mode`.
Entry, working directory, and target are strings. Arguments form a string array without argv[0].
Configuration is an array of objects with string `key` and `value`, sorted by key.
It contains effective values, including target-supplied values and last-option overrides.
Mode contains Boolean `check`, `release`, and `no_bounds_check` members.
The entry and each file path use the loader's filename. The runner normalizes paths for comparison.

Each file row contains integer `id`, string `path`, and string `module`.
File IDs start at 0 and increase without gaps in loaded-file order.
`enumeration` contains Boolean `complete`, integer `selected_bodies`, and integer
`inactive_declarations`. Count functions with bodies in inactive configuration branches separately.
Count neither extern declarations nor inactive bodies as selected bodies.
Lexical counts never establish a complete checked-body denominator.
Front-end failure can prevent complete enumeration. Retain discovered rows and set its status false.

Use these seven analysis names, in this order:
`graph`, `liveness`, `local`, `stored_borrows`, `raw`, `calls_heap`, and `process_exit`.
Each `analyses` row contains `name`, `producer`, and `status`.
Producer is `integrated` or `unavailable`. Status is `complete`, `incomplete`, `failed`, or
`unexecuted`. An unavailable producer has an incomplete closure outcome, even with no selected body.
Closure outcomes include required non-body facts, such as globals and generated startup.
The report runs integrated analyses. It never substitutes supplied test facts or permissive
services.
`limits` contains integer `version`, `d`, `r`, `p`, `g`, `h`, `t`, `e`, `w`, `w_scale`, and `v`.
Use the actual production table. Do not copy numeric values into this specification (D17.18).
`w` and `w_scale` are W_base and W_scale of the FIR-size W function (`fir.md` 14.1).

Each meter row contains integer `id`, string `name`, `owner`, integer `fir_size`, `counts`, and
`first_refusal`.
Owner is null for a closure computation, or the context-local function key of its computation.
`fir_size` is the FIR size of that computation.
Counts form an array with string `category` and `scope`, plus integer `used` and `bound`.
A W count has the bound `w + w_scale * fir_size`, saturated at the u64 maximum.
Each other count has the bound of its table member.
Category uses D, R, P, G, H, T, E, W, or V. Scope uses ownership_api.limit_scope names.
Report only categories the actual ledger measures. Absence does not mean zero use.
First refusal uses the reason object below, or null. Meter IDs start at 0 without gaps.
The first milestone names the existing graph ledger `graph_private` and service ledgers `services`.
The local increment adds one `local` ledger for each verified body: its local flow computation.
A `local` ledger has an owner. Its FIR size equals the FIR size of that body's service ledger.
Local correspondence can need a second local flow run, the classification run.
It runs when the first run has a failure, no validated violation and no refusal.
It drops the outside effects of calls and non-local releases.
When it ends within its budget and fails nothing, each failure depends on those effects.
Then the body leaves the local scope. Any other end keeps the body in that scope.
Its first pass keeps one joined path state for each block, as if G were 1.
Only a first pass that fails a step needs the second pass, with the production G.
The classification run is a computation of its own, with a `local_classification` ledger.
That ledger has the owner and the FIR size of the body's `local` ledger.
A refusal there explains no failure: the body stays in the local scope with incomplete proof.
The ledger keeps that refusal, and the compiler emits its budget error.
The stored-borrow increment adds one `stored_borrows` ledger for each verified body.
It is the stored run: the local steps, and the steps of struct and array leaves, stack
sources and range loans.
Its FIR size equals the FIR size of that body's service ledger.
Its classification run has a `stored_borrows_classification` ledger with the same owner and
FIR size. The rules of the local classification run apply to it.
A stored violation that the local run validates at the same operation counts once, in the
`local` row.
The raw increment adds one `raw` ledger for each verified body: its raw computation.
A `raw` ledger has an owner. Its FIR size equals the FIR size of that body's service ledger.
The graph FIR size is the sum of the service ledgers only.
Graph construction uses the graph's retained private W ledger.
Service dispatch, liveness, and the target queries of one body charge that body's service ledger.
Report these scopes separately.
Never sum separate ledgers as one shared meter. Never reset or split a computation to hide refusal.
Required accounting that no ledger covers remains incomplete. Graph completion supplies no shared
graph/liveness accounting claim.

Each body row contains `source`, `key`, `checking`, `lowering`, `verification`, `analyses`,
`ownership`, `violations`, and `first_incomplete`.
`source` contains integer `file`, `line`, `col`, `end_line`, `end_col`, and `instance`, plus string
`module` and `name`. Its file ID refers to `files`. Its range identifies the declaration name.
Ranges use section 4.1. Instance is 0 for ordinary v1 function definitions.
`key` is null before canonical key construction, or an object with integer `module`, `declaration`,
and `instance` from ownership_api.function_id.
Assign module keys in checked dependency order and declaration keys in selected source order.
Number keys from 0. Keys remain local to this checked closure.
The normalized path, name range, name, and instance define source identity across contexts.
The module name remains context metadata. Numeric keys and pointers do not define source identity.

Checking, lowering, and verification each use `complete`, `failed`, or `unexecuted`.
Each body's analysis array contains the seven named rows in the same order.
A row contains `name`, `correspondence`, `solver`, `proof`, and integer `violations`.
Correspondence and solver use `complete`, `incomplete`, `failed`, or `unexecuted`.
Proof and body ownership use `complete`, `violated`, `incomplete`, `failed`, or `unexecuted`.
Complete correspondence means the stage has all required facts for that body's declared obligations.
A solver can finish with incomplete correspondence. Its proof remains incomplete unless independent
facts prove all affected obligations (D17.18).
Only validated errors increment violations. Abstract possibilities remain incomplete proof.
Body violations sum its analysis violation counts. Do not count the same diagnostic twice.
After successful prerequisites, unavailable analysis rows have unexecuted correspondence and solver,
and incomplete proof.
Do not run a dependent stage when checking, lowering, or verification fails.
Retain its row with unexecuted correspondence, solver, and proof.
Missing rows never mean successful analysis.
Derive body ownership from proof rows with this precedence: failed, violated, incomplete,
unexecuted, complete. A failed prerequisite makes body ownership failed.
Retain incompleteness separately when a validated violation also exists.

`totals` contains integer `bodies` and `violations`, plus checking, lowering, verification, and
ownership counter objects. Each counter names all statuses allowed for that field, including zeros.
It also contains an `analyses` array with the seven names and correspondence, solver, and proof
counter objects, plus integer `violations`.
Each status counter object's counts sum to the selected body count.
The body count equals the row count and `enumeration.selected_bodies`.
These totals count context-body occurrences. They do not combine proofs from overlapping closures.

Each reason object contains `stage`, `code`, `source`, and `limit`.
Stage is `enumeration`, `checking`, `lowering`, `verification`, `tool`, or an analysis name.
Code is a string. Incomplete reasons use ownership_api.incomplete_reason names or
`missing_producer`.
Failure codes use `source_error`, `unsupported_lowering`, `verification_failure`,
`analysis_failure`,
or `tool_failure`. Source is null without a location, or has `file`, `line`, `col`, `end_line`,
and `end_col`. These members are integers.
Limit is null, or an object with string `category` and integer `used` and `bound`.
Category uses D, R, P, G, H, T, E, W, or V. Keep the first refused charge and its location.
A W limit has the W bound of the meter that refused the charge.
Use analysis order, then canonical function and source order, to select a report's first reason.
Keep each affected body's first incomplete reason even when report diagnostics reach their limit.
Stream coverage rows or retain bounded summary records. The report requires no expanded proof trace.
Failure to retain required rows or write them gives exit 2. Never publish truncated coverage.

Accepted means exit 0, complete enumeration, complete closure outcomes for all seven required
analyses, complete body proofs, and zero violations.
Unavailable producers prevent exit 0, including missing non-body obligations in an empty closure.
An empty body array alone establishes no complete ownership acceptance.
Rejected means exit 1. Failed means exit 2.
Source errors and unsupported lowering exit 1. Verifier violations and internal service failures
exit 2.
Keep available failed and unexecuted rows in either case. A crash may produce no valid report.
Solver completion, graph completion, and a complete JSON document do not establish ownership safety.

Write to a new temporary file beside the report path. Complete the document, close it, then rename
it
atomically over that path. Publish only a complete document with `complete: true`.
On report failure, remove the temporary file and exit 2. An older destination can remain unchanged.
The runner therefore uses a fresh report path that does not exist before the invocation.
After a successful ownership proof, a later build-tool failure updates the final report outcome.
An error before report initialization can leave no report. CI treats that case as a failed attempt.

**Audit attestation.** The runner writes a `fort-ownership-audit` version 1 object.
Its members are `kind`, `version`, `complete`, `source_revision`, `compiler_revision`,
`compiler_sha256`, `target`, `configuration`, `inventory`, `attempts`, and `totals`.
Revisions are full git object names from source and compiler build provenance. They need not match.
The compiler digest and all other SHA-256 values are 64 lowercase hexadecimal characters.
The runner verifies the compiler bytes and source bytes before and after each invocation.
It rejects changed bytes, missing provenance, mismatched revisions, and missing or invalid reports.
Capture candidate .ft input hashes before invocation under the entry, search, and standard roots.
Reject a reported file that the captured input manifest lacks.

Target is a string. Configuration uses the compiler report's key/value array.
Inventory is a sorted string array of tracked compiler paths from `git ls-files -- src/fort`.
Retain paths ending in .ft. Run main.ft first, then the remaining roots in inventory order.
An attempt row contains `root`, `cwd`, `argv`, `exit_status`, `report_path`, `report_sha256`, and
`inputs`. Inputs contain normalized `path` and `sha256` for each reported closure file.
Root, working directory, and report path are strings. Arguments form a string array, including
argv[0].
Exit status is an integer. Report digest is null when no report exists. Inputs form an object array.
Each input path and digest is a string. Preserve failed attempt evidence even when validation fails.
Normalize compiler source paths relative to the audited checkout. Normalize standard files relative
to the selected standard root, with a `std/` prefix. Keep other files as absolute paths.
The context identity is the root, target, effective configuration, search roots, and standard root.
Capture search and standard roots from the invocation. Do not merge local numeric keys across
contexts. Use normalized source identities to deduplicate file and declaration counts only.

Attestation totals contain `root_attempts`, `unique_files`, `unique_source_bodies`, and
`context_bodies`. Count the first two from attempts and input paths. Count the latter two from body
identities and body rows. Each inventory root has one attempted invocation for this target.
Each total is an integer. Retain unsuccessful attempts. Exit 2 or invalid evidence fails the audit.
Use atomic publication for the attestation too. Its complete marker describes complete evidence.
It grants no ownership acceptance.

The first CI validator accepts exit 0 or 1 only with fresh valid reports and a valid attestation.
It checks names, versions, types, identities, status combinations, row totals, and invocation bytes.
It rejects missing roots, duplicate identities within a context, stale paths, and truncated
evidence.
It also rejects incomplete selected-body enumeration. Discovered rows prove no complete denominator.
Preserve that failed root and report. Unsupported lowering can remain valid exit-1 audit evidence
when its selected-body denominator is complete.
Exit 2 or a missing valid report fails the gate. No coverage-regression baseline applies initially.
The local scope declares a body when that body's `local` correspondence is complete.
Scoped local enforcement rejects each local violation in any body.
It also rejects each declared body whose `local` proof is not complete.
The stored scope declares a body when its `stored_borrows` correspondence is complete.
Scoped stored enforcement rejects each `stored_borrows` violation in any body.
It also rejects each declared body whose `stored_borrows` proof is not complete.
The raw scope declares a body when that body's `raw` correspondence is complete.
Scoped raw enforcement rejects each raw violation in any body.
It also rejects each declared body whose `raw` proof is not complete.
Later scoped enforcement reads the same reports and requires complete proof within its declared
scope.
It never converts exit 1 into accepted complete compiler proof.

## 2. Build pipeline

Compilation is whole-program (D9.10):

0. In an IR mode or under `--check` or `--fir`, select the built target or the explicit
   `--target` (D14.1). Reject an unsupported target form with status 2 before step 1.
   Under `--check` or `--fir`, the selected target supplies the configuration values of D21.1.
   A non-built `-S` target requires an explicit `--std-dir` before step 4.
   A non-built `-c` or link target exits 2 before step 4 unless `-S` also appears.
1. Read the entry file and derive its module path and root (exit 2 if unreadable, 1 if the base
   name contains a `.` or a `:`, the two characters a module path is spelled with, which would
   let it collide with that module's symbols). The base name need not otherwise be an identifier:
   the entry file is named on the command line rather than reached by an import path, so
   `007_case.ft` is the module `007_case` and nothing can import it, and `my-app.ft` is legal too
   (D9.1 as amended).
2. Parse each root and each newly reached module. Select its import `$if` branches before any
   inactive path probe, source read, module identity, binding, duplicate-binding, or cycle
   operation. Resolve only its selected imports. Select its declaration `$if` branches before name
   collection. Collect its selected declarations (module-system.md 2 and 3). Continue until the
   closure is complete. Reject cycles and duplicate identities (exit 1). `std.rt` is a root of the
   closure beside the entry file. The compiler loads it whether or not anything imports it (D9.10,
   D21.2, D21.3, section 5).
3. Check every module in dependency order, imported modules first (exit 1).
   When ownership analysis is selected, lower and verify the available fort bodies in the closure.
   Infer summaries and check ownership obligations before build-mode transformations (D19.8).
   This includes std.rt from std/rt.ft. A proof failure is a source error (exit 1).
4. Emit one LLVM IR module for the closure to `<tmp>/<entry>.ll` (D19.1), or to the `-S` output
   and stop.
5. Run `<cc>` over that module once: it compiles and links in one invocation (D14.3), exit 2 on
   failure. The line is

   ```sh
   clang --target=x86_64-linux-gnu -O1 -fPIE -Wno-override-module \
       -o <out> <tmp>/<entry>.ll <-l options> <-Xcc args>
   ```

   This line is the Linux x86-64 form. Mac arm64 uses the same line with
   `--target=arm64-apple-macosxM.m.p`.
   Both targets use `-O2` in place of `-O1` under `--release` (D14.3).
   Both add `-c` before `-o` and omit the `-l` options for `-c`.
   The line passes no `-pie`: clang links a position-independent executable by default on
   both targets.
   The module is the only compiler-produced input: it holds the runtime (D9.10, D13.1).
   Linux clang finds the cross sysroot, `Scrt1.o`, `crti.o`, `crtn.o` and linker itself.
   Mac Apple clang finds its active SDK and Mach-O linker through the host toolchain.
   The default `<cc>` is `clang` on both targets; an explicit `--cc` overrides it.
   `-Wno-override-module` silences a triple warning; `.ll` tells clang the input is IR.
   The line passes no `-x ir` and links no object from a fort C runtime.
6. Remove the temporary directory.

`--check` stops after step 3 (D20.1): it emits no module, creates no temporary directory, runs no
`--cc`, and does not apply the entry-point rule of D8.6, since the file it is given is a module
under inspection rather than a program. Selected ownership analysis uses symbolic inputs for
available fort bodies. Check and build modes use the same ownership proof (D20.1, D19.8).

- The temporary directory comes from `mkdtemp` under `$TMPDIR` (default `/tmp`) and is removed
  whether or not `--cc` succeeded.
- `--cc` uses the selected built-target line above, in that order.
  `-fPIE` and the target's PIE link flag make a position-independent executable (D14.3, D16).
- Every emitted module passes `opt -passes=verify` (D19.1): the language-test harness verifies
  the module of every test that compiles (`run_tests.py --verify-ir`, section 7.3), and the
  emitter suites verify each module they emit. `llvm.trap` raises SIGILL on x86-64 and SIGTRAP
  on arm64 (`test/lang/run/ffi/009`).
- An object from `-c` contains the whole program, the runtime included, so linking it needs no
  input the compiler produced beyond the object itself (D9.10, D13.1).

Where things live: `<std-dir>/*.ft` holds the standard library modules of D13.2 (`std.sys`,
`std.libc`, `std.rt`, `std.mem`, `std.io`, `std.str`, `std.strbuf`, `std.vec`,
`std.strmap`, `std.math`) as source, compiled with every program that imports them;
`std.rt` is the runtime (section 5) and is compiled with every program, imported or not (D9.10);
`<bindir>/fort` is the compiler, and `<bindir>/std` its fallback `--std-dir`. Only
modules in the import closure are read (module-system.md 10). A `--std-dir` (or `FORT_STD_DIR`)
that does not hold the library therefore fails every compile, a program with no `import` at all
included, with `module 'std.rt' not found` naming a module the user never wrote: the runtime is
read from there like any other standard library module.

### 2.1 Ownership entry and exit boundaries

Selected analysis checks all available fort bodies, including uncalled bodies (D17.19, D19.8).
It preserves global state and inferred caller requirements across ordinary function returns.
Library checking uses symbolic inputs and global states.
It still rejects local leaks and invalid source uses at their applicable boundaries.
It does not invent a foreign host call sequence or an executable entry.
Check and build modes use the same proof for the same closure and boundary assumptions (D20.1).

A build supplies the generated executable boundary (D11.6).
Account for static global initialization, runtime args_init, args, and source main in order.
The runtime argument-owning global exists even when source main has no argument parameter.
Positive argc allocates its headers. Nonpositive argc leaves it empty.
On source main return, apply its defers before its normal-return checks.
Then the generated call of `std.rt.shutdown` flushes runtime buffers and releases the
runtime-owned argument-header storage, in that order.
Check final owning-global emptiness before the generated C return.
Include imported library and runtime globals, and live owned descendants of their allocations.
Generated startup and shutdown can lie outside source-function FIR.
A count of lowered fort functions does not prove coverage of those generated effects.
Require explicit analysis coverage of the complete generated sequence.

Known normal process termination through std.sys.exit or std.rt.exit has the same final obligation.
Apply runtime cleanup before that boundary. Do not check argument-owner emptiness at call entry.
Establish the cleanup from analyzed fort bodies or verified generated effects.
An intended runtime guarantee does not replace an absent implementation effect.
These calls execute no caller defers and automatically delete no user globals.
Residual owners in suspended caller frames must also discharge before normal termination.
Infer and instantiate these requirements through fort wrappers and indirect fort targets.
Check prior releases, transfers, and retained borrows in their actual effect order.

Panic, runtime failures, and compiler traps use the abort cleanup exemption (D17.19).
Noreturn alone supplies no normal-exit or abort classification.
Unresolved foreign termination uses D17.13 trust and establishes no proved final boundary.
Unknown fort bodies receive no such exemption.
Library cleanup requirements and external host limits appear in memory-model.md 2.9.
These rules change no ABI representation. The runtime cleanup is the entry point
`std.rt.shutdown` of section 5.1: the generated `main` and `std.rt.exit` call it (D11.6).

## 3. Build modes

Two modes (D11.1); `--no-bounds-check` is an orthogonal switch (D10.6). The compiler itself
optimizes nothing in v1 and emits the same shape of IR in both modes; the only difference besides
the checks below is that `--cc` compiles the module with `-O2` instead of `-O1` (D14.3).

| Check                                              | checked | `--release` | `--no-bounds-check` |
|----------------------------------------------------|---------|-------------|---------------------|
| signed overflow: `+ - *`, unary `-`, `++ --`, `+= -= *=` | trap | wrap    | unchanged           |
| unsigned overflow, same operators                  | trap    | wrap        | unchanged           |
| `+% -% *%`, `+%= -%= *%=` (D11.2)                  | wrap    | wrap        | unchanged           |
| shift count negative or `>=` width (D11.1)         | trap    | masked      | unchanged           |
| `/ %` by zero, `MIN / -1`, `MIN % -1` (D6.13)      | trap    | trap        | unchanged           |
| index out of range (D6.8)                          | trap    | trap        | removed             |
| span bounds `0 <= lo <= hi <= len` (D6.9)          | trap    | trap        | removed             |
| `new`: negative count, size overflow, no memory    | trap    | trap        | unchanged           |
| store over a live `own` value (D17.11)             | trap    | no check    | unchanged           |
| `assert(cond)` (D12.2)                             | trap    | trap        | unchanged           |
| `panic(msg)`                                       | trap    | trap        | unchanged           |
| constant index or fold out of range (D4.4, D6.8)   | compile error in every mode                 |
| `cast` (D3.14)                                     | never traps in any mode                     |

"Trap" is the runtime error contract of D11.4: flush, one line on stderr, `abort()`. "Wrap" is
two's complement; "masked" means `count & (width - 1)`; "no check" means the store happens and
the allocation the old value designated would leak without the static proof (D17.11).
Selected ownership analysis rejects a store that loses that obligation in all build modes (D19.8).
Programs must not rely on wrapping or
trapping for correctness (D11.1); the wrapping operators exist for code that needs wrap-around in
both modes. A raw-pointer span adds no runtime range check (D6.9).
Fort storage still requires source and extent proof. Foreign storage uses D17.13 trust.
The undefined behaviors of D10.7 remain undefined in every mode.

## 4. Diagnostics

Ownership errors use the existing source diagnostic contract (D17.14, D20.1).
Check and build modes report the same proof verdicts under the same entry and boundary assumptions.
Library checking retains inferred global requirements; a build instantiates them at executable
entry.
JSON keeps its existing schema and includes the same error and note ranges (D20.2).

Compile-time diagnostics (D14.2) are written to stderr. Each one starts with a header line:

```sh
<file>:<line>:<col>: error: <message>
<file>:<line>:<col>: note: <message>
```

- `<file>` is the path the compiler opened: the entry file as given on the command line, an
  imported module as `<root>/<relative path>` with the root as given (`-I lib` gives
  `lib/util.ft`; the entry directory is the entry path's directory part, or nothing).
- `<line>` is 1-based; `<col>` is the 1-based column of the offending token's first byte, a tab
  counting as one column. A `note:` follows the `error:` it belongs to, with its own position.
- A position is the start of a range (D20.4): the diagnostic is about the bytes from `<line>`
  and `<col>` to one past the last byte of the construct's last token, and every syntax-tree
  node carries that range and the range of its name. The header line prints the start only, so
  an end never appears in a header line.
- Under each header line the compiler writes the rendered lines of its range (D14.2). The first
  is the source line where the range starts, the second is an underline, and each starts with
  four spaces. The underline has `^` at `<col>` and `~` up to the end column, exclusive; an empty
  range has the `^` alone. A range that ends on a later line is underlined to the end of its
  first line, and its later lines are not shown. Before the `^`, the underline copies each tab of
  the source line as a tab and writes a space for every other byte, so the `^` stands under its
  byte at any tab width. Columns count bytes, so a multi-byte character before the range moves
  the `^` one column right for each extra byte on a terminal that shows the character in one
  column. The header line is exact either way. A column past the end of its line stands one past
  the last byte of the line.
- The empty range at 1:1 is no position in the file and shows no rendered line. A lexical error
  at the first byte of a file has that range too, because the lexer reports a position and no
  token there (a rejected byte is not a token), so it shows its header line alone:
  `lex.ft:1:1: error: unexpected character '$'` has no line under it. A file the
  compiler cannot read, and a line the file does not have, show no rendered line either; the
  header line is written as before. The compiler reads the file through the same source it
  compiled, so the rendered line is the text the diagnostic is about.
- A rendered line starts with a space. A header line starts with its file path, and a path can
  start with a space: `fort --check " sp.ft"` prints a header line that starts with one. For a
  path that does not start with a space, a line that starts with a space is a rendered line and
  a line that starts with any other byte is a header line. The test harness relies on that, and
  no path of the test corpus starts with a space (7.3). A tool that reads the header lines only
  never reads a source line that itself spells `x.ft:1:1: error: y` as a diagnostic.
  `fort --check --json` writes no text form (section 4.1).
- An error without a position in the file, such as a missing `main`, uses `1:1` (D14.2). The
  entry file's base name is not one of these: it need not be a valid module name at all (D9.1,
  `module-system.md` 2).
- A lexical error is reported and lexing resumes at the start of the next line (D14.2): the line
  the error stands on is dropped whole, the tokens already lexed on it included, so a file reports
  at most one lexical diagnostic per line and the token stream covers the rest of the file and
  ends at the end of it. The parser reads those tokens and recovers from what the missing line
  broke, which costs it nothing when the line was a statement of its own and costs it the
  enclosing construct when the line carried a `{`, a `(` or a declaration header. The
  declarations after a half-typed literal therefore still reach the syntax tree and an editor
  keeps its index of them; a file that reported anything is not checked.
- After a syntax error the parser reports it and unwinds the construct it was parsing, reporting
  nothing more until it reaches a recovery point: the statement loop of a block or of a `case`
  clause, the clause loop of a `switch`, the field loop of a struct body, or the declaration loop
  of the module. There it skips what is left
  of the failed construct, keeps the skipped tokens as an error node that every later pass skips,
  and parses on, so a file reports one diagnostic for each construct that failed (D14.2). A
  parameter list, an argument list, an import item list and an enum body have no recovery point
  of their own and recover through the construct that encloses them.
- A skip runs to the end of the failed construct. It consumes at least one token, so it always
  makes progress, and then, outside the `(` and `[` the construct left open, consumes a `;`, and
  consumes the `}` that closes a brace it saw opened; it stops before a `}` it did not see opened
  and before a token that starts an ordinary top-level declaration, the end of the file ending
  every skip.
  The outermost brace pair that the skip saw opened ends the skip at its `}`, with one
  exception. Inside an open `(` or `[`, which the construct left open or the skip opened, a
  brace literal does not end the skip when a `;`, `,`, `)` or `]` follows its `}`. The skip goes
  on to the end of the group, so the `;` of a `for` header and the `,` of an argument list are
  not boundaries: `for (i32 mut[2] a = {}; a < 3; a++)` reports once. The token directly before
  the `{` decides that the pair is a literal: a `=`, `,`, `(` or `[`, or an identifier that a
  `=` or a `,` comes directly after. Any other `{` opens a block, so a block that a stray `;`
  follows still ends the skip. A struct literal with a qualified name and a typed array literal
  count as blocks. A block whose `{` comes directly after a `,`, `(` or `[` counts as a literal.
  Which other tokens stop it depends on the recovery point: a skip that stands where a statement
  or a clause would stops before `case` and `default`, since a switch body holds nothing else,
  and one that stands where a statement would also stops before a statement keyword (`if while
  for switch defer return break continue do $if`). A skip at the top level also stops before
  `$if`, which starts the next conditional declaration. A struct field is skipped to its `;` or
  to the `}` of the body, and a skip at the top level, where a `}` closes nothing, consumes one.
  Two
  lookaheads settle the braces a skip did not see opened: a `}` that a `;` follows closes a brace
  initializer or a struct literal the construct opened, since no block is followed by a `;`
  (D7.3), and a `}` that a `)` or a `]` follows stands inside a bracket the construct left open,
  where it closes nothing; both go with the skipped region. A `{` a construct left open is not
  counted: which brace it was meant to be is not decidable from the tokens, so the skip leaves
  the next `}` to the body it belongs to and drops the unclosed construct instead.
- A block, a `case` clause, a struct body and an enum body also end where a top-level declaration
  starts: at `struct`, `enum`, `extern`, `import`, or a `fn` followed by a name and then a `(`
  (a `fn` at statement level is a function type, whose `(` comes at once, D3.10). A file with a
  missing `}` therefore reports `expected '}', found 'fn'` once, at the
  declaration that follows it, instead of one diagnostic per following declaration (D14.2). The
  mirror holds for the `{` of a function body, a struct body or an enum body, which a complete
  declaration header precedes: a missing one is reported once and the body is read as though it
  were there, instead of the body being read as declarations.
  This missing-`}` boundary does not include `$if`. A `$if` form is valid inside a block and a
  `case` clause, so those bodies parse it as the next statement.
- A file reports at most 20 diagnostics, its lexical and its syntax errors counted against one
  budget, and the parser drops an error that starts where the one reported before it started; the
  lexer and the parser go on silently after either, so the tokens and the tree cover the whole
  file whatever was reported (D14.2). Diagnostics come out in the order they are reported,
  which is source order except where a construct is judged after its parts are parsed: the
  features the bootstrap lacks (toolchain.md 7.3) are reported that way, so a `do`-`while` whose
  body has a mistake reports the body's line first. Nothing reads a diagnostic's position
  relative to another's: the test harness matches each `error:` line to the annotation on its own
  line (D14.5).
- Every module of the closure is checked in dependency order (D14.2); a file with a syntax error
  is parsed whole and not checked. A declaration whose check failed has the error type, which
  silences every later diagnostic involving it, so an importer sees only its own errors.
- A diagnostic about two declarations stands at the declaration in the module the compiler is
  checking; within one module it stands at the later of the two by position (D14.2). The
  shadowing rule above is one case of it. Most such diagnostics report that one place alone:
  `'n' is already declared in this block`, `'n' shadows a parameter`, `'n' shadows an enclosing
  local`, `duplicate parameter 'n'`, `duplicate field 'n'` and `duplicate enum value N for 'n'`.
  A diagnostic that reports the other declaration as well makes that second report its note, and
  two do: `conflicting declarations of extern '<name>'` (`module-system.md` 13) and
  `redeclaration of '<name>'` (D7.9). Dependency order puts an imported module before its
  importer (D9.10) and leaves two modules that neither imports unordered. The error stands in the
  module being checked, so the module checked second carries it either way, and the note can name
  a module the reader did not write. The reason is section 9.2: a client that cannot
  open a file may drop that file's diagnostics, and a nested note goes with the error it follows,
  so an error placed in the file the reader cannot open leaves the reader nothing at all. A
  diagnostic about anything but two declarations stands where the construct it names stands, in
  whatever file that is, because it has no second place that says as much.
- A module-level `$if` with both an import leaf and a declaration leaf reports
  `a module $if cannot contain both imports and declarations` at its opening `$if`. An import
  selector after a declaration reports `an import comes before every declaration` at its opening
  `$if`, even when its selected branch is empty (D21.3).
- A `$if` condition that contains `sizeof` reports `'sizeof' is not available in a $if condition`
  at its first `sizeof`, before the selector evaluates the chain. An operand that the result does
  not select reports too. A reached declaration and a fixed-array `.len` length report the same
  text at their first `sizeof` (D21.2).
- The compiler emits no warnings (D14.2): unused imports, unused variables and statements after
  a terminating statement are not diagnosed.

```sh
main.ft:5:1: error: redeclaration of 'twice'
    fn twice(i32 n) i32 {
    ^~~~~~~~~~~~~~~~~~~~~
main.ft:1:1: note: previous declaration of 'twice' here
    fn twice(i32 n) i32 {
    ^~~~~~~~~~~~~~~~~~~~~
main.ft:11:5: error: cannot assign to immutable 'x'
        x = 2;
        ^
main.ft:12:14: error: the initializer expects u8, not i32
        u8 b = x + 1;
                 ^~~
util.ft:15:1: error: expected ';', found '}'
    }
    ^
```

The range of a function declaration ends at the `}` of its body, on a later line, so its
underline stops at the end of the header line. The third diagnostic stands at the `+`, because a
binary expression anchors its range at its operator (D20.4), and its range ends after the `1`.

Runtime diagnostics (D11.4) use the same position syntax, followed by `abort()`, so the shell
reports status 134:

```sh
main.ft:12:14: runtime error: index 5 out of range for length 3
main.ft:16:9: runtime error: overwriting owned value
main.ft:20:5: panic: queue empty
main.ft:31:5: assertion failed: n > 0
```

The position of a runtime error is the operator token of the failing operation (`[`, `+`, `-`,
`*`, `/`, `%`, `<<`, `>>`, `++`, `--`, a compound-assignment operator, unary `-`, the `=` of an
assignment for the overwrite check of D17.11) or the builtin name for `new`, `assert` and
`panic`. The assertion text is the verbatim source text of the argument. Runtime messages are
listed in section 5.

### 4.1 The check document (D20.2)

`fort --check --json entry.ft` writes one JSON document to stdout instead of the
compile-time lines of this section and no text diagnostic; the `fort: error:` lines of section 1
stay on stderr. The document is built whole and written with one `fwrite`, so stdout holds a
complete document or nothing: a client reads exit 0 or 1 with a document as a verdict and exit 2
with empty stdout as a crash. The document is one line ended by a newline; it is shown here over
several:

```json
{"version": 1,
 "files": ["main.ft", "util.ft"],
 "diagnostics": [{"file": "main.ft", "line": 7, "col": 5, "end_line": 7, "end_col": 6,
                  "severity": "error", "message": "cannot assign to immutable 'x'",
                  "notes": [{"file": "main.ft", "line": 3, "col": 9, "end_line": 3, "end_col": 10,
                             "message": "'x' declared here"}]}],
 "symbols": []}
```

- `"version"` is 1 for this form; a client that reads another number stops.
- `"files"` lists every file the compiler read, in that order, each spelled as the `<file>` of a
  diagnostic is, so a client knows which files it may clear stale diagnostics for. A file that was
  reached but could not be read is not listed.
- A diagnostic carries the whole range of D20.4: `"line"` and `"col"` are the start the text form
  prints, `"end_line"` and `"end_col"` are one past its last byte, all 1-based with a tab counting
  as one column. Columns are byte columns; converting them to UTF-16 code units is the client's
  job. `"severity"` is `"error"` on every diagnostic the compiler reports as one, there being no
  warnings in v1 (D14.2, D20.2).
- The `note:` lines of an error are nested in its `"notes"`, in order, each with its own range; a
  note carries no severity there. A note that follows no error has no error to nest under and
  stands as a diagnostic of its own, whose `"severity"` is `"note"`.
- `"symbols"` is the identifier index of section 9.1, which `--index` fills and which is the
  empty array without it (D20.3).
- Diagnostics appear in the order they were reported, which is the order of the text form.
- `--json` is a usage error without `--check`: the `--cc` a build spawns inherits stdout, so the
  guarantee above is the check mode's alone (D20.2). The document is written only when the
  compiler reaches a verdict: a usage error, a toolchain error or an internal error leaves stdout
  empty and exits 2 (D14.1).

### 4.2 Ownership proof diagnostics (D17.18)

Selected ownership analysis uses the text format of section 4 and the JSON form of section 4.1.
Ownership rejection and incomplete proof are compilation errors with exit 1.
Use the same source ranges, messages, notes, and order in both forms.
JSON version 1 gains no ownership-specific fields.

**Evidence classes.** A concrete reaching-path witness identifies feasible input conditions and
ordered FIR effects that reach the invalid operation.
Validate its alias substitutions, branch conditions, source identities, and effect order.
A witness can cross loops or recursive summaries only with a validated path or inductive argument.
A collection of independent may-facts does not supply that validation.
A witness can also cross an unresolved direct call of a fort callee that has a returning type and
no parameter that can own a value, a recursive callee included. It assumes that the callee
returns (D17.18, fir.md 14.1 A06): that is the one input condition that it does not prove.
After that call it takes no branch whose condition depends on an entry value that the call can
read (D17.18): through an argument, or through a value that the function stored before the call
outside a local whose storage no FIR address names. An argument of an earlier call counts as
stored. After a branch whose condition depends on any entry value, it crosses no fort call.
Such a branch or call leaves the event incomplete proof.
Amended 2026-10-08: a witness crossed no unresolved fort call. A callee that never returns
normally makes such a witness false. The cost is a false violation, never a missed one.
Amended 2026-10-09: a witness took each branch after a crossed call. A callee that aborts for
some values only, such as `require(n < 100)`, then gave a false violation.

A validated witness permits a path-dependent error: the operation fails on that reaching path.
Use unconditional wording only when the invalidity holds on all represented reaching paths.
An abstract possibility without a validated witness permits only an incomplete-proof error.
It does not establish that the program has an actual memory error.
Loss of witness precision cannot remove the safety obligation or produce successful compilation.

| Event | Primary error location and content |
|---|---|
| Invalid use | The use; identify the place and invalid source on the validated path. |
| Lost ownership | The overwrite, shallow release, or storage end; identify the residual owner. |
| Failed caller requirement | The call; identify its unsatisfied substituted requirement. |
| Incomplete proof | The affected operation; name the unproved storage or cleanup requirement. |
| Work-budget failure | The attempted operation; identify the work category and bound. |

Each event retains the responsible function and operation key, source range, reason, and evidence
class. It also retains the relevant source and owner paths, conditions, and finite witness links.
A precision-limit event names its category, numeric bound, and first source point that loses the
required fact. A work-budget event records its used count and the attempted operation.
These internal events are compiler interfaces. They require no source or foreign contract syntax.

Attach notes for allocation, lending, release, transfer, or storage end when they explain the error.
An unsatisfied caller requirement notes the callee operation and the substituted caller source or
alias. A deferred-effect note includes its registration range and the applicable exit range.
A limit note states what the abstraction loses. Do not state that widening releases storage.
Use "cannot prove" for missing proof. Do not use "use after release" without validated invalidity.

Representative primary messages follow. The named paths and counts come from the event.

```sh
case.ft:8:5: error: cannot prove that 'view' designates live storage
case.ft:4:13: note: access-path depth 2 loses the required source relation here
case.ft:9:5: error: ownership analysis exceeds the transfer-work bound 2
case.ft:9:5: note: 2 work units complete; this operation needs another work unit
case.ft:12:5: error: cannot prove completed cleanup at this return
case.ft:7:5: note: this incomplete fort summary preserves a possible caller return
```

The numbers in these examples illustrate event rendering. They select no production limit.
Do not emit both an incomplete-proof and budget error for the same unresolved obligation.
Preserve proved independent errors when another operation gives incomplete proof.
Suppress a dependent cascade only for the same operation, source relation, and underlying reason.
Do not silence a separate owner, call requirement, storage boundary, or termination outcome.
Diagnostic suppression affects rendering, not abstract state, obligations, or the final verdict.

Order ownership events by canonical module and function keys, then source range, operation key,
event class, and source relation key. Sort notes by causal role and then their stable source keys.
Use that same order for duplicate selection and report-budget exhaustion.
Do not use work-queue arrival or allocation addresses to select the first diagnostic.
If a report bound omits detail, preserve a terminal incomplete-proof error and exit 1.
The implementation records its numeric bounds and measured coverage under FIR 14.1.

## 5. The runtime

The runtime is `std.rt`, an ordinary fort module (`<std-dir>/rt.ft`) that the compiler loads into
every import closure and emits into the program's module like any other (D13.1, D9.10). It owns
process start and exit (D11.6), heap allocation (D10.2, D10.3), the runtime-error and panic paths
(D11.4, including the ownership overwrite check of D17.11), formatting and buffering for the print
family (D11.5, D11.7, D12.2, D18), and the program arguments for `std.sys`. It imports `std.libc`
and reaches the operating system through it (D9.8): `write` for the buffers, `calloc` and `free`
for `alloc` and `free` below, `abort` for the failure paths, `isatty` for D11.5's question and
`strlen` for `args_init`. A program that writes `import std.rt;` calls it as it calls any module
(D9.3).

Four things about it are not ordinary, and they are the whole list: it is a root of every closure,
imported or not (D9.10); the compiler knows the names of section 5.1 and emits calls to them
(D12.2); its `struct enum_member` is the `%fort.enum_member` of section 6 item 2 rather than a
`%struct.` of its own; and the definitions of its `noreturn` entry points carry the attribute
group of item 14, which no other fort function gets. Everything else in it -- its buffers, its
helpers, its own `extern` declarations -- is an ordinary module's.

The compiler holds the list of names below, since fort has no attribute with which a module could
mark a declaration as the target of a builtin (D12.2), and emits its calls against the signatures
this section fixes, by the mangled names of D9.7 (`@"std.rt.print_i64"`). It emits no declaration
for any of them: the module that holds the call holds the definition too (section 6 item 8).

### 5.1 Entry points

The signatures are fort, and `std.rt` defines each one; fort has no prototype, so the bodies are
simply left out here. Their IR follows from the type table of section 6
item 2 and the convention of item 7 and is nothing special: a `bool` result is `zeroext i1` and a
`bool` parameter `i1 zeroext`, `i8` being `bool`'s memory type alone; a `char` parameter is
`i8 zeroext`; `u64` and `i64` are both `i64` (D9.9). An aggregate result is the leading
`ptr sret(%T)` of item 7 on a `void` function, which is what `args` returns through. `loc`
abbreviates the three parameters `char* file, u32 line, u32 col`, the position of D11.4. Every
`fail_*` function, `panic`, `assert_fail` and `exit` is `fn noreturn` (D8.5).

```fort
// The table a print of an enum reads (D3.9, D12.2). It is the fort type of the
// `%fort.enum_member` the compiler emits (section 6 items 2 and 21).
struct enum_member { i32 value; char* name; }

// Allocation (D10.2, D10.3). `alloc` returns zeroed storage for `count` elements
// of `elem_size` bytes, at least one byte so the result is never null (`new(T, 0)`
// is non-null); an overflowing product or a failed allocation is a runtime error
// at `loc`. `free` releases what `alloc` returned; a null `p` is a no-op.
// Ownership (D17) is erased: the runtime sees plain pointers, and the compiler
// zeroes a `del` or `move` operand itself (section 6, items 17 and 18). The
// result is `void mut* own`, the type `libc.calloc` answers with: storage of no
// type that the caller owns and may write (D3.11, `stdlib.md` 2.2). `alloc`
// carries that mark out rather than dropping it, so a caller that reaches this
// entry point directly casts to a typed `own` pointer to write through it, and
// the cast carries the `own` and adds no `mut` (D3.14, D17.3). `free`
// takes `void* own`, which every result of `alloc` reaches by the monotone drop
// of D5.4. The mark reaches no instruction, no signature and no size: every
// pointer is one machine word, so the IR form of item 7 is `ptr` either way
// (D3.11). It does move one source column, the declaration of the local that
// holds `calloc`'s result being four characters further right, which the
// overwrite check of section 6 item 18 records.
// The two are not called `new` and `del`: `new` is a keyword (D2.4), and `del`
// is a universe function that a module-level declaration of that name shadows
// (D12.2, D7.9) -- inside `std.rt`, which releases its own buffers and its argv
// storage with `del`, that would cost the module the operation it needs. A name
// is free to take when the module implements the builtin rather than using it,
// which is why `panic` below is `panic`.
fn alloc(u64 elem_size, u64 count, char* file, u32 line, u32 col) void mut* own;
fn free(void* own p) void;

// Strings (D3.7). `str_eq` is true when the two strings have the same length and
// the same bytes, which is what `==` and `!=` on strings compare, so the zero
// string equals `""`. The compiler emits no call to a C symbol of its own accord
// (D9.8), so the byte comparison this needs is the runtime's, reached through
// `std.libc` like every other call it makes.
fn str_eq(char* a, u64 a_len, char* b, u64 b_len) bool;

// Failures (D11.4): flush every buffer, write one line to stderr, abort.
// Values arrive sign-extended to 64 bits; `hi` is `len` for `e[lo..]`;
// `type_name` is the NUL-terminated name of the shifted operand's type, and it
// carries the suffix because `type` is a reserved word (D2.4); `text` is the
// NUL-terminated source text of the assert argument. `fail_div_overflow` is
// `MIN / -1` and `MIN % -1`; `fail_alloc_count` is `new(T, n)` with a negative
// signed `n`; `fail_overwrite` is an assignment to an `own` reference-typed
// lvalue whose current value is not zero (D17.11), emitted in checked builds
// only; `fail_enum` is the default a switch over an enum with no `default`
// clause is given (D7.7), where `type_name` is the enum's name, and it is
// emitted in both build modes.
fn fail_bounds(i64 index, u64 len, char* file, u32 line, u32 col) noreturn;
fn fail_span(i64 lo, i64 hi, u64 len, char* file, u32 line, u32 col) noreturn;
fn fail_overflow(char* file, u32 line, u32 col) noreturn;
fn fail_shift(i64 count, char* type_name, char* file, u32 line, u32 col) noreturn;
fn fail_div_zero(char* file, u32 line, u32 col) noreturn;
fn fail_div_overflow(char* file, u32 line, u32 col) noreturn;
fn fail_alloc_count(i64 n, char* file, u32 line, u32 col) noreturn;
fn fail_overwrite(char* file, u32 line, u32 col) noreturn;
fn fail_enum(i64 v, char* type_name, char* file, u32 line, u32 col) noreturn;
fn panic(char* ptr, u64 len, char* file, u32 line, u32 col) noreturn;
fn assert_fail(char* text, char* file, u32 line, u32 col) noreturn;

// Printing (D11.5, D11.7, D12.2): format one value per D11.7 and append it to the
// buffer of `fd`. A float arrives in its own type and prints with the shortest
// digits that round-trip in that type (D18); `print_f32` and `print_f64` are the
// two float entry points of D18.1 and D18.4, and an `f32` is never widened to an
// `f64` first, because the digits depend on the type.
// `flush` writes out one buffer (`io.close` and `io.flush` call it); `flush_all`
// writes out every buffer, at exit and before every failure. A buffer whose
// descriptor is a terminal is written out at every newline too (D11.5, 5.3).
fn print_i64(i32 fd, i64 v) void;
fn print_u64(i32 fd, u64 v) void;
fn print_f32(i32 fd, f32 v) void;
fn print_f64(i32 fd, f64 v) void;
fn print_bool(i32 fd, bool v) void;
fn print_char(i32 fd, char c) void;
fn print_ptr(i32 fd, void* p) void;
fn print_str(i32 fd, char* ptr, u64 len) void;
fn print_enum(i32 fd, i32 v, enum_member* m, u64 n) void;
fn flush(i32 fd) void;
fn flush_all() void;

// Process (D11.6, D8.6). The compiler emits `main(argc, argv)` in the entry
// module (section 6 item 22): it calls `args_init`, which builds the argument
// span from `argv` (one string per argument, NUL-terminated, since it is the
// `argv` byte sequence itself), then `args`, then the entry module's `main`,
// then `shutdown`. It returns status & 0xFF.
// args() lends the span during source execution and applicable source defers.
// shutdown is the runtime cleanup. It flushes every buffer first. Then it
// releases the runtime-owned header allocation and leaves args_store empty.
// The strings borrow argv bytes; shutdown does not release those bytes.
// A call that finds args_store empty releases nothing.
// exit calls shutdown, then ends the process through foreign exit with
// status & 0xFF. sys.exit calls exit. Neither path automatically deletes user
// globals. exit does not run caller defers. Section 2.1 fixes the final
// obligation boundary.
fn args_init(i32 argc, char* mut* argv) void;
fn args() string@;
fn shutdown() void;
fn exit(i32 status) noreturn;
```

`std.rt` is the only module the compiler names, and the list above is every name it knows
(D11.6, D18.4). It exports two more float functions, `append_f32` and `append_f64`, which give a
`str_buf` the bytes the printers give a descriptor (`stdlib.md` 2.12); the compiler emits no call
that names them, so they are not entry points and this list stays complete. The module's buffers,
its helpers and everything else it needs are its own and are exported like any module's (D9.6),
which makes them implementation details a program must not use (`stdlib.md` 1.1); `sys` and `io`
import it for `exit`, `args`, `flush` and `flush_all` (`stdlib.md` 3) and the rest of the library
leaves it alone. `errno` is not its business either: `sys.errno()` reaches libc's
`__errno_location` directly.

The two float printers stood in a module of their own, `std.rt_float`, until T-132. They stood
apart because a compiler that builds the runtime must accept floats to compile them and the C
bootstrap does not, and the C bootstrap compiled `std/rt.ft` into every closure it read. T-131
ended that: the C bootstrap builds pin 0's `src/fort` with pin 0's library and reads HEAD's
library never (`notes/compiler.md` 8, invariant 5), so `std/rt.ft` may hold a float and T-132
made the two modules one. Deleting `std/rt_float.ft` took a pin of its own, because the last
pin's loader read that file name for any closure holding a float (`notes/compiler.md` 8,
invariant 3). What the fold costs is measured: a program that prints no float paid
nothing for the split and now pays 8,549 bytes of `.text` and 105,914 bytes of emitted IR, because
a module emits every definition of every module of its closure with no reachability filter;
`notes/compiler.md` 7 holds the measurement and the command for each number.

The float printers are the one place where the runtime asks for a formatted value
rather than laying the bytes out itself. What D18.2 fixes is the text, not the method; one method,
and the one the C runtime used, is to ask `snprintf("%.*e", ...)` for one significant digit, then
two, and so on, and keep the first length whose text `strtod` reads back as the value, since 17
digits for `f64` and 9 for `f32` (`strtof`) always read back. `printf` returns the nearest decimal
of the length asked for, which is not always the one to keep: for a normal power of two above the
minimum normal the values that read back as it reach half an ulp above and only a quarter below, the
binade below being coarser, so the nearest decimal can fall short of that interval while the next
one up falls inside it, and that neighbour is worth trying before lengthening. The neighbour below
never needs trying, the gap below a float never being wider than the gap above. That method asks two
things of the C library which the standard permits but does not require and glibc provides, a
correctly rounded `printf` (ties to even) and a correctly rounded `strtod` down to the subnormals,
where it also reports `ERANGE`, which the caller ignores because only the value matters. Any
implementation that produces the digits of D18.2 and the layout of D18.3 is admissible.

### 5.2 Messages

Each failure writes exactly one line, after `flush_all`, then aborts (D11.4). The entry points
are those of 5.1, in `std.rt`:

| Entry point           | Line after `<file>:<line>:<col>: ` (D11.4)                   |
|-----------------------|--------------------------------------------------------------|
| `fail_bounds`         | `runtime error: index 5 out of range for length 3`           |
| `fail_span`           | `runtime error: span bounds 2..7 out of range for length 3`  |
| `fail_overflow`       | `runtime error: integer overflow`                            |
| `fail_shift`          | `runtime error: shift count 64 out of range for i64`         |
| `fail_div_zero`       | `runtime error: division by zero`                            |
| `fail_div_overflow`   | `runtime error: division overflow`                           |
| `fail_alloc_count`    | `runtime error: negative allocation count -1`                |
| `alloc` (overflow)    | `runtime error: allocation size overflow`                    |
| `alloc` (no memory)   | `runtime error: out of memory`                               |
| `fail_overwrite`      | `runtime error: overwriting owned value`                     |
| `fail_enum`           | `runtime error: enum value 0 is not a member of level`       |
| `panic`               | `panic: <message bytes>`                                     |
| `assert_fail`         | `assertion failed: <expression text>`                        |

`<file>` is as in section 4. Numbers in messages are decimal; the index, the span bounds and
the allocation count are printed as signed values. Falling off the end of a `noreturn` function
executes the trap of section 6 item 20 (D8.5, D19.7). Linux x86-64 raises SIGILL with no message.
Mac arm64 raises SIGTRAP with no message.

### 5.3 Buffering

| fd    | Used by                              | Policy (D11.5)                                   |
|-------|--------------------------------------|--------------------------------------------------|
| 1     | `print`, `println`, `fprint(1, ...)` | buffered, and line-buffered when interactive     |
| 2     | `eprint`, `eprintln`                 | unbuffered; every call writes immediately        |
| other | `fprint(fd, ...)`, `fprintln`        | one buffer per descriptor, same policy as 1      |

A buffered descriptor is flushed when its buffer is full, at exit, and before any runtime error.
It is *interactive* when `isatty` says so, which the runtime asks once, when it creates that
descriptor's buffer, and never again, so that no `print` carries a system call of its own; a
descriptor that is interactive is flushed at a newline as well, the whole buffer and not only the
bytes up to the newline. That is C's rule (C11 7.21.3p7) and the reason a reader cares is the
order it fixes: on a terminal each `println` appears as the program runs, and `print` and
`eprint` output interleave in the order the program wrote them, where before this rule a program
printing one line to each showed the `eprint` line alone until it exited. On a pipe or a file
nothing changes: `print` output waits for a flush, so redirecting a program still yields the same
bytes in the same few writes. Line buffering is decided per descriptor and not for stdout alone,
`fprint(fd, ...)` on a terminal being as interactive as `print` is.

Asking costs the program nothing it can observe: `isatty` fails with `ENOTTY` on a descriptor
that is not a terminal, and `sys.errno()` hands a program the errno of its own last call
(`stdlib.md` 2.4), so the runtime restores errno around the question.

`fprint(1, ...)` shares the stdout buffer with `print` (D11.5). The runtime tells descriptors
apart by number alone, so `fprint(2, ...)` behaves as `eprint`. An `extern` write to a
descriptor bypasses the buffers; a program that mixes the two on one descriptor flushes first
(`io.flush`, D11.5). A write error on any descriptor is ignored; the bytes are dropped. The
buffer size is the runtime's choice.

`test/tty_test.py` (the ctest `tty`) is the witness for the interactive half: the language
harness captures a program's stdout through a pipe, so no test under `test/lang` can take the
interactive path, and the script drives a compiled program on a real pseudo terminal instead.

## 6. Code generation contract

This section is normative for the compiler. The LLVM IR module it emits must satisfy every item
and must pass `opt -passes=verify` (D19.1).
The two worked examples illustrate LLVM types, calls, data, and runtime checks.
They use hand-written runtime stubs instead of the complete std.rt module (item 8, D9.10, D13.1).
Their args_init stubs allocate no argument headers. Their args stubs return an empty span.
Their `shutdown` stubs release nothing, and they retain no runtime argument owner.
Production startup with positive argc creates that owner and requires final cleanup (D17.19).
The examples omit production allocation and cleanup effects. They are not exact complete compiler
output.
Item 22 defines the required production entry sequence and its cleanup call, `std.rt.shutdown`
(section 5.1).
A change to the emitter's text is a change here.

1. **Form and module header.** One textual module (`.ll`, LLVM 18 syntax, opaque pointers) holds
   the whole program (D9.10, D19.1) and is built by appending text. It
   begins with

   ```llvm
   target triple = "x86_64-unknown-linux-gnu"
   ```

   for Linux x86-64. Mac arm64 begins with the selected
   `target triple = "arm64-apple-macosxM.m.p"` (D14.1, D19.1).
   Linux keeps its normalized header bytes. Neither target emits `target datalayout`.
   Apple clang derives the Mac layout from its triple. The module carries no `!llvm.module.flags`,
   no `!llvm.ident`, no `source_filename` and no comments (D19.1): clang derives the layout from
   the triple, and position independence comes from the `--cc` line (section 2), not from module
   flags. Sections appear in this order and nowhere else: the triple, the named types, the
   module-level globals and constants (D7.10), the function definitions, the private data, the
   declarations, the attribute groups. Forward references to globals are legal in `.ll`, which
   is what lets one pass emit a function before the data it names. Naming and ordering inside
   each section follow D19.5. Two `-S` runs keep equal source text, source roots, working
   directory, build mode, and selected target. Their arguments differ only in the `-o` path.
   That different output path does not change IR bytes.

2. **Type mapping** (D19.2). The value type is what a temporary holds; the memory type is what
   an `alloca`, a global or a field holds.

   | fort                     | value type        | memory type     | notes                        |
   |--------------------------|-------------------|-----------------|------------------------------|
   | `i8 i16 i32 i64`         | `i8 i16 i32 i64`  | same            | `sdiv`, `sext`, `icmp slt`   |
   | `u8 u16 u32 u64`         | `i8 i16 i32 i64`  | same            | `udiv`, `zext`, `icmp ult`   |
   | `bool`                   | `i1`              | `i8`            | 0 or 1 in memory (D3.3)      |
   | `char`                   | `i8`              | `i8`            | unsigned byte (D3.2)         |
   | `f32`, `f64`             | `float`, `double` | same            | D3.1                         |
   | `T*`, `void*`, `fn (P) R`| `ptr`             | `ptr`           | opaque (D3.10, D3.11)        |
   | `T[N]`                   | none              | `[N x T]`       | outside in (D3.6)            |
   | `T@`, `string`           | none              | `%fort.span`    | `type { ptr, i64 }`          |
   | `struct a.b.s`           | none              | `%struct.a.b.s` | fields in order, no `packed` |
   | `enum`                   | `i32`             | `i32`           | D3.9                         |
   | `void`                   | `void`            | none            | result type only             |

   Signedness is in the instruction, never in the type (D3.1), and an array type nests outside
   in, so `i32[3][4]` is `[3 x [4 x i32]]` (D3.6). Every load of a `bool` place is a
   `load i8` and a `trunc`, every store a `zext` and a `store i8`, so a `bool` field has C's
   `_Bool` layout and `std.rt.print_bool(i32, bool)` needs no special case; the
   `trunc`/`zext` pairs disappear in the optimizer. One `%fort.span` serves every span and
   `string`, because with opaque pointers `i32@`, `u8@` and `string` have identical IR (D3.5,
   D3.7). `%fort.span` and `%fort.enum_member = type { i32, ptr }` are emitted in every module,
   used or not, so the emitter tracks nothing; unused named types are legal. `%fort.enum_member`
   is the IR of `std.rt`'s own `struct enum_member` (section 5.1) and is emitted under that name
   rather than as `%struct.std.rt.enum_member`, one named type for one layout.

3. **Aggregates live in memory** (D19.3). Only scalars are SSA values: a struct, fixed array,
   span or `string` always occupies a place, is copied with `llvm.memcpy.p0.p0.i64`, zeroed
   (`{}`, `del`, `move`) with `llvm.memset.p0.i64`, and reached field by field or element by
   element with `getelementptr`. The emitter never loads or stores an aggregate as one value and
   never writes `insertvalue` or `extractvalue` on one; its only `extractvalue` takes apart the
   `{iN, i1}` of an overflow intrinsic (item 15). Exactly three `getelementptr` shapes exist,
   and all three always carry `inbounds`: `inbounds <arrty>, ptr %a, i64 0, i64 %i` for an
   element of a fixed array, `inbounds <elemty>, ptr %p, i64 %i` for an element reached through
   a pointer or a span's `.ptr`, and `inbounds %struct.x, ptr %s, i32 0, i32 <k>` for a field.
   `inbounds` is always true: an element access is preceded by its bounds check (item 16) and a
   field or header access is in bounds by construction, and `--no-bounds-check` removes the
   check's branch, never the `inbounds` (D10.6). This is D9.9's model spelled in IR.

4. **Symbols, linkage, visibility** (D9.7). The dotted names of D9.7 are quoted:
   `@"main.add"`, `@"std.io.read_file"`, `@"main.LIMIT"`; quoting is uniform and does not change
   the fort symbol name, which is `main.add`.
   C names (`extern` declarations, and the `main` the compiler emits) are unquoted; the runtime
   is fort, so `@"std.rt.print_i64"` is quoted like every other dotted name (D9.7). Fort
   functions, constants and globals are `dso_local` with the default external linkage (D9.6), so
   fort-to-fort calls are direct, a call into the runtime among them, and fort data is addressed
   PC-relative; `extern` symbols carry no `dso_local` and go through the procedure linkage and
   global offset tables. Private data (`@.str.N`, `@.file.N`, `@.enum.<path.name>`) is
   `private unnamed_addr`.

   A name LLVM's unquoted identifiers (`[-a-zA-Z$._][-a-zA-Z$._0-9]*`) do not admit is quoted
   too, which only an entry module's can be, since every other module path is identifiers
   joined with dots (D9.1): `%"struct.a\22b.point"` and `@".enum.a\22b.color"` for an entry
   file `a"b.ft`. Inside the quotes, the two bytes a quoted name cannot hold, `"` and `\`, and
   every byte outside the printable range are written as the `\XX` hex pair of item 5, which
   LLVM reads back to the byte. Mach-O adds a leading `_` to the external object symbol.
   That prefix stays outside the IR name and keeps the name mapping injective (D9.7).
   One target symbol is one IR entity: `main` is reserved, so the checker refuses an `extern` that
   declares it (D9.7, module-system.md 13) and the emitter declares no name it defines, which
   leaves the definition of item 22 alone. A runtime entry point is a fort definition in the
   module like any other, so nothing declares it either (item 8).

5. **Data emission.** Private data follows the function definitions, `@.file.N` constants before
   `@.str.N` before `@.enum.*` (D19.5):

   ```llvm
   @.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
   @.str.0  = private unnamed_addr constant [7 x i8] c"before\00", align 1
   @"main.LIMIT"   = dso_local constant i32 100, align 4
   @"main.TABLE"   = dso_local constant [2 x ptr] [ptr @"main.f", ptr @"main.g"], align 8
   @"main.counter" = dso_local global i64 0, align 8
   @"main.origin"  = dso_local global %struct.main.point zeroinitializer, align 4
   ```

   - A string literal is `[len + 1 x i8]` with the trailing NUL that `len` excludes (D3.7);
     non-printable bytes are written as `\XX` hex pairs.
   - `constant` for module constants (D7.10) and for private data, `global` for `mut` globals;
     an all-zero `global` lands in `.bss` and a nonzero one in `.data` by itself, and a
     `constant` whose initializer holds a relocation lands in `.data.rel.ro` because the module
     is compiled as position-independent code (item 6).
   - `unnamed_addr` marks private data only: a named fort constant keeps its address significant
     because `&CONST` is expressible (D6.7).
   - Every global, `alloca`, `load` and `store` carries an explicit `align N` from D3.1 and
     D3.8.
   - Only referenced private data is emitted: a program with no check has no `@.file.N`
     (the example of 6.1), and an enum table exists only if some `print` of that enum type is
     compiled (item 21).

6. **Position independence** (D14.3, D16). Nothing in the IR expresses it: `dso_local` (item 4)
   and section 2's `-fPIE` and target PIE link flag give PC-relative data.
   Fort-to-fort calls stay direct; C externs use the target's linker stubs or linkage tables.
   The one requirement the module carries
   is that an address is never an integer constant derived from a symbol; addresses appear only
   as `ptr` values and as `ptr` constants in initializers.

7. **Calling convention, fort to fort** (D9.9). Scalars (integers, `bool`, `char`, enums,
   pointers, function pointers, floats) are ordinary parameters and results.
   LLVM applies System V on Linux and Apple arm64 on Mac.
   A struct, fixed array, span or `string` argument is a plain `ptr` parameter: the
   caller allocates a copy in its entry block, `llvm.memcpy`s into it and passes its address,
   and `byval` is never used, since it would mean a callee-visible copy on the stack rather than
   the pointer in the integer slot D9.9 requires. An aggregate result is a leading
   `ptr sret(%T) %ret.sret` parameter on a function whose result type is `void`; the pointer
   arrives in `rdi` on Linux; the callee echoes it in `rax`.
   Both targets mark `sret(%T)` on the definition and at the call site.
   Apple arm64 then puts the result destination in `x8`, not `x0` (D9.9).
   A span or `string` is one hidden pointer and is never split into two scalars, so the entry
   module's `main` takes the argument span as one `ptr` (D9.9, D11.6). `bool`, `char`, `u8` and
   `u16` parameters and results carry `zeroext` and `i8` and `i16` carry `signext`, in fort and
   extern signatures alike, so an extern-legal signature is a valid C callback by construction
   (D9.9).
   Every fort definition is `define dso_local <ret> @"m.f"(...) #0`, where `#0` is
   `{ nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }` on both targets (D10.8).
   `nounwind` stands because fort has
   no exceptions, the frame pointer because it is what a debugger gets without DWARF (section
   9), and `probe-stack` for item 13.

   A call through a function pointer is an ordinary `call` whose callee is the `ptr` value and
   whose function type is written out, because an opaque pointer carries none (D19.2):
   `call i32 (i32, i32) %t0(i32 %t1, i32 %t2)`.
   An aggregate result is `call void (ptr, i32) %t0(ptr sret(%T) %r.0, i32 3)` on both targets.
   `opt -passes=verify` accepts the call-site attribute.
   The callee is evaluated before the arguments (D6.3) and every rule
   above holds at that call site unchanged, aggregate arguments and the extension attributes
   included, so it differs from the call of a name only in the callee and that type (D3.10). The
   callee is always a fort function, since an `extern fn` in value position is an error (D3.10).
   An extern direct call takes its target ABI form from its declaration (item 8).

8. **Extern declarations** (D9.8). An extern function uses unmangled C types.
   A fixed extern uses a fixed LLVM declaration and call form on both targets. The declaration
   carries `nobuiltin`:

   ```llvm
   declare i64 @write(i32, ptr, i64) nobuiltin
   declare signext i8 @c_narrow(i8 signext, i16 zeroext) nobuiltin
     %t12 = call i64 @write(i32 %t8, ptr %t9, i64 %t10) #3
     %t11 = call signext i8 @c_narrow(i8 signext %t8, i16 zeroext %t9) #3
   ```

   A C extern that writes `...` keeps its fixed prefix in a variadic LLVM type:

   ```llvm
   declare i32 @printf(ptr, ...) nobuiltin
     %t13 = call i32 (ptr, ...) @printf(ptr %t9, i32 %t11, double %t12) #3
   ```

   On Linux the variadic call type sets the System V vector-register count in `al`.
   Apple arm64 puts the variable `i32` and `double` arguments on the stack.
   A fixed declaration of C `printf` would use the wrong form on both targets.
   Only `i32`, `u32`, `i64`, `u64`, `f64`, pointers and function pointers can appear in that tail.
   The call site writes their actual LLVM types; unsigned integers use the same IR widths.
   `#3 = { nobuiltin }` stays on each extern call site on both targets.
   Each extern declaration also carries `nobuiltin`. Apple clang can
   otherwise mark `calloc` as an allocator whose call and matching `free`
   have no observable effect. This can remove an allocation failure when
   the caller reads no storage (D10.2, D11.4). The attributes prevent rewriting C calls
   without changing intrinsic lowering driver-wide.
   A function-pointer call is fixed (D3.10); an extern name is not a pointer value.

   The runtime needs no declaration at all: `std.rt` is in the closure (D9.10), so the module
   that holds a call to `@"std.rt.print_i64"` holds its definition, emitted from fort source like
   every other function (item 7). A `declare` beside a `define` is a redefinition `opt` rejects,
   and a call into the runtime is an ordinary fort-to-fort call, non-variadic, with the parameter
   and result attributes item 7 gives the fort signature of section 5.1 and no `#3`.

   The intrinsics are declared with the spellings LLVM 18 prints, in this fixed order, one per
   type actually used and none otherwise:

   ```llvm
   declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly,
       ptr noalias nocapture readonly, i64, i1 immarg) #5
   declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6
   declare { i8, i1 } @llvm.sadd.with.overflow.i8(i8, i8) #4
   declare i32 @llvm.fptosi.sat.i32.f64(double) #4
   declare void @llvm.trap() #7
   ```

   The `llvm.memcpy` declaration is one line in the module and is wrapped here only to fit the
   page. The order is: `llvm.memcpy.p0.p0.i64`, then `llvm.memset.p0.i64`, then the overflow
   family in the order `sadd ssub smul uadd usub umul` and, within each, the widths
   `i8 i16 i32 i64` (item 15), then `llvm.fptosi.sat.i<N>.f32`, `llvm.fptosi.sat.i<N>.f64`,
   `llvm.fptoui.sat.i<N>.f32` and `llvm.fptoui.sat.i<N>.f64` by ascending target width (item
   12), then `llvm.trap` (item 20). The parameter attributes shown are part of the spelling.

   Only referenced declarations are emitted, in a fixed order (D19.5): `extern` C functions in
   first-use order, which is the order of the first call of each in the text of the function
   definitions (`spec/fir.md` 12.5), then the intrinsics in the order of the table above, the
   two groups separated by a blank line. There are two groups and no third: a symbol is declared
   exactly once, and every fort function the module calls, the runtime's included, is defined in
   it. Two modules that declare one C symbol are two fort declarations of one target symbol and
   yield one `declare`, which is why the extern group is keyed by the C name and not by the
   declaration (D9.7, D9.8).

9. **Normalization** (D9.8, D19.2). A narrow value is not widened to 32 bits: an `i8` value has
   type `i8` and its width is in the type. The only extensions the emitter produces are the
   `zeroext` and `signext` attributes of item 7, which make LLVM normalize on both sides of a
   call, the `trunc` and `zext` of `bool`'s value and memory types, and the explicit conversions
   of item 12.

10. **Locals, control flow, evaluation order** (D19.4). Every local, scalar parameter copy and
    compiler temporary is an `alloca` in the entry block, before any other instruction, in
    declaration order; nothing is variable-length. Names are fixed by D19.5, per function and
    reset at each definition: `%t<N>` for an instruction result in emission order, `%L<N>` for a
    block by its FIR block number with FIR block 0 always literally `entry` and the failure blocks
    after the last FIR block (`fir.md` 12.4), `%<ident>.<slot>` for
    a local or parameter slot, `%<ident>.in` for an incoming parameter, `%ret.sret` for an
    aggregate result pointer, and `%tmp<K>` from a third counter for a place the compiler
    invents (an aggregate argument copy, a short-circuit slot). A name that embeds a fort
    identifier always contains a dot and an invented one never does, so a local named `tmp` is
    `%tmp.0` and cannot collide with `%tmp0` (D19.5).

    A scalar parameter arrives as `%<name>.in` and is stored into its slot immediately. An
    aggregate parameter is not copied again: its place is the caller-made copy the incoming
    `ptr` designates (item 7), which the callee may write to, since D8.2's by-value rule is
    satisfied by the caller's copy. Control flow is explicit blocks: `if`, `while`,
    `for`, `break` and `continue` become `br`; a fort `switch` on an integer, `char` or enum
    becomes an LLVM `switch` with one case per label and a default block: the `default` clause
    wherever it stands, the continuation when there is none, and, for an enum switch with no
    `default` clause, a failure block calling `std.rt.fail_enum` with the operand
    sign-extended to 64 bits and the enum's name, which is the default D7.7 gives it and which
    neither build mode removes; `&&`, `||` and
    `?:` short-circuit through a stack slot rather than a `phi`; after a terminating statement
    the lowering opens a fresh block for the unreachable statements D14.2 allows. Evaluation
    order needs nothing: LLVM keeps the order of side effects, the lowering orders the calls,
    loads and stores as D6.3 requires, `fd` once (D6.3, D12.2), and the translator keeps that
    order. Deferred statements are already expanded at each exit by the lowering (D7.8,
    `fir.md` 9.5).

11. **SSA discipline** (D19.4). Because every user-visible value lives in an `alloca`, a
    temporary `%tN` is used only in the block that defines it or in a block that block dominates
    (its own check's continuation and failure blocks). The emitter never carries a value across
    a merge point and never builds a `phi`; the optimizer does. This is what satisfies the
    verifier's dominance rule without any analysis in the compiler.

12. **Casts** (D3.14). `trunc`, `zext` and `sext` for integer to integer, widening by the
    source's signedness; `zext i1` for `bool` to integer; `ptrtoint` and `inttoptr` for pointer
    to and from `u64`; nothing at all for pointer to pointer and for casts that only drop
    mutability or ownership (D5.4, D17.4); `sitofp` and `uitofp` for integer to float; `fptrunc`
    and `fpext` for float to float; and the saturating intrinsics for float to integer, because
    plain `fptosi` is poison out of range while D3.14 requires truncation toward zero,
    saturation at the target's range, 0 for NaN, and no trap:

    ```llvm
      %t3 = call i32 @llvm.fptosi.sat.i32.f64(double %t2)
    ```

13. **Stack probing** (D10.8). Both targets write `"probe-stack"="inline-asm"` on every fort
    definition.
    The backend establishes a frame larger than a page one page at a time.
    The attribute stays in `-S` IR, so the driver line cannot remove the guarantee.

14. **Checks and failure blocks** (D19.6). Every runtime check computes one `i1` that is true on
    failure and branches with the failure label first:

    ```llvm
      br i1 %t4, label %L1, label %L0
    ```

    `assert` is the one exception: its operand is already the success condition, so it branches
    to the continuation first (D12.2), and the `default` D7.7 gives an enum `switch` is reached
    by that switch's default edge rather than by a branch of its own. The continuation label is
    allocated before the failure label, and failure blocks are emitted after every normal block
    of the function, in ascending label order (D19.5), so the one a `switch` makes is written
    before the bodies of its clauses, whose own checks take larger labels. Each failure block
    holds exactly one call to the section 5.1 entry point, with the check's values,
    `ptr @.file.N` and the `i32` line and column of section 4's position rule, followed by
    `unreachable`; nothing else, because the callee aborts (D11.4). A failure block is the
    emitter's own code and not a call the program wrote, so the call-site trap of D8.5 and D19.7
    does not stand in it; a program that calls an entry point of section 5.1 itself gets that
    trap like any other call to a `noreturn` function (item 20).
    Every `noreturn` entry point of section 5.1 carries
    `#8 = { cold noreturn nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }`.
    The entry points are the `std.rt.fail_*`
    family, `std.rt.panic` and `std.rt.assert_fail`, which the failure blocks call, and
    `std.rt.exit`, which only `std.rt` reaches. `noreturn` is truthful,
    since each is `fn noreturn` in fort and aborts (D8.5), and `cold` lays the block out of line,
    which on an exit path is a layout hint and nothing more. `#8` is `#1` of item 20 plus `cold`,
    since these are fort definitions and carry what every fort definition carries (item 7); the
    emitter gives it to the definitions of the names it knows (section 5.1) and to no other fort
    function. It is a new index rather than the `#2` that used to hold `{ cold noreturn nounwind }`
    on the runtime's C declarations, because those two sets of attributes are not the same and one
    index cannot mean both. No attribute is put on a failure call site.

15. **Integer checks** (D11.1, D11.3). In checked mode one intrinsic per operation, at the
    operand's width:

    ```llvm
      %t2 = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %t0, i32 %t1)
      %t3 = extractvalue { i32, i1 } %t2, 0
      %t4 = extractvalue { i32, i1 } %t2, 1
      br i1 %t4, label %L1, label %L0
    ```

    `llvm.sadd`, `llvm.ssub` and `llvm.smul` `.with.overflow.iN` for signed `+ - *`, the
    `llvm.uadd`, `llvm.usub` and `llvm.umul` family for unsigned; unary `-`, `++`, `--` and the
    compound assignments use the same intrinsics (negation is `llvm.ssub.with.overflow.iN(0,
    x)`). Release mode and the wrapping operators `+% -% *%` emit plain `add`, `sub` and `mul`.
    `nsw`, `nuw` and `exact` are never emitted (D16): release mode's wrapping is defined
    behavior, and in checked mode the check has already proved the absence of overflow. No
    float instruction carries a fast-math flag, since floats are IEEE 754 (D6.12, D16).

    A shift count is materialized at 64 bits (`sext` for a signed count type, `zext` for an
    unsigned one) so that a negative count is reported with its signed value, then
    `icmp uge i64 %cnt, <width>` branches to a `std.rt.fail_shift(i64 %cnt, ptr @.str.T, ...)`
    block, where `@.str.T` is the shifted operand's type name; release mode replaces the check
    with `and i64 %cnt, <width - 1>` (D11.1). The count is then truncated to the operand's type
    and the shift is `shl`, `ashr` for a signed operand or `lshr` for an unsigned one (D6.2), so
    a shift never produces poison. Division and remainder, in both modes (D6.13): `icmp eq %d,
    0` branches to `std.rt.fail_div_zero`; for a signed type the conjunction of
    `icmp eq %d, -1` and `icmp eq %n, <MIN>` branches to `std.rt.fail_div_overflow`; then
    `sdiv`, `srem`, `udiv` or `urem`.

16. **Bounds checks** (D6.8, D6.9). The index is sign-extended (signed) or zero-extended
    (unsigned) to `i64`, then one `icmp uge i64 %idx, %len` branches to
    `std.rt.fail_bounds(i64 %idx, i64 %len, ...)`, so a negative index fails the same compare.
    Element addressing is `getelementptr inbounds` (item 3):

    ```llvm
      %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0
      %t5 = getelementptr inbounds i32, ptr %t4, i64 %t3
    ```

    A span expression checks both bounds with one branch (`icmp ugt i64 %hi, %len`,
    `icmp ugt i64 %lo, %hi`, `or i1`) into `std.rt.fail_span(i64 %lo, i64 %hi, i64 %len, ...)`. A
    fixed array's length is an `i64` literal. `--no-bounds-check` removes exactly these branches and
    keeps the `inbounds`, which is what makes it unsafe (D10.6).

17. **`new` and `del`** (D10.2, D10.3, D17.9).

    ```llvm
      %t0 = call ptr @"std.rt.alloc"(i64 4, i64 1, ptr @.file.0, i32 7, i32 13)
    ```

    `new(T, n)` materializes `n` as `i64` first and, when its fort type is signed, branches on
    `icmp slt i64 %n, 0` to `std.rt.fail_alloc_count(i64 %n, ...)`; it then calls
    `std.rt.alloc(sizeof(T), %n, loc)` and writes the header field by field into the destination
    place (`getelementptr inbounds %fort.span, ptr %d, i32 0, i32 0` for `ptr`, `i32 0, i32 1`
    for `len`). `del(x)` loads the pointer (field 0 for a span or `string`), calls `std.rt.free`
    and, on an lvalue operand, zeroes the place: `store ptr null` for a pointer, a 16-byte
    `llvm.memset` for a span or `string`. On an rvalue nothing is stored.

18. **Ownership** (D17). `own` is erased: same types, same ABI, same normalization, and neither
    the module nor the runtime carries ownership information. `move(lv)` reads the operand's
    value into an intermediate of the emitter's own -- the register a `load` names for a pointer
    or a `void*`, a `%tmpK` slot an `llvm.memcpy` fills for a span, a `string` or an owning
    aggregate -- zeroes the operand (`store ptr null` or `llvm.memset`) and only then copies the
    value on to the destination, in both build modes (D17.6). The intermediate is not optional:
    the destination may be the operand itself (`s = move(s)`, `*p = move(*q)`,
    `v[i] = move(v[j])`), and copying to the destination first would let the zeroing destroy the
    value the move is meant to yield. The overwrite check (D17.11), in checked mode only, runs
    after the right-hand side is evaluated and immediately before the store:

    ```llvm
      %t7 = getelementptr inbounds %fort.span, ptr %v.2, i32 0, i32 0
      %t8 = load ptr, ptr %t7, align 8
      %t9 = icmp ne ptr %t8, null
      br i1 %t9, label %L5, label %L4
    ```

    with `std.rt.fail_overwrite(ptr @.file.N, i32 line, i32 col)` at the `=` token. A span or
    `string` target is produced into a compiler temporary first, an aggregate being written into
    a place rather than held in a register (D19.3), and the header is copied over after the
    check; a pointer target's value is already in a register, so the store follows the check
    directly. Release mode emits the plain store with no temporary; `--no-bounds-check` does not
    affect the check; `move`, `del` and assignments of owning aggregates never emit it. A
    declaration of an owning local does emit it, at the declared name, and gen_function stores
    the zero value into that local's slot once in the entry block so that the first execution
    reads zero (D17.11 as amended, D19.4). The check's own load cannot be optimized away, since
    it reads the location a later store writes. `return x` of an `own` local or parameter is the
    implicit move of D17.5: the value is read, the operand is then zeroed the same way, and only
    then does the function return, so a `defer del(x)` above it sees the zero value (D7.8).

19. **Builtins** (D12.2). The print family evaluates `fd` once (`1`, `2`, or the first argument)
    and then each argument left to right, one call per argument (D11.5): `i8 i16 i32 i64`
    sign-extended to `i64` to `std.rt.print_i64`; `u8 u16 u32 u64` zero-extended to `i64` to
    `print_u64`; `f32` and `f64` to `std.rt.print_f32` and `print_f64` (D18.1); `bool`
    passed as it stands to `print_bool`, whose parameter is a fort `bool`; `char` to
    `print_char`; an enum as
    `(i32 %v, ptr @.enum.<path.name>, i64 <count>)` to `print_enum`; a pointer, `void*` or
    function pointer to `print_ptr`; a `string` as its `ptr` and `len`
    fields, or as `(ptr @.str.N, i64 <len>)` for a literal, to `print_str`. Each unqualified name
    here is a function of `std.rt` (section 5.1). `println` and its
    relatives end with `std.rt.print_char(i32 %fd, i8 zeroext 10)`. `assert(cond)` branches to
    a block that calls `std.rt.assert_fail(ptr @.str.N, ptr @.file.N, i32 line, i32 col)` and
    is followed by `unreachable`, in both build modes, where `@.str.N` is the verbatim source
    text of the argument; `panic(msg)` calls `std.rt.panic(ptr, i64, ptr, i32, i32)` and is
    followed by `unreachable` (D11.4).

20. **`noreturn`** (D8.5, D19.7). A `noreturn` fort function is
    `define dso_local void @"m.f"(...) #1` with
    `#1 = { noreturn nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }`.
    The block that would fall off the end of its body ends
    with

    ```llvm
      call void @llvm.trap()
      unreachable
    ```

    as does every call site of such a function (D8.5 requires the trap in both places).
    `llvm.trap` is the trap instruction D8.5 asks for, and `unreachable` alone would not be one,
    since LLVM may let control fall through it. Linux x86-64 raises SIGILL with no message.
    Mac arm64 raises SIGTRAP with no message (D11.4).

    `noreturn` is emitted on a fort definition, as `#1` above and as the `#8` of item 14 on the
    runtime's, and never on a declaration of a C function the program wrote with `extern fn`,
    whatever its fort return type. The reason is that the optimizer deletes the trap after a call
    to a function it is told never returns: for a fort definition that is harmless, because the
    trap at the end of the body survives, but an `extern` that returns anyway must still hit a trap
    at the call site (`memory-model.md` 6), which only an unadorned declaration preserves.

21. **Enum tables** (D3.9, D12.2).

    ```llvm
    @.enum.main.color = private unnamed_addr constant [3 x %fort.enum_member]
        [%fort.enum_member { i32 0, ptr @.str.3 },
         %fort.enum_member { i32 5, ptr @.str.4 },
         %fort.enum_member { i32 6, ptr @.str.5 }], align 8
    ```

    The table is one line in the module and is wrapped here only to fit the page, as the
    `llvm.memcpy` declaration of item 8 is. One entry per member in declaration order;
    `%fort.enum_member = type { i32, ptr }` has C's 16-byte layout with its 4 bytes of padding, so
    it matches `std.rt`'s `struct enum_member` (section 5.1), which is emitted under this name
    and not as a `%struct.` of its own (item 2). A table is emitted only for an enum
    some `print` of that type reaches.

22. **`main`** (D11.6, D8.6). The compiler emits it in the entry module, and it is the only
    unmangled definition there (D9.7). `@main` is the C entry point the start-up code calls, so it
    takes C's `argc` and `argv` and returns C's `int`; it is a definition of this module like any
    other and carries `dso_local` and `#0` (item 4, item 7). It calls the entry module's `main`
    directly. When that `main` declares the parameter, `@main` passes `ptr %args`, the span it
    built in its own frame. That span is the caller-made copy of item 7, so the callee uses it in
    place and no second copy exists (item 10).
    The following Linux ABI fragments show startup and source-entry calls.
    The call of `std.rt.shutdown` is the runtime cleanup of section 2.1. It flushes and then
    releases the argument headers, before the final obligation boundary and `ret`.

    ```llvm
      %args = alloca %fort.span, align 8
      call void @"std.rt.args_init"(i32 %argc, ptr %argv)
      call void @"std.rt.args"(ptr sret(%fort.span) %args)
      %t0 = call i32 @"main.main"(ptr %args)
      call void @"std.rt.shutdown"()
    ```

    When the entry module's `main` takes no parameter, the call passes no argument and the rest
    follows the same rule:

    ```llvm
      %args = alloca %fort.span, align 8
      call void @"std.rt.args_init"(i32 %argc, ptr %argv)
      call void @"std.rt.args"(ptr sret(%fort.span) %args)
      %t0 = call i32 @"main.main"()
      call void @"std.rt.shutdown"()
    ```

    `std.rt.args` returns an aggregate, so it takes the destination as the hidden result pointer
    of item 7, written `sret(%fort.span)` on its own definition.
    The call writes `call void @"std.rt.args"(ptr sret(%fort.span) %args)` on both targets
    (D9.9).
    `args_init` runs first, since `args` lends what it built.
    The generated definition retains the C signature i32 @main(i32 %argc, ptr %argv).
    After runtime argument cleanup and the final obligation boundary, it emits:

    ```llvm
      %t1 = and i32 %t0, 255
      ret i32 %t1
    ```

    The and implements status & 0xFF (D11.6). Cleanup must precede this return (D17.19).
    A complete emitted main must include the required cleanup between the fragments.

23. **`-S` and `-c`** (D14.1). `-S` writes the complete module and stops.
    The module follows this contract, including final entry cleanup (item 22).
    `-c` writes it into the temporary directory and runs `--cc -c` over it (section 2).
    The compiler never writes a `.s` file.
    A user can run llc on the -S output to read the machine code.

The attribute groups have fixed indices, and only the used ones are emitted, so gaps in the
numbering are normal (D19.5):

- `#0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }` on every fort definition
  on both targets (item 7), and `#1`, the same set plus `noreturn`, on a `noreturn`
  definition (item 20).
- `#2` is not emitted: it held `{ cold noreturn nounwind }` on the runtime's `_Noreturn` C
  declarations, which item 8 no longer produces. The hand-written modules of 6.1 and 6.2 declare
  what they call and number their own groups (preamble), which is why one of them still shows it.
- `#3 = { nobuiltin }` on every extern call site (item 8).
- `#4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }` on the
  overflow intrinsics (item 15) and on `llvm.fptosi.sat` and `llvm.fptoui.sat` (item 12).
- `#5 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }` on `llvm.memcpy`
  and `#6 = { nocallback nofree nounwind willreturn memory(argmem: write) }` on `llvm.memset`.
- `#7 = { cold noreturn nounwind memory(inaccessiblemem: write) }` on `llvm.trap` (item 20).
- `#8 = { cold noreturn nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }`, `#1` plus
  `cold`, on the definitions of the `noreturn` entry points of section 5.1 (item 14).

`mustprogress` is deliberately absent everywhere, from `#4`, `#5` and `#6`, where clang would
print it, and from fort definitions: it licenses the optimizer to delete a loop with no side
effects, and a fort `while (true) { }` must keep running (D8.4 counts it as terminating, and
D14.2 emits no warning about what follows it).

### 6.1 A program without checks

```fort
fn main() i32 { println("hello, world!"); return 0; }
```

The following standalone Linux module illustrates `main.ft`.
Its argument stubs create no owner, including when argc is positive.
Its `shutdown` stub only returns, because no stub allocates. Production `shutdown` follows item
22 (D17.19).

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local void @"std.rt.print_str"(i32 %fd, ptr %ptr, i64 %len) #0 {
entry:
  %t0 = call i64 @write(i32 %fd, ptr %ptr, i64 %len) #3
  ret void
}

define dso_local void @"std.rt.print_char"(i32 %fd, i8 zeroext %c) #0 {
entry:
  %byte.0 = alloca i8, align 1
  store i8 %c, ptr %byte.0, align 1
  %t0 = call i64 @write(i32 %fd, ptr %byte.0, i64 1) #3
  ret void
}

define dso_local void @"std.rt.args_init"(i32 %argc, ptr %argv) #0 {
entry:
  ret void
}

define dso_local void @"std.rt.args"(ptr sret(%fort.span) %ret.sret) #0 {
entry:
  %t0 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 0
  store ptr null, ptr %t0, align 8
  %t1 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 1
  store i64 0, ptr %t1, align 8
  ret void
}

define dso_local void @"std.rt.shutdown"() #0 {
entry:
  ret void
}

define dso_local i32 @"main.main"() #0 {
entry:
  call void @"std.rt.print_str"(i32 1, ptr @.str.0, i64 13)
  call void @"std.rt.print_char"(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
entry:
  %args = alloca %fort.span, align 8
  call void @"std.rt.args_init"(i32 %argc, ptr %argv)
  call void @"std.rt.args"(ptr sret(%fort.span) %args)
  %t0 = call i32 @"main.main"()
  call void @"std.rt.shutdown"()
  %t1 = and i32 %t0, 255
  ret i32 %t1
}

@.str.0 = private unnamed_addr constant [14 x i8] c"hello, world!\00", align 1

declare i64 @write(i32, ptr, i64) nobuiltin

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #3 = { nobuiltin }
```

The five runtime definitions let this module link on its own through the C library's write.
The source function, quoted names, private data, and attributes illustrate items 4, 5, 7, and 19.
A production module includes the complete runtime closure (D9.10, D13.1).
Its args_init creates header storage for positive argc even when source main takes no arguments.
After source main returns, the production entry calls `std.rt.shutdown`. It flushes and releases
that storage before the final normal-exit boundary and the masked return (D11.6, D17.19).
The empty stubs above model neither that allocation nor its release.
The illustrated entry is not the complete production entry of item 22.
Section 5.1 fixes the cleanup entry point `std.rt.shutdown`; this example adds no ABI
signature.

### 6.2 A program with a check

```fort
fn main() i32 {
    println("before");
    i32[3] a = {};
    i64 mut i = 5;
    return a[i];
}
```

The following standalone Linux module illustrates `abort.ft` with the fault position at 12:13.
Its module path is abort (D9.1). Its source main symbol is abort.main (D9.7).
It uses the same empty argument and `shutdown` stubs as section 6.1. They create and release no
runtime argument owner (item 22, D17.19).

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local void @"std.rt.print_str"(i32 %fd, ptr %ptr, i64 %len) #0 {
entry:
  %t0 = call i64 @write(i32 %fd, ptr %ptr, i64 %len) #3
  ret void
}

define dso_local void @"std.rt.print_char"(i32 %fd, i8 zeroext %c) #0 {
entry:
  %byte.0 = alloca i8, align 1
  store i8 %c, ptr %byte.0, align 1
  %t0 = call i64 @write(i32 %fd, ptr %byte.0, i64 1) #3
  ret void
}

define dso_local void @"std.rt.args_init"(i32 %argc, ptr %argv) #0 {
entry:
  ret void
}

define dso_local void @"std.rt.args"(ptr sret(%fort.span) %ret.sret) #0 {
entry:
  %t0 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 0
  store ptr null, ptr %t0, align 8
  %t1 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 1
  store i64 0, ptr %t1, align 8
  ret void
}

define dso_local void @"std.rt.shutdown"() #0 {
entry:
  ret void
}

define dso_local void @"std.rt.fail_bounds"(i64 %i, i64 %n, ptr %f, i32 %l, i32 %c) #8 {
entry:
  %t0 = call i64 @write(i32 2, ptr @.str.1, i64 65) #3
  call void @abort() #3
  call void @llvm.trap()
  unreachable
}

define dso_local i32 @"abort.main"() #0 {
entry:
  %a.0 = alloca [3 x i32], align 4
  %i.1 = alloca i64, align 8
  call void @"std.rt.print_str"(i32 1, ptr @.str.0, i64 6)
  call void @"std.rt.print_char"(i32 1, i8 zeroext 10)
  call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)
  store i64 5, ptr %i.1, align 8
  %t0 = load i64, ptr %i.1, align 8
  %t1 = icmp uge i64 %t0, 3
  br i1 %t1, label %L1, label %L0

L0:
  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0
  %t3 = load i32, ptr %t2, align 4
  ret i32 %t3

L1:
  call void @"std.rt.fail_bounds"(i64 %t0, i64 3, ptr @.file.0, i32 12, i32 13)
  unreachable
}

define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
entry:
  %args = alloca %fort.span, align 8
  call void @"std.rt.args_init"(i32 %argc, ptr %argv)
  call void @"std.rt.args"(ptr sret(%fort.span) %args)
  %t0 = call i32 @"abort.main"()
  call void @"std.rt.shutdown"()
  %t1 = and i32 %t0, 255
  ret i32 %t1
}

@.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
@.str.0 = private unnamed_addr constant [7 x i8] c"before\00", align 1
@.str.1 = private unnamed_addr constant [66 x i8]
    c"abort.ft:12:13: runtime error: index 5 out of range for length 3\0A\00", align 1

declare void @abort() nobuiltin
declare i64 @write(i32, ptr, i64) nobuiltin

declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6
declare void @llvm.trap() #7

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #3 = { nobuiltin }
attributes #6 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #7 = { cold noreturn nounwind memory(inaccessiblemem: write) }
attributes #8 = { cold noreturn nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
```

The `@.str.1` constant is one line in the module and is wrapped here only to fit the page, as the
`llvm.memcpy` declaration of item 8 is. The locals are entry-block allocas, the array is zeroed
with `llvm.memset`, the bounds check of item 16 branches to a failure block at the end of the
function, and `%fort.span` and `%fort.enum_member` are emitted although nothing but `main` uses
them (item 2). The definition of `std.rt.fail_bounds` carries the `#8` of item 14 and ends with
the `llvm.trap` of item 20 after its call to a C function declared `noreturn` nowhere. The
program prints `before`, then `abort.ft:12:13: runtime error: index 5 out of range for length 3`,
and dies with SIGABRT (D11.4).
That abort path requires no cleanup (D17.19).
A production normal return calls `std.rt.shutdown`, which releases the argument storage after the
flush and before the final boundary and return. The `shutdown` stub here releases nothing, because
its stubs allocate no owner.

## 7. Testing

### 7.1 Layout (D14.4)

```sh
test/
  test.h                       C unit-test macros
  common.h                     TEST_UNUSED and shared helpers, provided by the implementation
  <component>_test.c           one C suite per compiler component (lexer_test.c, ...)
  lang/
    run_tests.py               runs every language test below
    xfail.txt                  tests the compiler cannot pass yet
    run/<area>/NNN_name.ft     compile, run, compare
    fail/<area>/NNN_name.ft    must not compile, with annotated errors
    fail/<area>/NNN_name.stderr the golden: the compiler's whole stderr for that test
    run/modules/<name>/main.ft multi-file run test; the directory is the root
    fail/modules/<name>/main.ft multi-file fail test, same rule
    fail/modules/<name>/expected.stderr the golden of that multi-file fail test
    ffi/*.c                    C helpers for `link:` directives
    programs/*.ft              larger programs, treated as run tests
```

`<area>` is one of `lexical constants operators casts mutability declarations control switch
defer functions structs enums arrays spans strings pointers globals builtins errors modes modules
ffi stdlib ownership`: `errors` holds the `abort` tests of the runtime checks (the overwrite
check of D17.11 included, with its `--release` twin under `modes`), `modes` the `--release`
tests, `stdlib` the tests of the standard library once it exists, and `ownership` the run tests
of `own`, `move`, lending and `del` and the fail tests of every rule of D17. `NNN` is a three-digit
sequence number and `name` a short snake-case description. Paths in `link:` are relative to
`test/lang/`.

### 7.2 Directives (D14.5)

All directives are `//!` lines at the top of the file, except `error`, which annotates a line:

| Directive                       | Meaning                                                  |
|---------------------------------|----------------------------------------------------------|
| `//! run` or `//! fail`         | required, first line                                     |
| `//! flags: --release`          | extra compiler flags                                     |
| `//! args: a b c`               | program arguments, split on spaces                       |
| `//! link: ffi/helpers.c`       | C helper to link, relative to `test/lang`; repeatable    |
| `//! stdin:` then `//< ` lines  | standard input, one line per `//< ` line                 |
| `//! stdout:` then `//| ` lines | expected stdout, compared exactly, trailing spaces included |
| `//! exit: N`                   | expected exit status, default 0                          |
| `//! abort`                     | expect termination by SIGABRT                            |
| `//! signal: NAME`              | expect termination by that signal                        |
| `//! stderr: <substring>`       | `<substring>` must appear in stderr; repeatable          |
| `//! stdout-<os>:` then `//| ` lines | expected stdout when the harness runs for `<os>`      |
| `//! exit-<os>: N`              | expected exit status when the harness runs for `<os>`    |
| `//! abort-<os>`                | expect SIGABRT when the harness runs for `<os>`          |
| `//! signal-<os>: NAME`         | expect that signal when the harness runs for `<os>`      |
| `//! stderr-<os>: <substring>`  | in stderr when the harness runs for `<os>`; repeatable   |
| `//! error: <substring>`        | `fail` tests only, at the end of the offending line      |
| `//! error-any: <substring>`    | `fail` tests only, at the top                            |

The expected output is each `//| ` line's text after the marker followed by a newline; a bare
`//|` is an empty line. Output without a final newline cannot be expressed, so tests end their
output with `println`. Every `stderr:` substring must appear in stderr.

`<os>` is a `$cfg(target_os)` value, `linux` or `macos`. A target form states the expectation
of one OS where the two targets differ: a trap is SIGILL on x86-64 and SIGTRAP on arm64, and an
import `$cfg(target_os)` selects prints another value. The plain form stays the expectation of
every other OS, so a reader sees both in the header. A target form without its plain form is a
lint error; for `exit-<os>`, `abort-<os>` and `signal-<os>` any plain outcome serves. The rules
of a plain directive hold for its target form: it appears at most once (`stderr-<os>` repeats),
and the three outcome forms of one OS are mutually exclusive. `stderr-<os>` replaces every
plain `stderr:`, and an outcome form replaces the plain outcome whole.

In a `fail` test every `//! error:` line must produce a diagnostic on that line containing the
substring, and no unannotated diagnostic may occur; `//! error-any:` requires some diagnostic to
contain the substring and is for errors without a useful line, such as circular imports.

Every `fail` test also has a golden, which is not a directive (D14.5): `NNN_name.stderr` beside
`NNN_name.ft`, or `expected.stderr` in the directory of a directory test. The golden holds the
compiler's whole stderr for the test, the rendered lines of section 4 included, and the harness
compares it byte for byte after two normalizations, because both directories differ between
machines. The `--std-dir` prefix of a path becomes `<std>`. The absolute path of the corpus root
becomes `<root>`: a test path is relative, because the compiler runs in `test/lang` (D14.4), but
a module's identity is its real path, so a message about two names for one file quotes the root
in full. The annotations stay: an `error:` says at the line what the test is about, and the
golden pins the rendering.

### 7.3 What the harness does

`test/lang/run_tests.py [options] [filter...]` (Python 3, standard library only) runs the
compiler named by `--fort` (default `$FORT`, else `build/<Host>/debug/fort`, `<Host>` being `Linux`
or `Darwin`) with `--std-dir` from `--std-dir` (default `$FORT_STD_DIR`, else `std` beside the
compiler) and `--cc` from `--cc` (default `clang`, which must be a clang as `--cc` is, D14.1).
`--target` (default `x86_64-linux-gnu`) is the triple the harness passes to `--cc` as
`--target=<triple>` when it links a test's C helpers itself. `--verify-ir` runs `fort -S -o prog.ll
<test>` for every test whose compilation succeeds and verifies the module with `<opt> -passes=verify
-disable-output` (D19.1); `--opt` names that program (default `opt-18`). A module the verifier
rejects is a FAIL; an `opt` that cannot be launched, times out or dies by a signal is an ERROR, like
a compiler exit 2 (D14.1). `--runner` names a command that runs the programs when binfmt does not,
`-j` the number of parallel tests, `--timeout` the seconds per step, `-v` prints the commands and
outputs of failures and `--keep` keeps the temporary directories. The
compiler runs with `test/lang` as its working directory (D14.4). Each test uses a fresh
temporary directory as `TMPDIR`, sets `LC_ALL=C`, and disables core dumps.
The `--target` triple names the target OS: `arm64-apple-macosxM.m.p` with three ASCII numeric
parts is `macos`, and every other string is `linux`. The harness applies the `-<os>` directives
of that OS (7.2) and sets `QEMU_LD_PREFIX` for `linux` unless it inherits a value; for `macos`
it removes the variable:

| Directive    | Harness action                                                              |
|--------------|-----------------------------------------------------------------------------|
| `run`        | `fort <flags> -o prog <test>` must exit 0; run `prog`; compare its output    |
| `fail`       | `fort <flags> -o prog <test>` must exit 1 with only annotated errors        |
| (golden)     | the compiler's stderr of a `fail` test must equal its golden (7.2)          |
| `flags:`     | appended to the `fort` command line                                         |
| `args:`      | appended to the program's command line                                      |
| `link:`      | `fort -c`, then `cc -o prog prog.o <helpers>`                               |
| `stdin:`     | the `//< ` lines, each with a newline, are the program's stdin; else empty   |
| `stdout:`    | the program's stdout must equal the `//| ` lines; with no directive, empty   |
| `exit:`      | the program's exit status must equal `N`                                    |
| `abort`      | the program must die with SIGABRT (status 134 from the shell)               |
| `signal:`    | the program must die with `SIG<NAME>`, one of the six names below           |
| `stderr:`    | each substring must occur in the program's (`run`) or compiler's (`fail`) stderr |
| `error:`     | an `error:` line with that file and line must contain the substring         |
| `error-any:` | some `error:` line must contain the substring                               |

`<test>` is the test file, or `main.ft` in a multi-file test; `-o` names a file in the temporary
directory even for a `fail` test, so a compiler that wrongly succeeds never writes `a.out` into
`test/lang`. `//! abort` is the spelling for SIGABRT and `//! signal:` covers the rest: its text is
a signal's POSIX name without the `SIG` prefix, one of `ABRT`, `BUS`, `FPE`, `ILL`, `SEGV` and
`TRAP` (`signal: ABRT` says what `abort` says). Any other name, and a number, is a lint error, since
the set is normative here rather than whatever the machine running the harness happens to define.
`exit:`, `abort` and `signal:` state one outcome between them and are mutually exclusive. Before the
`stderr:` substrings of a `run` test are looked for, the lines qemu-user adds when a signal kills
the program (`qemu: uncaught target signal 6 (Abort) - core dumped`) are dropped, since native
execution prints nothing there; that holds for every signal, so a `signal:` test compares the
program's own stderr and not qemu's note. For `error:` the harness also fails the test when the
compiler reports an `error:` for a line that carries no annotation; a diagnostic matched by an
`error-any:` counts as annotated, and further diagnostics on an annotated line are accepted. In
multi-file tests, directives are read from `main.ft`, `//! error:` annotations from every `.ft` file
in the directory (D14.4), and no `-I` is passed because the directory is the root (D9.2). A compiler
exit status other than 0 or 1 (2 is a usage, toolchain or internal error, D14.1), a compiler crash,
a compiler timeout, a failure of the harness's own `link:` step and a program that cannot be started
are `ERROR`, not a verdict about the test; a program that times out is a `FAIL`.

The harness takes the lines that start with a byte other than a space as the header lines of
section 4 and matches its patterns against those lines alone, so a rendered source line is never
read as a diagnostic. That holds because no path of the corpus starts with a space. A `fail`
test whose golden is missing is a `FAIL`, and a `FAIL` that names a golden names its first
differing line with both spellings. `--bless` writes the golden of each
selected `fail` test from the compiler's stderr, normalized as in 7.2, and then judges the test
against it. A compiler step that is an `ERROR` writes no golden. A reviewer reads the diff of the
goldens as part of the change. `--bless` does not combine with `--check-json`.

One expectation file beside the harness lists path prefixes of tests (relative to `test/lang`,
`#` comments allowed). `xfail.txt` names the tests the compiler cannot pass yet. A listed test
that fails or errors is `XFAIL`, a listed test that passes is `XPASS` and fails the run, so the
list shrinks in the commit that makes tests pass. `--xfail` names another list.
`--no-xfail` ignores the list.

The C compiler is frozen against general language work. It changes only for a specified C defect
or to keep the native C-to-bootstrap-1 edge working on a supported host. Product tests do not run
the C compiler (T-046, T-160).

The harness prints one `PASS`, `FAIL`, `XFAIL`, `XPASS` or `ERROR` line per test with the
reason where there is one, then a summary, and exits with 1 if any test is `FAIL`, `XPASS` or
`ERROR`; each `filter` selects the tests whose path contains it. `--list` prints the selected
tests and their count. `--lint` validates the corpus without a compiler and fails on: a first
line other than `//! run` or `//! fail` or one that does not match the directory; an unknown,
malformed, duplicated or empty directive; a `signal:` naming none of the six signals above;
two of `exit`, `abort` and `signal` together; a run-only directive in
a `fail` test or `error`/`error-any` in a `run` test; a `link:` file that does not exist; a
`//<` or `//|` not followed by a space or outside its block; a directive after the header or in
a sibling module; a `fail` test with neither `error:` nor `error-any:`; a golden with no test
beside it and a golden beside a `run` test; an unknown area, a badly named test, a stray file,
a directory test outside `modules`, and a gap or duplicate in the `NNN` numbering of an area;
and an expectation-list entry that matches no test. A run performs the same checks first and
stops when they fail, and fails when the filters select no test.

### 7.4 Examples

The examples are abbreviated; the seed corpus holds full versions in the areas named. A run
test under `test/lang/run/arrays/`:

```fort
//! run
//! stdout:
//| 3 2
fn main() i32 {
    i32[3] a = {1, 2, 3};
    println(a[2], " ", a[1]);
    return 0;
}
```

A fail test, `test/lang/fail/mutability/001_assign_immutable.ft`:

```fort
//! fail
fn main() i32 {
    i32 x = 1;
    x = 2;                       //! error: immutable
    return x;
}
```

An abort test with `stderr:` under `test/lang/run/errors/`; `before` is flushed by the failure
path (D11.4):

```fort
//! run
//! abort
//! stderr: runtime error: index 5 out of range for length 3
//! stdout:
//| before
fn main() i32 {
    i32 mut@ own xs = new(i32, 3);
    defer del(xs);
    println("before");
    xs[5] = 1;
    return 0;
}
```

An ownership fail test under `test/lang/fail/ownership/`, one annotated line per rule (D17.5,
D17.8, D17.9):

```fort
//! fail
struct node { i32 v; }

fn main() i32 {
    node mut* own a = new(node);
    node mut* own b = a;         //! error: move
    node mut* c = new(node);     //! error: would leak
    node* view = a;
    del(view);                   //! error: own
    del(a);
    return 0;
}
```

A multi-file test, `test/lang/run/modules/<name>/main.ft` with `util.ft` beside it:

```fort
//! run
//! stdout:
//| 7
import util;
import util.twice;

fn main() i32 {
    println(util.inc(twice(3)));
    return 0;
}
```

```fort
// util.ft
fn twice(i32 x) i32 {
    return x * 2;
}

fn inc(i32 x) i32 {
    return x + 1;
}
```

A release-mode test under `test/lang/run/modes/`, whose checked-mode twin uses `//! abort` and
`//! stderr: runtime error: integer overflow` instead:

```fort
//! run
//! flags: --release
//! stdout:
//| -2147483648
fn main() i32 {
    i32 mut x = 2147483647;
    x += 1;
    println(x);
    return 0;
}
```

An FFI test under `test/lang/run/ffi/`, with its helper in `test/lang/ffi/helpers.c`:

```fort
//! run
//! link: ffi/helpers.c
//! stdout:
//| 12
extern fn helper_add(i32 a, i32 b) i32;

fn main() i32 {
    println(helper_add(5, 7));
    return 0;
}
```

```c
int32_t helper_add(int32_t a, int32_t b) { return a + b; }
```

`args:` and `exit:` are exercised by a `main(string@ args)` that returns `cast(args.len, i32)`
under `//! args: one two` and `//! exit: 3`.

### 7.5 Compiler unit tests in C

Each `bootstrap0/test/<component>_test.c` is one suite built against the compiler's sources and
`test.h`: tests are defined with `TEST(name, body)`, registered with `TEST_RUN`, and the suite exits
through `TEST_EXIT` with status 0 (ok), 1 (a failed assertion) or 2 (a test error). The first
command-line argument, when present, is a name prefix selecting tests. `TEST_ASSERT_*` macros log
file, line, expression, actual and expected values and return `TEST_RESULT_FAIL`. A body is one
macro argument, so a comma outside parentheses inside it must be parenthesized.

```c
#include "test.h"
#include "lexer.h"

TEST(lexes_hex_literal, {
    lexer_t lx;
    lexer_init(&lx, "0x7F");
    token_t t = lexer_next(&lx);
    TEST_ASSERT_EQ_INT32(t.kind, TOK_INT);
    TEST_ASSERT_EQ_INT64(t.value, 127);
})

int main(int argc, char** argv) {
    TEST_INIT("lexer", argc, argv);
    TEST_RUN(lexes_hex_literal);
    TEST_EXIT();
}
```

The names `lexer_t`, `lexer_init`, `lexer_next`, `token_t` and `TOK_INT` are chosen in the
implementation phase (section 8). Unit tests cover what language tests cannot observe directly:
the token stream, the parser's speculative rewinds, constant folding at the edges of the
`[-2^63, 2^64 - 1]` range (D4.4), the mutability-drop check (D5.4) and option parsing.

### 7.6 Coverage target (D14.6)

The corpus aims at more than two lines of test for each line of source (`agents/lines.py`): `wc -l`
over `bootstrap0/test/*.c`, `bootstrap0/test/common/*.h`, `test/**/*.ft` and `test/lang/ffi/*.c`
against `wc -l` over `bootstrap0/src/*.c`, `bootstrap0/src/*.h`, `src/fort/*.ft` and `std/*.ft`, the
runtime among them (D13.1). The standard library is source and not test (D14.6): it is code the
project ships, and the tests that exercise it are `test/lang/run/stdlib`. The seed tests of the
design phase establish the format with one example per area; the full corpus is sized as follows,
in files:

| Area                    | run | fail |
|-------------------------|-----|------|
| lexer and literals      | 30  | 30   |
| operators and constants | 50  | 35   |
| casts                   | 25  | 20   |
| mutability              | 20  | 40   |
| control flow            | 45  | 25   |
| functions               | 30  | 25   |
| structs                 | 30  | 20   |
| enums                   | 20  | 15   |
| arrays                  | 30  | 15   |
| spans and `new`/`del`   | 35  | 20   |
| strings                 | 30  | 10   |
| pointers                | 25  | 20   |
| defer                   | 20  | 10   |
| modules                 | 30  | 30   |
| FFI                     | 20  | 10   |
| globals                 | 15  | 10   |
| builtins                | 30  | 10   |
| stdlib                  | 45  | 5    |
| ownership               | 15  | 20   |
| programs                | 20  | 0    |
| total                   | 565 | 370  |

Every operator, keyword and builtin appears in at least one run test and, where it can be
misused, one fail test (decisions.md checklist). Every runtime check has an `abort` test in
checked mode and, for the checks that release mode removes, a release-mode twin.

## 8. Compiler architecture sketch

This section is not normative. It records the intended shape so that the other sections are
implementable; the design is to be planned in the implementation phase.

- **Language and dependencies.** C11, POSIX, no external libraries; one binary `fort`. The
  repository holds `src/` (compiler), `std/*.ft` (standard library, the runtime `std.rt`
  included), `test/` (section 7) and a build script producing `build/fort` and `build/std/` with
  the library sources.
- **The chain that builds the compiler** (D14.7). CMake detects Linux or Darwin from the host
  operating system. It rejects cross compilation and all other systems. The C compiler builds
  `bootstrap-1` for that host. Each source pin builds the next pin. The last pin builds HEAD.
  `tools/bootstrap.ref`
  holds the ordered, gap-free list of full commit SHAs. CMake validates and owns this graph. A
  graph does not call a shell script to read the list or select a predecessor. A shallow clone that
  lacks
  a listed commit cannot bootstrap. `FORT_STAGE1_COMPILER` skips the list and builds HEAD directly.
  `FORT_ENABLE_BOOTSTRAP0=OFF` requires this external compiler. Add a pin only when the current last
  pin cannot build a required later revision. A new pin must build with its predecessor and build
  its successor. Both builds must pass on both supported host systems before the pin enters the
  list. The C unit suites test the C implementation. Product tests use the current compiler and
  current standard library. A successful native build proves the C-to-bootstrap-1 edge.
  External-stage1 mode registers no bootstrap tests. The project does not maintain C-to-fort parity.
- **Driver.** Parses options (section 1), owns the module table keyed by real path, runs the
  passes below, invokes `--cc` over the emitted module (D14.3) and maps failures to exit
  statuses.
- **Lexer.** A complete token array per file (kind, position, literal value) applying D2; the
  array makes each speculative parse of `grammar.md` section 7 a saved and restored index.
- **Parser.** Recursive descent over the token array, one AST per module, with the nesting
  limit of D2.11; imports go back to the driver, which loads modules until the closure is
  complete and acyclic.
- **Checker.** Two-phase top-level resolution per D7.10: phase one enters every module-level
  name; phase two resolves types, signatures, constant values and struct sizes lazily with cycle
  detection, so declaration order never matters; then each body is checked against D3 to D8 and
  D17 and the AST is annotated with types, constant values, lvalue mutability, resolved symbols
  and, on each assignment, whether the target is an `own` lvalue that needs the overwrite check.
- **Codegen.** For each function, the lowering makes FIR from the annotated AST and expands the
  deferred statements statically at each exit (D7.8, D19.8); the verifier tests the FIR, the
  build-mode pass applies the selected mode, and the verifier tests it again; the translator then
  appends the text of section 6 to a single LLVM IR module (D19.1), with no libLLVM and no
  register allocation: locals are `alloca`s in the entry block and intermediates are SSA
  temporaries or `%tmp` slots (`fir.md` 3, 11, 12). A function that the lowering does not
  support is a compile error (`fir.md` 9.8).
- **Memory.** Arenas per compilation; nothing is freed before exit.

## 9. Editor support

An editor asks the compiler two questions about a saved file: what is wrong with it, and what
does this name mean. Section 4.1 answers the first; the identifier index below answers the second.
Both are one batch run of `fort`, and the check mode is the compiler's whole editor interface: it
never grows a server (D20).

### 9.1 The identifier index (D20.3)

`fort --index entry.ft` runs the check mode and fills the document's `"symbols"` array with one
record per identifier occurrence the checker resolved, in every module of the closure that was
checked. `--index` implies `--check` and `--json`, so the option is the whole command line an
editor needs; without it `"symbols"` is the empty array.

```json
{"file": "main.ft", "line": 7, "col": 16, "end_line": 7, "end_col": 19,
 "name": "add", "kind": "fn", "type": "fn (i32, i32) i32", "is_decl": false,
 "decl": {"file": "mathx.ft", "line": 12, "col": 8, "end_line": 12, "end_col": 11}}
```

- The record's own range is the range of that one name token, never the construct's first token
  (D20.4): the range of `add` in `mathx.add(1, 2)` covers `add` alone, so an editor underlines the
  name the reader pointed at. Positions are the 1-based byte columns of section 4, the end
  exclusive; converting them to UTF-16 code units is the client's job, as it is for a diagnostic.
- `"name"` is the identifier as it is spelled at that occurrence, so an `as` alias reads as the
  alias and the declaration it binds reads as its own name (D9.3).
- `"kind"` is what the name denotes, spelled as a diagnostic spells it: `module`, `fn`,
  `extern fn`, `struct`, `enum`, `enum member`, `field`, `constant`, `global`, `local`, `parameter`
  or `builtin` (D7.9, D7.10, D3.9, D12.2).
- `"type"` is the declaration's type as a declaration spells it, the `mut` of level 0 included
  (D5.2, D5.3): `i32`, `i32 mut* own`, `fn (i32, i32) i32`. It is the empty string for a name that
  denotes no value type, which is a module, a struct name, an enum name and a builtin, and `null`
  when the declaration failed to check, which a client renders as unknown: the type it has is the
  poison of section 4 and says nothing a reader wants (D20.3).
- `"is_decl"` is true on the occurrence that declares the name in this file and false on every use
  of it, and `"decl"` is the range of the declaring name token: for a declaration, its own range.
  An `as` alias declares its name in the importing module (D9.3), so it is the one record with
  `"is_decl"` true whose `"decl"` lies elsewhere: going to the definition of `double` in
  `import util.twice as double;` lands on `twice`, while the alias is still the anchor a rename of
  `double` in this file starts from. An import without an alias introduces the name its declaration
  already has, so its occurrence is a use.
- `"decl"` is `null` for a builtin, which no source declares (D12.2), and the empty range at 1:1 of
  the module's file for a module, which a file declares and which has no name token (D9.1), so
  jumping to the definition of `mathx` in `mathx.add(1, 2)` opens `mathx.ft` at the top rather than
  reading as a builtin's nothing to jump to. No record is therefore a module's declaration, and a
  module has no rename anchor.
- Records are ordered by file, an imported module before its importers (D9.10), and within a file
  by the start of the occurrence, so a client may bisect the records of a file by position.
- A name the checker could not resolve carries no record, and a file that did not parse
  contributes none, so a file with errors still indexes everything that resolved. A construct with
  no name token of its own carries none either, and neither does a segment of an import path
  before its last: those name search directories, not modules (D9.2, D9.3). The pseudo-fields
  `.len` and `.ptr` carry none: they are a property of the type and not a declaration of any
  module (D3.4, D3.5, D3.7).
- Every record repeats the type and the declaration range of the name it resolves, so a client
  answers hover and go-to-definition from the record under the cursor alone.
- A conditional import contributes records only from its selected branch. An inactive import adds
  no module record, binding record, declaration record, or use record. Both branches remain in the
  syntax tree (D21.3).

### 9.2 What an editor does with it

The index is a batch answer about the file as it was saved, which is what an editor built on
`--index` can promise: it publishes the diagnostics of `"files"`, clearing the files that no longer
have any, and serves hover and definition from the records of the last run whose closure contained
the file. `"files"` is the set a client may clear, not the set it publishes: every entry of
`"diagnostics"` is published whether or not its file is listed, since an error about a file
the compiler never read names a file that `"files"` cannot hold (section 4.1, D20.2) -- with the
one exception that a client which cannot open a file may drop that file's diagnostics, there being
nothing to show them against. Between two saves the answers are stale, and a client says so rather
than guessing. The VS Code extension in `editors/vscode` is the client this repository ships, and
it takes the smaller half of this: `--check --json` and the diagnostics alone, with no index, no
hover and no definition (T-089). It is also the instance of that exception: it runs the compiler in
the development VM and shows the answers on the host, so a diagnostic whose file lies outside the
workspace folder -- the standard library, read from the compiler's own directory in the guest --
names a file the editor cannot open, and it is dropped rather than published against a path that
resolves to nothing. `editors/README.md` is its install guide and its list of limitations.

## 10. Not in v1

Deferred by D15 and the design reviews: debugger support (no DWARF, no `!dbg` metadata; the
frame pointer of section 6 item 7 and the symbol names are what a debugger gets), an optimizer
of the compiler's own and `-O` options on `fort`'s command line (`--cc` optimizes the module at
`-O1`, or `-O2` under `--release`, D14.3), building the module through the LLVM C API in process
(D15), warnings, separate compilation and incremental builds, a package manager, documentation
generation, cross-target objects and links, any target beyond Linux x86-64 and Mac arm64,
`--help` text beyond the
usage line, and conditional compilation. The idioms that replace the deferred language features
are listed with each item in D15.
