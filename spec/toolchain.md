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
| `--cc <path>`       | the clang that compiles and links the IR (D14.3)           | `clang`    |
| `--target <triple>` | passed to `--cc` as `--target=<triple>` (D14.1)            | see below  |
| `-Xcc <arg>`        | passed to `--cc` verbatim, after the arguments below       | none       |
| `--check`           | run the front end only and stop (D20.1)                    | off        |
| `--json`            | write the check document to stdout (D20.2), needs `--check`| off        |
| `--index`           | fill the document's identifier index (D20.3)               | off        |
| `--tokens`          | write the entry file's tokens to stdout and stop (D14.1)   | off        |
| `--ast`             | write the entry file's tree to stdout and stop (D14.1)     | off        |
| `--help`            | print the usage line and exit 0                            |            |
| `--version`         | print the compiler version and exit 0                      |            |

- `-o`, `-I`, `--std-dir`, `--cc`, `--target` and `-Xcc` take the following argument; `-l<lib>`
  is one argument. `-I` roots are searched in command-line order (D9.2) and `-Xcc` arguments are
  passed in command-line order. The last `-o`, `--std-dir`, `--cc` and `--target` win.
- `--cc` must name a clang, since nothing else reads LLVM IR (D14.1, D19.1). The
  default target triple is `x86_64-linux-gnu` (D14.1).
- The default output is `a.out`; with `-c` it is `<entry>.o` and with `-S` `<entry>.ll` (D14.1),
  where `<entry>` is the entry file's base name without `.ft`, placed in the current directory as
  `cc` does.
- The default standard library directory is `$FORT_STD_DIR` when set, else `std` relative to
  the directory containing the `fort` binary.
- `-S` and `-c` together stop at the IR. With `-S`, `-l`, `--cc`, `--target` and `-Xcc` are
  unused.
- `--release` and `--no-bounds-check` are independent and may be combined.
- `--check` runs steps 1 to 3 of section 2 and stops there: no IR, no `--cc`, no temporary, and
  the entry module need not define `main`, since it is a module under inspection and not a
  program (D20.1, D8.6). With it, `-o`, `-S`, `-c`, `-l`, `--cc`, `--target` and `-Xcc` are
  unused. `--json` replaces the text diagnostics of section 4 with the document of section 4.1 on
  stdout and is a usage error without `--check`, since a build spawns a `--cc` that inherits
  stdout and could not promise a complete document or nothing (D20.2). `--index` fills the
  document's `"symbols"` array with the identifier index of section 9.1 and implies `--check` and
  `--json`, so `fort --index main.ft` is the whole of what an editor runs (D20.3).
- `--tokens` runs the lexer over the entry file and stops there: it resolves no import, parses
  nothing and needs no standard library, so it is the one thing a compiler with a lexer and no
  parser can do (D14.1). It writes one line per token to stdout, ending with the `end of file`
  token, and is a usage error together with `--check`, `--json` or `--index`, which all need a
  front end; every other option is unused, as under `--check`. A lexical error is reported on
  stderr in the form of section 4 and lexing resumes at the next line (D14.2), so the dump covers
  the whole file either way and the status is then 1. A line is

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
  `identifier`, `integer literal`, `float literal`, `char literal`, `string literal` and
  `end of file` -- which stands last because it is the only field that may hold a space. So
  `fort --tokens` on a file holding `x = 0x10;` writes

  ```sh
  1:1-1:2 0 "x" identifier
  1:3-1:4 0 "=" =
  1:5-1:9 16 "0x10" integer literal
  1:9-1:10 0 ";" ;
  2:1-2:1 0 "" end of file
  ```

  Both compilers write those bytes: `tools/diff_tokens.sh` compares stage1's dump with stage2's,
  together with their diagnostics and their exit statuses, over every `.ft` file in the
  repository, and that is how the self-hosted lexer is held against the bootstrap's (the ctest
  `diff-tokens`).
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
  module import path item fn extern-fn param struct field-decl enum member var
  type prim string void noreturn name fn-type ptr span array
  block assign incdec call-stmt if while do for range-for switch case defer return
  break continue init designator
  int float char str bool null ident unary binary ternary call index span field arrow
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

  Both compilers write those bytes: `tools/diff_ast.sh` compares stage1's tree with stage2's,
  together with their diagnostics and their exit statuses, over every `.ft` file in the
  repository, and that is how the self-hosted parser is held against the bootstrap's (the ctest
  `diff-ast`).
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

These are all the `fort: error: <message>` texts, each of them exit status 2 (D14.1). Six report a
command line the compiler cannot use and are followed by the usage line: `missing argument for
option '<opt>'`, `unexpected argument '<arg>'` (a second entry file), `unknown option '<opt>'`, `no
entry file`, `--json requires --check` (D20.2) and `--tokens does not combine with --check, --json
or --index`. Four report an operation of section 2 that
failed, with the system's error text as `<reason>`: `cannot read '<file>': <reason>` (the entry
file), `cannot write '<file>': <reason>` (the LLVM IR module), `cannot create a temporary directory
in '<dir>': <reason>` (`mkdtemp` under `$TMPDIR`) and `cannot run '<cc>': <reason>` (`--cc` could
not be started). Two report the outcome of `--cc`: `cc failed with status <n>` and `cc failed with
signal <n>`. The last two are the compiler's own failures: `internal error: <what>` and `out of
memory`.

```sh
fort main.ft -o main                          # build ./main in checked mode
fort -S main.ft                               # write main.ll and stop
fort -c main.ft                               # write main.o and stop
clang --target=x86_64-linux-gnu -o main main.o          # link the -c object by hand
fort --release -o main main.ft                # release mode
fort --release --no-bounds-check -o bench main.ft
fort -I lib -I vendor -lm main.ft             # extra roots, link libm
fort --cc clang-18 --target x86_64-linux-gnu -Xcc -fuse-ld=lld main.ft
fort --check lib/util.ft                      # check that module and its imports, print nothing
fort --check --json main.ft                   # one JSON document on stdout, for an editor
fort --index main.ft                          # the same document with the identifier index
fort --tokens main.ft                         # one line per token of that file, nothing else
FORT_STD_DIR=/opt/fort/std fort main.ft
```

The only environment variables read are `FORT_STD_DIR` (D14.1) and `TMPDIR`, which locates the
temporary directory for the intermediate IR file (D19.1).

## 2. Build pipeline

Compilation is whole-program (D9.10):

1. Read the entry file and derive its module path and root (exit 2 if unreadable, 1 if the base
   name contains a `.` or a `:`, the two characters a module path is spelled with, which would
   let it collide with that module's symbols). The base name need not otherwise be an identifier:
   the entry file is named on the command line rather than reached by an import path, so
   `007_case.ft` is the module `007_case` and nothing can import it, and `my-app.ft` is legal too
   (D9.1 as amended).
2. Parse it; resolve each import (module-system.md 2 and 3); parse each newly reached module
   until the closure is complete; reject cycles and duplicate identities (exit 1). `std.rt` is a
   root of the closure beside the entry file and is loaded whether or not anything imports it
   (D9.10, section 5).
3. Check every module in dependency order, imported modules first (exit 1).
4. Emit one LLVM IR module for the closure to `<tmp>/<entry>.ll` (D19.1), or to the `-S` output
   and stop.
5. Run `<cc>` over that module once: it compiles and links in one invocation (D14.3), exit 2 on
   failure. The line is

   ```sh
   clang --target=x86_64-linux-gnu -O1 -fPIE -pie -Wno-override-module \
       -o <out> <tmp>/<entry>.ll <-l options> <-Xcc args>
   ```

   with `-O2` in place of `-O1` under `--release` (D14.3), and `-c` before `-o`, no `-pie` and
   no `-l` for `-c`. The module is the only input the compiler names: it holds the whole program,
   the runtime included (D9.10, D13.1). That clang finds the cross sysroot, its `Scrt1.o`,
   `crti.o` and `crtn.o` and `x86_64-linux-gnu-ld` by itself, so no `--sysroot`,
   `--gcc-toolchain` or `-fuse-ld` is needed; `-Wno-override-module` silences the warning about
   the module's own target triple, and the `.ll` suffix is what tells clang the input is IR, so
   `-x ir` is not passed.
6. Remove the temporary directory.

`--check` stops after step 3 (D20.1): it emits no module, creates no temporary directory, runs no
`--cc`, and does not apply the entry-point rule of D8.6, since the file it is given is a module
under inspection rather than a program.

- The temporary directory comes from `mkdtemp` under `$TMPDIR` (default `/tmp`) and is removed
  whether or not `--cc` succeeded.
- `--cc` is invoked with exactly the arguments shown, in that order; `-fPIE -pie` and the
  position-independent code the IR compiles to make the output a position-independent executable
  (D14.3, D16).
- Every emitted module passes `opt -passes=verify` (D19.1). `test/ir/*.ll` are hand-written
  modules in the form the compiler emits and `test/pipeline_test.sh` runs this pipeline over
  them; the language-test harness verifies the module of every test that compiles
  (`run_tests.py --verify-ir`, section 7.3).
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
the allocation the old value designated leaks (D17.11). Programs must not rely on wrapping or
trapping for correctness (D11.1); the wrapping operators exist for code that needs wrap-around in
both modes. `p[lo..hi]` on a raw pointer is never checked (D6.9), and the undefined behaviors of
D10.7 are undefined in every mode.

## 4. Diagnostics

Compile-time diagnostics (D14.2) are written to stderr, one per line:

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
  node carries that range and the range of its name. The text form above prints the start only,
  so an end never appears in a diagnostic line.
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
  and before a token that starts a top-level declaration, the end of the file ending every skip.
  Which other tokens stop it depends on the recovery point: a skip that stands where a statement
  or a clause would stops before `case` and `default`, since a switch body holds nothing else,
  and one that stands where a statement would also stops before a statement keyword (`if while
  for switch defer return break continue do`); a struct field is skipped to its `;` or to the `}`
  of the body, and a skip at the top level, where a `}` closes nothing, consumes one. Two
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
- The compiler emits no warnings (D14.2): unused imports, unused variables and statements after
  a terminating statement are not diagnosed.

```sh
main.ft:7:5: error: cannot assign to immutable 'x'
main.ft:3:9: note: 'x' declared here
util.ft:12:23: error: expected ';'
```

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
// entry point directly casts to a typed pointer to write through it -- which
// adoption does anyway (D3.14, D17.3) -- and the cast adds no `mut`. `free`
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
// `argv` byte sequence itself), then the `fort_entry` it emits beside it, then
// `flush_all`, and returns `status & 0xFF`. The span lives for the whole process
// and `args()` hands it out for `sys.args()`. `exit` flushes every buffer and
// ends the process with `status & 0xFF`; `sys.exit` is a call to it.
fn args_init(i32 argc, char* mut* argv) void;
fn args() string@;
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
executes the trap of section 6 item 20 (D8.5, D19.7): the process dies with SIGILL and no
message.

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
and must pass `opt -passes=verify` (D19.1). The two examples at the end are `test/ir/hello.ll`
and `test/ir/abort.ll` byte for byte; the pipeline test builds and runs them (section 2). They
are hand-written modules that exercise the pipeline, not output of the compiler: each defines the
handful of `std.rt` entry points it calls, over the C library, where a module the compiler builds
holds the whole of `std.rt` (item 8, D9.10, D13.1). A change to one of them is a change here.

1. **Form and module header.** One textual module (`.ll`, LLVM 18 syntax, opaque pointers) holds
   the whole program (D9.10, D19.1) and is built by appending text in one forward pass. It
   begins with

   ```llvm
   target triple = "x86_64-unknown-linux-gnu"
   ```

   in clang's normalized spelling, and carries no `target datalayout`, no `!llvm.module.flags`,
   no `!llvm.ident`, no `source_filename` and no comments (D19.1): clang derives the layout from
   the triple, and position independence comes from the `--cc` line (section 2), not from module
   flags. Sections appear in this order and nowhere else: the triple, the named types, the
   module-level globals and constants (D7.10), the function definitions, the private data, the
   declarations, the attribute groups. Forward references to globals are legal in `.ll`, which
   is what lets one pass emit a function before the data it names. Naming and ordering inside
   each section follow D19.5, so the text is a function of the program alone.

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
   the ELF symbol, which is `main.add`. C names (`extern` declarations, and the `fort_entry` and
   `main` the compiler emits) are unquoted; the runtime is fort, so `@"std.rt.print_i64"` is
   quoted like every other dotted name (D9.7). Fort functions, constants and globals are
   `dso_local` with the default external linkage (D9.6), so fort-to-fort calls are direct, a call
   into the runtime among them, and fort data is addressed PC-relative; `extern` symbols carry no
   `dso_local` and go through the procedure linkage and global offset tables. Private data
   (`@.str.N`, `@.file.N`, `@.enum.<path.name>`) is `private unnamed_addr`.

   A name LLVM's unquoted identifiers (`[-a-zA-Z$._][-a-zA-Z$._0-9]*`) do not admit is quoted
   too, which only an entry module's can be, since every other module path is identifiers
   joined with dots (D9.1): `%"struct.a\22b.point"` and `@".enum.a\22b.color"` for an entry
   file `a"b.ft`. Inside the quotes, the two bytes a quoted name cannot hold, `"` and `\`, and
   every byte outside the printable range are written as the `\XX` hex pair of item 5, which
   LLVM reads back to the byte: the ELF symbol is the name itself, so this stays spelling only
   like the quoting of every dotted name (D9.7). One ELF symbol is one IR entity: `fort_entry` and
   `main` are reserved, so the checker refuses an `extern` that declares either (D9.7,
   module-system.md 13) and the emitter declares no name it defines, which leaves the two
   definitions of item 22 alone. A runtime entry point is a fort definition in the module like any
   other, so nothing declares it either (item 8).

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
     (`test/ir/hello.ll`), and an enum table exists only if some `print` of that enum type is
     compiled (item 21).

6. **Position independence** (D14.3, D16). Nothing in the IR expresses it: `dso_local` (item 4)
   and the `-fPIE -pie` of section 2 give RIP-relative data, direct fort-to-fort calls and
   linkage-table calls to `extern` symbols. The one requirement the module carries
   is that an address is never an integer constant derived from a symbol; addresses appear only
   as `ptr` values and as `ptr` constants in initializers.

7. **Calling convention, fort to fort** (D9.9). Scalars (integers, `bool`, `char`, enums,
   pointers, function pointers, floats) are ordinary parameters and results and LLVM applies
   System V. A struct, fixed array, span or `string` argument is a plain `ptr` parameter: the
   caller allocates a copy in its entry block, `llvm.memcpy`s into it and passes its address,
   and `byval` is never used, since it would mean a callee-visible copy on the stack rather than
   the pointer in the integer slot D9.9 requires. An aggregate result is a leading
   `ptr sret(%T) %ret.sret` parameter on a function whose result type is `void`; the pointer
   arrives in `rdi` and is echoed in `rax`, which is D9.9's ABI. The attribute is written on the
   definition and not at the call site, which passes the destination as a plain `ptr`: on
   x86-64 the two are identical and only tail-call eligibility can tell them apart (D9.9).
   A span or `string` is one hidden pointer and is never split into two scalars, so
   `fort_entry`'s C prototype stays literally true (D11.6). `bool`, `char`, `u8` and `u16`
   parameters and results carry `zeroext` and `i8` and `i16` carry `signext`, in fort and extern
   signatures alike, so an extern-legal signature is a valid C callback by construction (D9.9).
   Every fort definition is `define dso_local <ret> @"m.f"(...) #0`, where `#0` is
   `{ nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }`: `nounwind` because fort has
   no exceptions, the frame pointer because it is what a debugger gets without DWARF (section
   9), and `probe-stack` for item 13.

   A call through a function pointer is an ordinary `call` whose callee is the `ptr` value and
   whose function type is written out, because an opaque pointer carries none (D19.2):
   `call i32 (i32, i32) %t0(i32 %t1, i32 %t2)`, and `call void (ptr, i32) %t0(ptr %r.0, i32 3)`
   for an aggregate result. The callee is evaluated before the arguments (D6.3) and every rule
   above holds at that call site unchanged, aggregate arguments and the extension attributes
   included, so it differs from the call of a name only in the callee and that type (D3.10). The
   callee is always a fort function, since an `extern fn` in value position is an error (D3.10):
   an extern is called through the variadic type of item 8, which only its declaration can
   supply.

8. **Extern declarations** (D9.8). An `extern` function is declared with its C types,
   unmangled, and with a variadic tail, and is called through the matching variadic call type:

   ```llvm
   declare i32 @printf(ptr, ...)
   declare signext i8 @c_narrow(i8 signext, i16 zeroext, ...)
   ```

   ```llvm
     %t10 = call i32 (ptr, ...) @printf(ptr @.str.0) #3
     %t11 = call signext i8 (i8, i16, ...) @c_narrow(i8 signext %t8, i16 zeroext %t9) #3
   ```

   LLVM passes the vector-register count a variadic callee reads exactly when the call-site type
   is variadic, so declaring every extern variadic is what makes a fixed-prototype declaration
   of a variadic C function safe (D9.8); a non-variadic callee ignores that count, so the
   declaration is ABI-identical for it. `#3 = { nobuiltin }` on every extern call site that goes
   through that variadic type keeps LLVM from rewriting a declared symbol into another library
   call, and is preferred to a driver-wide `-fno-builtin`, which would also change how our
   `llvm.memcpy` and `llvm.memset` are lowered. A call through a function pointer is not variadic
   (D3.10 has no variadic function type) and needs no such declaration; it is also never a call of
   an extern, whose name is not a value, so every extern call of a C library symbol in the module
   carries the variadic type above (D3.10).

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
   first-use order, then the intrinsics in the order of the table above, the two groups separated
   by a blank line. There are two groups and no third: a symbol is declared exactly once, and
   every fort function the module calls, the runtime's included, is defined in it. Two modules
   that declare one C symbol are two fort declarations of one ELF symbol and yield one `declare`,
   which is why the extern group is keyed by the C name and not by the declaration (D9.7, D9.8).

9. **Normalization** (D9.8, D19.2). A narrow value is not widened to 32 bits: an `i8` value has
   type `i8` and its width is in the type. The only extensions the emitter produces are the
   `zeroext` and `signext` attributes of item 7, which make LLVM normalize on both sides of a
   call, the `trunc` and `zext` of `bool`'s value and memory types, and the explicit conversions
   of item 12.

10. **Locals, control flow, evaluation order** (D19.4). Every local, scalar parameter copy and
    compiler temporary is an `alloca` in the entry block, before any other instruction, in
    declaration order; nothing is variable-length. Names are fixed by D19.5, per function and
    reset at each definition: `%t<N>` for an instruction result in emission order, `%L<N>` for a
    block in creation order with the entry block always literally `entry`, `%<ident>.<slot>` for
    a local or parameter slot, `%<ident>.in` for an incoming parameter, `%ret.sret` for an
    aggregate result pointer, and `%tmp<K>` from a third counter for a place the compiler
    invents (an aggregate argument copy, a short-circuit slot). A name that embeds a fort
    identifier always contains a dot and an invented one never does, so a local named `tmp` is
    `%tmp.0` and cannot collide with `%tmp0` (D19.5).

    A scalar parameter arrives as `%<name>.in` and is stored into its slot immediately. An
    aggregate parameter is not copied again: its place is the caller-made copy the incoming
    `ptr` designates (item 7), which the callee may write to, since D8.2's by-value rule is
    satisfied by the caller's copy. `fort_entry` is the exception, because its caller is the C
    runtime rather than fort code, which is why item 22 copies the argument span. Control flow
    is explicit blocks: `if`, `while`,
    `for`, `break` and `continue` become `br`; a fort `switch` on an integer, `char` or enum
    becomes an LLVM `switch` with one case per label and a default block: the `default` clause
    wherever it stands, the continuation when there is none, and, for an enum switch with no
    `default` clause, a failure block calling `std.rt.fail_enum` with the operand
    sign-extended to 64 bits and the enum's name, which is the default D7.7 gives it and which
    neither build mode removes; `&&`, `||` and
    `?:` short-circuit through a stack slot rather than a `phi`, so the tree walk never has to
    know its predecessors; after a terminating statement the emitter opens a fresh `%L<N>` block
    for the unreachable statements D14.2 allows. Evaluation order needs nothing: LLVM keeps the
    order of side effects and the walk emits calls, loads and stores in source order, `fd` once
    (D6.3, D12.2). Deferred statements are already expanded at each exit by the front end
    (D7.8).

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

13. **Stack probing** (D10.8). The `"probe-stack"="inline-asm"` attribute on every fort
    definition (item 7) makes the backend establish a frame larger than a page one page at a
    time. It is in the IR rather than on the `--cc` line so that a module written by `-S`
    carries the guarantee by itself.

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
    Every `noreturn` entry point of section 5.1 carries `#8 = { cold noreturn nounwind
    "frame-pointer"="all" "probe-stack"="inline-asm" }` on its definition: the `std.rt.fail_*`
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
    `define dso_local void @"m.f"(...) #1` with `#1 = { noreturn nounwind "frame-pointer"="all"
    "probe-stack"="inline-asm" }`, and the block that would fall off the end of its body ends
    with

    ```llvm
      call void @llvm.trap()
      unreachable
    ```

    as does every call site of such a function (D8.5 requires the trap in both places).
    `llvm.trap` is the trap instruction D8.5 asks for, and `unreachable` alone would not be one,
    since LLVM may let control fall through it. Reaching either raises SIGILL with no message
    (D11.4).

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

22. **`fort_entry` and `main`** (D11.6, D8.6). Both are emitted in the entry module and are the
    only unmangled definitions in it (D9.7). `fort_entry` receives the argument span
    by hidden pointer, copies it into its own frame when `main` declares the parameter, and
    returns what `main` returns:

    ```llvm
    define dso_local i32 @fort_entry(ptr %args.in) #0 {
    entry:
      %args.0 = alloca %fort.span, align 8
      call void @llvm.memcpy.p0.p0.i64(ptr align 8 %args.0, ptr align 8 %args.in, i64 16, i1 false)
      %t0 = call i32 @"main.main"(ptr %args.0)
      ret i32 %t0
    }
    ```

    When `main` takes no parameter there is no `alloca` and no copy, only the call and the
    `ret` (the second example below). `@main` is the C entry point the start-up code calls, so it
    takes C's `argc` and `argv` and returns C's `int`; it is a definition of this module like any
    other and carries `dso_local` and `#0` (item 4, item 7):

    ```llvm
    define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
    entry:
      %args = alloca %fort.span, align 8
      call void @"std.rt.args_init"(i32 %argc, ptr %argv)
      call void @"std.rt.args"(ptr %args)
      %t0 = call i32 @fort_entry(ptr %args)
      call void @"std.rt.flush_all"()
      %t1 = and i32 %t0, 255
      ret i32 %t1
    }
    ```

    `std.rt.args` returns an aggregate, so it takes the destination as the hidden result pointer
    of item 7, written `sret(%fort.span)` on its own definition and a plain `ptr` here (D9.9);
    `args_init` runs first, since `args` hands out what it built. The `and` is D11.6's
    `status & 0xFF`.

23. **`-S` and `-c`** (D14.1). `-S` writes the module and stops, so the text above is exactly
    what a user reads; `-c` writes it into the temporary directory and runs `--cc -c` over it
    (section 2). The compiler never writes a `.s` file; `llc` over the `-S` output is how a
    human reads the machine code.

The attribute groups have fixed indices, and only the used ones are emitted, so gaps in the
numbering are normal (D19.5):

- `#0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }` on every fort definition
  (item 7), and `#1`, the same set plus `noreturn`, on a `noreturn` definition (item 20).
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

in `main.ft` is `test/ir/hello.ll`:

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local void @"std.rt.print_str"(i32 %fd, ptr %ptr, i64 %len) #0 {
entry:
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %ptr, i64 %len) #3
  ret void
}

define dso_local void @"std.rt.print_char"(i32 %fd, i8 zeroext %c) #0 {
entry:
  %byte.0 = alloca i8, align 1
  store i8 %c, ptr %byte.0, align 1
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %byte.0, i64 1) #3
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

define dso_local void @"std.rt.flush_all"() #0 {
entry:
  ret void
}

define dso_local i32 @"main.main"() #0 {
entry:
  call void @"std.rt.print_str"(i32 1, ptr @.str.0, i64 13)
  call void @"std.rt.print_char"(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"main.main"()
  ret i32 %t0
}

define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
entry:
  %args = alloca %fort.span, align 8
  call void @"std.rt.args_init"(i32 %argc, ptr %argv)
  call void @"std.rt.args"(ptr %args)
  %t0 = call i32 @fort_entry(ptr %args)
  call void @"std.rt.flush_all"()
  %t1 = and i32 %t0, 255
  ret i32 %t1
}

@.str.0 = private unnamed_addr constant [14 x i8] c"hello, world!\00", align 1

declare i64 @write(i32, ptr, i64, ...)

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #3 = { nobuiltin }
```

The module the compiler emits for that program differs from this one in one way, and the way is
the size: it also holds every definition of `std.rt` and of `std.libc`, because every closure
holds the runtime (D9.10, D13.1). The five definitions above stand for them, over the C library's
`write`, so that the file is small enough to read and the pipeline test links it on its own. Every
other byte is what the compiler writes: `main.main`, `fort_entry` and the `main` of item 22, the
quoted dotted names of item 4, the private data of item 5 and the attribute group of item 7.

### 6.2 A program with a check

```fort
fn main() i32 {
    println("before");
    i32[3] a = {};
    i64 mut i = 5;
    return a[i];
}
```

in `abort.ft`, whose module path is therefore `abort` (D9.1) and whose `main` is the symbol
`abort.main` (D9.7), with the `[` of `a[i]` at line 12, column 13, is `test/ir/abort.ll` on the
same terms:

```llvm
target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local void @"std.rt.print_str"(i32 %fd, ptr %ptr, i64 %len) #0 {
entry:
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %ptr, i64 %len) #3
  ret void
}

define dso_local void @"std.rt.print_char"(i32 %fd, i8 zeroext %c) #0 {
entry:
  %byte.0 = alloca i8, align 1
  store i8 %c, ptr %byte.0, align 1
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %byte.0, i64 1) #3
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

define dso_local void @"std.rt.flush_all"() #0 {
entry:
  ret void
}

define dso_local void @"std.rt.fail_bounds"(i64 %i, i64 %n, ptr %f, i32 %l, i32 %c) #8 {
entry:
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 2, ptr @.str.1, i64 65) #3
  call void (...) @abort() #3
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

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"abort.main"()
  ret i32 %t0
}

define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
entry:
  %args = alloca %fort.span, align 8
  call void @"std.rt.args_init"(i32 %argc, ptr %argv)
  call void @"std.rt.args"(ptr %args)
  %t0 = call i32 @fort_entry(ptr %args)
  call void @"std.rt.flush_all"()
  %t1 = and i32 %t0, 255
  ret i32 %t1
}

@.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
@.str.0 = private unnamed_addr constant [7 x i8] c"before\00", align 1
@.str.1 = private unnamed_addr constant [66 x i8]
    c"abort.ft:12:13: runtime error: index 5 out of range for length 3\0A\00", align 1

declare void @abort(...)
declare i64 @write(i32, ptr, i64, ...)

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

## 7. Testing

### 7.1 Layout (D14.4)

```sh
test/
  test.h                       C unit-test macros
  common.h                     TEST_UNUSED and shared helpers, provided by the implementation
  <component>_test.c           one C suite per compiler component (lexer_test.c, ...)
  lang/
    run_tests.py               runs every language test below
    run_tests_test.py          the harness's own unit tests
    xfail.txt                  tests the compiler cannot pass yet
    bootstrap-unsupported.txt  tests stage1 must reject
    xfail-stage2.txt           tests stage2 cannot pass yet
    unsupported-stage2.txt     tests stage2 must reject
    run/<area>/NNN_name.ft     compile, run, compare
    fail/<area>/NNN_name.ft    must not compile, with annotated errors
    run/modules/<name>/main.ft multi-file run test; the directory is the root
    fail/modules/<name>/main.ft multi-file fail test, same rule
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
| `//! error: <substring>`        | `fail` tests only, at the end of the offending line      |
| `//! error-any: <substring>`    | `fail` tests only, at the top                            |

The expected output is each `//| ` line's text after the marker followed by a newline; a bare
`//|` is an empty line. Output without a final newline cannot be expressed, so tests end their
output with `println`. Every `stderr:` substring must appear in stderr.

In a `fail` test every `//! error:` line must produce a diagnostic on that line containing the
substring, and no unannotated diagnostic may occur; `//! error-any:` requires some diagnostic to
contain the substring and is for errors without a useful line, such as circular imports.

### 7.3 What the harness does

`test/lang/run_tests.py [options] [filter...]` (Python 3, standard library only) runs the
compiler named by `--fort` (default `$FORT`, else `build/debug/fort`) with `--std-dir` from
`--std-dir` (default `$FORT_STD_DIR`, else `std` beside the compiler) and `--cc` from `--cc`
(default `clang`, which must be a clang as `--cc` is, D14.1). `--target` (default
`x86_64-linux-gnu`) is the triple the harness passes to `--cc` as `--target=<triple>` when it
links a test's C helpers itself. `--verify-ir` runs `fort -S -o prog.ll <test>` for every test
whose compilation succeeds and verifies the module with `<opt> -passes=verify -disable-output`
(D19.1); `--opt` names that program (default `opt-18`). A module the verifier rejects is a FAIL;
an `opt` that cannot be launched, times out or dies by a signal is an ERROR, like a compiler exit
2 (D14.1). `--runner` names a command that runs the programs when binfmt does not, `-j` the
number of parallel tests, `--timeout` the seconds per step, `-v` prints the commands and outputs
of failures and `--keep` keeps the temporary directories. The
compiler runs with `test/lang` as its working directory (D14.4). For each test, in a fresh
temporary directory that is also `TMPDIR`, with `LC_ALL=C`, `QEMU_LD_PREFIX` set unless
inherited and core dumps disabled:

| Directive    | Harness action                                                              |
|--------------|-----------------------------------------------------------------------------|
| `run`        | `fort <flags> -o prog <test>` must exit 0; run `prog`; compare its output    |
| `fail`       | `fort <flags> -o prog <test>` must exit 1 with only annotated errors        |
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

Two expectation files beside the harness list path prefixes of tests (relative to `test/lang`,
`#` comments allowed). `xfail.txt` names the tests the compiler cannot pass yet: a listed test
that fails or errors is `XFAIL`, a listed test that passes is `XPASS` and fails the run, so the
list shrinks in the commit that makes tests pass. `bootstrap-unsupported.txt` names the tests
that use features the C bootstrap deliberately lacks (floats, the nested array and span levels
of D3.6, `do`-`while` and `?:`; function pointers are in its subset, D3.10). Each is judged as a
`fail` test, whatever its own kind. The compiler must exit 1 and must report at least one
diagnostic. At least one of those diagnostics must contain `not supported by the bootstrap
compiler`, or must stand in a file of the test itself. The two shapes answer two cases and
neither one covers both. A test refused inside the library's import closure gets no diagnostic
that names the test: `run/stdlib/096_math_limits.ft` imports `std.math`, whose `?:` the C
bootstrap refuses, and all nine of its diagnostics name `std/math.ft`. A test that spells a form
added after the pinned tree the C bootstrap compiles (`notes/compiler.md` 8) gets an ordinary
syntax error in its own file, with none of those words, because that compiler never learned to
name the form. Amended 2026-09-14 (T-131), which added the second shape; until then the words
were the only expectation.
`--xfail` and `--unsupported` name other lists; `--no-xfail` and `--no-unsupported` ignore
them. Each compiler has one list of each kind: `xfail-stage2.txt` and `unsupported-stage2.txt`
are stage2's, and the CMake test `lang-stage2` names both, because stage2 reads the nested
levels stage1 refuses, implements the `do`-`while` and `?:` stage1 refuses (D6.6, D7.5) and
accepts the floats stage1 refuses (D2.6, D3.1), and must be judged for all of them like any
other test.

Each compiler answers for its own list. `unsupported-stage2.txt` is the list the self-hosted
compiler is run with, and it is empty: the four families the C bootstrap lacks are the nested
array and span levels of D3.6, `do`-`while`, `?:` and floats, and stage2 implements all four, so
it refuses nothing the corpus holds and answers for every test as for any other. Every one of
those entries stays in `bootstrap-unsupported.txt`, which is stage1's and which nothing empties,
since the C bootstrap is frozen. An entry arrives in stage2's list the day stage2 refuses a test
stage1's list also holds, and leaves it the day stage2 implements the feature. Neither run passes
`--no-unsupported`, so neither compiler is excused any test of the corpus.

The harness prints one `PASS`, `FAIL`, `XFAIL`, `XPASS` or `ERROR` line per test with the
reason where there is one, then a summary, and exits with 1 if any test is `FAIL`, `XPASS` or
`ERROR`; each `filter` selects the tests whose path contains it. `--list` prints the selected
tests and their count. `--lint` validates the corpus without a compiler and fails on: a first
line other than `//! run` or `//! fail` or one that does not match the directory; an unknown,
malformed, duplicated or empty directive; a `signal:` naming none of the six signals above;
two of `exit`, `abort` and `signal` together; a run-only directive in
a `fail` test or `error`/`error-any` in a `run` test; a `link:` file that does not exist; a
`//<` or `//|` not followed by a space or outside its block; a directive after the header or in
a sibling module; a `fail` test with neither `error:` nor `error-any:`; an unknown area, a
badly named test, a stray file, a directory test outside `modules`, and a gap or duplicate in
the `NNN` numbering of an area; and an expectation-list entry that matches no test. A run
performs the same checks first and stops when they fail, and fails when the filters select no
test.

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

Each `test/<component>_test.c` is one suite built against the compiler's sources and `test.h`:
tests are defined with `TEST(name, body)`, registered with `TEST_RUN`, and the suite exits through
`TEST_EXIT` with status 0 (ok), 1 (a failed assertion) or 2 (a test error). The first command-line
argument, when present, is a name prefix selecting tests. `TEST_ASSERT_*` macros log file, line,
expression, actual and expected values and return `TEST_RESULT_FAIL`. A body is one macro
argument, so a comma outside parentheses inside it must be parenthesized.

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

The corpus aims at about three lines of test for each line of source: `wc -l` over `test/*.c`,
`test/*.h`, `test/**/*.ft` and `test/lang/ffi/*.c` against `wc -l` over `src/bootstrap/*.c`,
`src/bootstrap/*.h`, `src/fort/*.ft` and `std/*.ft`, the runtime among them (D13.1). The
standard library is source and not test (D14.6): it is code the project ships, and the tests that
exercise it are `test/lang/run/stdlib`. The seed tests of the
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
- **The chain that builds the compiler** (T-131). The C compiler in `src/bootstrap` does not
  compile `src/fort`. `tools/bootstrap.ref` names a chain of pinned commits of this repository,
  oldest first. The C compiler builds pin 0's `src/fort` with pin 0's `std`, each pin builds the
  next, and the last pin builds HEAD's `src/fort` with HEAD's `std`. So `src/fort` and `std` may
  use any form the last pin implements, and a cold machine still builds everything from the C
  sources and the git history. A shallow clone holds no pin and cannot build. `notes/compiler.md`
  8 states the five invariants of the chain and says when a pin moves.
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
- **Codegen.** One forward pass over the annotated AST appending text to a single LLVM IR
  module (D19.1, section 6), with no intermediate representation of its own, no libLLVM and no
  register allocation: locals are `alloca`s in the entry block, intermediates are SSA
  temporaries, and deferred statements are expanded statically at each exit (D7.8).
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
generation, cross-compilation and any target other than x86-64 Linux, `--help` text beyond the
usage line, and conditional compilation. The idioms that replace the deferred language features
are listed with each item in D15.
