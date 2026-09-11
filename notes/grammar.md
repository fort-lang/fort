# fort grammar

Normative grammar for fort v1. Decisions are in `decisions.md` (cited as `Dn.m`). Where this file
and a specification document disagree, this file and `decisions.md` win.

Notation is EBNF: `=` defines a rule, `|` separates alternatives, `[ x ]` is optional, `{ x }` is
zero or more, `( x )` groups, terminals are quoted, `;` ends a rule. Rule names are `snake_case`.
The grammar is LL(k) with one exception, the declaration-versus-statement choice, which is handled
by the speculative parse described under "Disambiguation" at the end.

## 1. Lexical grammar

Tokens are produced greedily (longest match). Whitespace and comments separate tokens and are
otherwise ignored. No token spans a newline.

```ebnf
letter      = "A" ... "Z" | "a" ... "z" | "_" ;
dec_digit   = "0" ... "9" ;
oct_digit   = "0" ... "7" ;
bin_digit   = "0" | "1" ;
hex_digit   = dec_digit | "A" ... "F" | "a" ... "f" ;

identifier  = letter { letter | dec_digit } ;          (* D2.3; not a keyword *)

keyword     = "as" | "bool" | "break" | "case" | "cast" | "char" | "continue" | "default"
            | "defer" | "do" | "else" | "enum" | "extern" | "f32" | "f64" | "false" | "fn"
            | "for" | "i8" | "i16" | "i32" | "i64" | "if" | "import" | "mut" | "new"
            | "noreturn" | "null" | "own" | "return" | "sizeof" | "string" | "struct"
            | "switch" | "true" | "u8" | "u16" | "u32" | "u64" | "void" | "while" ;  (* D2.4 *)

reserved    = "async" | "await" | "const" | "match" | "pub" | "priv" | "trait" | "type"
            | "union" | "yield" ;                        (* D2.4; usable nowhere *)

int_literal = dec_literal | hex_literal | oct_literal | bin_literal ;      (* D2.5 *)
dec_literal = "0" | ( "1" ... "9" ) { [ "_" ] dec_digit } ;
hex_literal = "0x" hex_digit { [ "_" ] hex_digit } ;
oct_literal = "0o" oct_digit { [ "_" ] oct_digit } ;
bin_literal = "0b" bin_digit { [ "_" ] bin_digit } ;

float_literal = dec_literal "." dec_digits [ exponent ]
              | dec_literal exponent ;                   (* D2.6: no leading zero *)
dec_digits    = dec_digit { [ "_" ] dec_digit } ;
exponent      = ( "e" | "E" ) [ "+" | "-" ] dec_digits ;

char_literal   = "'" ( char_plain | escape ) "'" ;       (* D2.7 *)
char_plain     = any printable ASCII byte (0x20 ... 0x7E) except "'" and "\" ;
string_literal = '"' { string_plain | escape } '"' ;     (* D2.9 *)
string_plain   = any byte except '"', "\", and newline ;
escape         = "\n" | "\t" | "\r" | "\0" | "\\" | "\'" | '\"'
               | "\x" hex_digit hex_digit ;               (* D2.8 *)

line_comment  = "//" { any byte except newline } ;       (* the only comment form, D2.2 *)

operator = "+%=" | "-%=" | "*%=" | "<<=" | ">>="
         | "+%" | "-%" | "*%" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
         | "==" | "!=" | "<=" | ">=" | "&&" | "||" | "<<" | ">>" | "++" | "--"
         | "::" | "->" | ".."
         | "+" | "-" | "*" | "/" | "%" | "=" | "<" | ">" | "!" | "&" | "|" | "^" | "~"
         | "?" | ":" | "." | "," | ";" | "(" | ")" | "[" | "]" | "{" | "}"
         | "@" ;                                    (* D2.10; "@" is the span suffix, D3.5 *)
```

Notes:

- Non-ASCII bytes may appear only inside string literals and comments (D2.1).
- `/*` is not a comment introducer: the adjacent pair is a lexical error (D2.2). Separated, `/`
  and unary `*` are the ordinary operators.
- `1.` and `.5` are not float literals; `1.0` and `0.5` are. `1..5` lexes as `1`, `..`, `5`.
- Nesting depth of `( [ {` and of type suffixes is limited to 256 (D2.11).

## 2. Module structure

```ebnf
module      = { import_decl } { top_decl } ;               (* D9.3: imports first *)

import_decl = "import" import_path [ "as" identifier ] ";"
            | "import" import_path "::" "{" import_item { "," import_item } [ "," ] "}" ";" ;
import_path = identifier { "::" identifier } ;
import_item = identifier [ "as" identifier ] ;

top_decl    = fn_decl | extern_decl | struct_decl | enum_decl | global_decl ;
```

Whether the last segment of an `import_path` names a module or a symbol is decided by resolution
(D9.3), not by the grammar.

## 3. Declarations

```ebnf
fn_decl      = "fn" return_type identifier "(" [ param_list ] ")" block ;    (* D8.1 *)
extern_decl  = "extern" "fn" return_type identifier "(" [ param_list ] ")" ";" ;  (* D9.8 *)
return_type  = type | "void" | "noreturn" ;                                 (* D8.5 *)
param_list   = param { "," param } ;
param        = type identifier ;

struct_decl  = "struct" identifier "{" field { field } "}" ;                (* D3.8 *)
field        = type identifier ";" ;

enum_decl    = "enum" identifier "{" enum_member { "," enum_member } [ "," ] "}" ;  (* D3.9 *)
enum_member  = identifier [ "=" const_expr ] ;

global_decl  = var_decl ;                                  (* initializer constant, D7.10 *)

var_decl     = var_decl_head ";" ;                         (* D7.1 *)
var_decl_head = type identifier "=" initializer ;
initializer  = expr | brace_init ;
brace_init   = "{" [ init_list ] "}" ;                     (* D6.5 *)
init_list    = positional_list | designated_list ;
positional_list = initializer { "," initializer } [ "," ] ;
designated_list = designated { "," designated } [ "," ] ;
designated   = "." identifier "=" initializer ;
```

A `struct_decl` has no trailing semicolon. A `brace_init` is legal only as the `initializer` of a
`var_decl_head` whose type is a struct or array, as `{}` for any aggregate, span, string or
enum, and nested inside another `brace_init` or typed literal (D6.5).

## 4. Types

```ebnf
type         = base_type [ "own" ] [ "mut" ] { ref_suffix } { array_suffix } { ref_suffix } ;
                                                            (* D3.6, D5.3, D17.2 *)
ref_suffix   = ( "*" | "@" ) [ "own" ] [ "mut" ] ;             (* pointer, span D3.5 *)
array_suffix = "[" const_expr "]" [ "mut" ] ;                   (* fixed array, D3.4 *)
base_type    = prim_type | "string" | "void" | fn_type | qualified_name ;
prim_type    = "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64"
             | "f32" | "f64" | "bool" | "char" ;
fn_type      = "fn" return_type "(" [ type { "," type } ] ")" ;      (* D3.10 *)
qualified_name = identifier [ "." identifier ] ;                    (* D9.4 *)
```

Reading rules (D3.6, D5.2, D5.3):

- A reference suffix (`*` pointer, `@` span) applies to everything to its left, so they read
  inside-out: `node*@` is a span of pointers, `u8@*` a pointer to a span, `u8@@` a span of spans.
- Fixed-array suffixes form one group and read outside-in like C: `i32[3][4]` is three arrays of
  four. Reference suffixes before the group make arrays of references (`node*[16]`, `node@[4]`);
  after it, references to the whole array (`i32[4]*`, `i32[4]@` is a span of `i32[4]`). No array
  suffix may follow a trailing reference suffix (`i32[4]*[2]` does not parse; wrap it in a struct).
- `void` is legal as a `base_type` only when followed by at least one `*` (D3.11).
- A `fn_type` used as `base_type` may take suffixes: `fn i32(i32)[4]` is four function pointers.
- A `mut` marks the storage of what it follows: after the base type, values of that type; after
  a `*` or `@`, the pointer or span header that suffix introduces (the storage holding it); after
  `[N]`, the array, whose elements share its storage, so a `mut` between an element type and its
  `[N]` is an error. Nothing precedes the base type. The outermost position is the binding:
  `i32 mut x`, `node* mut p`, `u8@ mut s` (D5.3).
- An `own` follows a `*` or an `@` and marks the reference that suffix introduces as owning its
  target; after the base type it is legal only for `string`, the reference without a suffix
  (`string own s`). It precedes `mut` in a position (`node* own mut p`), never follows a
  fixed-array suffix, and inside `new(...)` parses only after a `*` of the element type (D17.2,
  D17.3).
- In a `fn_type`, a `mut` in the outermost position of a parameter type is ignored for type
  identity (D5.6).

## 5. Statements

```ebnf
block        = "{" { statement } "}" ;

statement    = var_decl
             | assign_stmt
             | incdec_stmt
             | call_stmt
             | if_stmt
             | while_stmt
             | do_stmt
             | for_stmt
             | range_for_stmt
             | switch_stmt
             | defer_stmt
             | return_stmt
             | "break" ";"
             | "continue" ";"
             | block ;

assign_stmt  = assign_head ";" ;                                     (* D7.2 *)
assign_head  = lvalue_expr assign_op expr ;
assign_op    = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "+%=" | "-%=" | "*%="
             | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
incdec_stmt  = incdec_head ";" ;
incdec_head  = lvalue_expr ( "++" | "--" ) ;
call_stmt    = call_expr ";" ;                                       (* D7.3 *)

if_stmt      = "if" "(" expr ")" block { "else" "if" "(" expr ")" block } [ "else" block ] ;
while_stmt   = "while" "(" expr ")" block ;
do_stmt      = "do" block "while" "(" expr ")" ";" ;
for_stmt     = "for" "(" [ for_init ] ";" [ expr ] ";" [ for_step ] ")" block ;   (* D7.5 *)
for_init     = var_decl_head | assign_head | call_expr ;
for_step     = assign_head | incdec_head | call_expr ;
range_for_stmt = "for" "(" type identifier ":" expr ")" block ;      (* D7.5 *)

switch_stmt  = "switch" "(" expr ")" "{" { case_clause } "}" ;       (* D7.6 *)
case_clause  = ( "case" const_expr { "," const_expr } | "default" ) ":" { statement } ;

defer_stmt   = "defer" ( assign_stmt | incdec_stmt | call_stmt | block ) ;   (* D7.8 *)
return_stmt  = "return" [ expr ] ";" ;                               (* D7.11 *)
```

`lvalue_expr` is syntactically a `postfix_expr` or a unary `*` expression; the type checker
enforces D6.7. `call_expr` is a `postfix_expr` whose last postfix is a call. `const_expr` is an
`expr` that the checker requires to be constant (D4.6).

## 6. Expressions

Precedence from lowest to highest (D6.1). All binary operators are left-associative; `?:` is
right-associative.

```ebnf
expr         = ternary_expr ;
ternary_expr = or_expr [ "?" expr ":" ternary_expr ] ;
or_expr      = and_expr { "||" and_expr } ;
and_expr     = bitor_expr { "&&" bitor_expr } ;
bitor_expr   = bitxor_expr { "|" bitxor_expr } ;
bitxor_expr  = bitand_expr { "^" bitand_expr } ;
bitand_expr  = eq_expr { "&" eq_expr } ;
eq_expr      = rel_expr { ( "==" | "!=" ) rel_expr } ;
rel_expr     = shift_expr { ( "<" | "<=" | ">" | ">=" ) shift_expr } ;
shift_expr   = add_expr { ( "<<" | ">>" ) add_expr } ;
add_expr     = mul_expr { ( "+" | "-" | "+%" | "-%" ) mul_expr } ;
mul_expr     = unary_expr { ( "*" | "/" | "%" | "*%" ) unary_expr } ;
unary_expr   = ( "!" | "~" | "-" | "*" | "&" ) unary_expr
             | postfix_expr ;
postfix_expr = primary_expr { postfix } ;
postfix      = "(" [ arg_list ] ")"                                   (* call *)
             | "[" expr "]"                                           (* index, D6.8 *)
             | "[" [ expr ] ".." [ expr ] "]"                         (* span, D6.9 *)
             | "." identifier                                         (* field, .len, .ptr *)
             | "->" identifier ;                                      (* D6.10 *)
arg_list     = expr { "," expr } ;

primary_expr = int_literal | float_literal | char_literal | string_literal
             | "true" | "false" | "null"
             | identifier
             | "(" expr ")"
             | struct_literal
             | array_literal
             | cast_expr
             | sizeof_expr
             | new_expr ;

struct_literal = qualified_name brace_init ;                          (* D6.5 *)
array_literal  = array_type brace_init ;                              (* D6.5 *)
array_type     = base_type { ref_suffix } "[" const_expr "]" { "[" const_expr "]" } ;
cast_expr      = "cast" "(" expr "," type ")" ;                       (* D6.4 *)
sizeof_expr    = "sizeof" "(" type ")" ;                              (* D3.15 *)
new_expr       = "new" "(" alloc_type [ "," expr ] ")" ;              (* D10.2 *)
alloc_type     = base_type { "*" [ "own" ] } { "[" const_expr "]" } ;
```

Notes:

- A `qualified_name` in `primary_expr` position is parsed as `identifier` followed by the `.`
  postfix; the checker resolves module, type and enum qualification (D9.4, D3.9).
- Comparison operators do not chain: `a < b < c` parses but is a type error (`bool < T`).
- `-x` on an unsigned type, and `!`/`~` on the wrong types, are type errors, not parse errors.
- `new(T)` allocates one `T` and `new(T, n)` allocates `n` of them as a span; the brackets in
  an `alloc_type` are fixed-array dimensions of `T` (`new(i32[4], n)` yields `i32[4] mut@ own`).
  `mut` does not parse inside `new(...)`, and `own` only after a `*` of the element type
  (`new(node* own, n)`, D17.3); the result is writable at every level and owned (D5.8, D17.3).
- An `array_literal` type has only fixed dimensions and no trailing reference suffix:
  `i32[3]@{...}` and `i32[3]*{...}` do not parse.

## 7. Disambiguation

The grammar has three places where a lookahead of one token is not enough. All are resolved by a
speculative parse over the token array (rewind on failure); none require symbol-table knowledge.

1. **Declaration versus statement** (D7.1). At the start of a statement:
   - a keyword among `if while do for switch defer return break continue` or `{` starts that
     statement;
   - a `prim_type`, `string`, or `fn` starts a declaration;
   - otherwise, speculatively parse a `type`; if the next token is then an identifier, the
     statement is a `var_decl`; else rewind and parse `assign_stmt | incdec_stmt | call_stmt`.
   Because expression statements are calls only (D7.3), `a * b;` never has to be parsed, and
   `foo[3] = x;` (assignment) versus `foo[3] arr = {};` (declaration) is settled by the token
   after the type. Since D5.3 puts every `mut` and `own` after what it qualifies, a statement
   never begins with one; `foo@ mut x;` and `foo* own p;` are settled by the same speculative
   parse.
2. **Struct and array literals versus expressions** (D6.5). In `primary_expr` position, an
   `identifier` (optionally `. identifier`) directly followed by `{` is a `struct_literal`; a
   successful speculative parse of `array_type` directly followed by `{` is an `array_literal`.
   `{` never follows a complete expression anywhere else in the grammar, because every
   control-flow condition is parenthesized and every body is braced.
3. **`for` forms** (D7.5). After `for (`, speculatively parse `type identifier`; if the next token
   is `:` the loop is a `range_for_stmt`; if it is `=` the loop is a `for_stmt` whose `for_init`
   is a declaration; otherwise rewind and parse `for_init` as an assignment, a call, or empty.

At the top level the first token decides: `import`, `fn`, `extern`, `struct`, `enum`, or a type
(a `global_decl`). A `fn` at statement level always begins a declaration whose type is a
`fn_type` (`fn i32(i32) op = add;`); function definitions are top-level only (D8.3).

A parse that fails does not stop the file: the parser reports the error, skips to the next
statement, clause, field or declaration boundary and parses on, so a file reports one diagnostic
for each construct that failed (D14.2). The recovery points and what a skip consumes are in
`toolchain.md` 4. Recovery adds one rule to the grammar above: a `block`, a `case_clause`, a
`struct_decl` body and an `enum_decl` body also end where a top-level declaration starts, so
that a missing `}` is reported once rather than once per following declaration, and the `{` of a
`fn_decl` body, a `struct_decl` body or an `enum_decl` body may be missing without the body
ceasing to be one. Which of the two a `fn` starts is the same speculative parse as case 1: a
return type followed by an identifier is a `fn_decl`, a return type followed by `(` is a
`fn_type` (D3.10). The skipped tokens are no production of this grammar; the tree holds them as
an error node (D14.2).

## 8. Grammar-to-decision index

| Construct              | Decisions                         |
|------------------------|-----------------------------------|
| Lexical elements       | D2.1 to D2.11                     |
| Imports                | D9.1 to D9.4                      |
| Functions and `extern` | D8.1, D8.5, D8.6, D9.8            |
| Structs and enums      | D3.8, D3.9                        |
| Declarations           | D7.1, D7.10, D6.5                 |
| Types                  | D3.1 to D3.12, D5.2, D5.3, D17.2  |
| Statements             | D7.2 to D7.8, D7.11               |
| Expressions            | D6.1 to D6.13, D3.14, D3.15, D10.2 |
