# Fixture decision log for test/highlight_test.py

Not the real decision log: this file is a copy of the D2.4 and D2.10 bullets with one keyword
(`effect`) and one operator (`<->`) added, so the drift test can prove that a token added to the
decisions and not to the grammar is caught without anyone editing `notes/decisions.md`.

## D2 Lexical structure

- **D2.3** Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, case-sensitive, no length limit.
- **D2.4** Keywords:
  `as bool break case cast char continue default defer do effect else enum extern f32 f64 false fn
  for i8 i16 i32 i64 if import mut new noreturn null own return sizeof string struct switch true u8
  u16 u32 u64 void while`.
  Reserved for future use, not usable as identifiers:
  `async await const match pub priv trait type union yield`.
- **D2.10** Operators and punctuation:
  `+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>= == != < <= > >= && || ! & | ^ ~
  << >> ++ -- ? : :: . -> .. <-> ( ) [ ] { } , ; @`. Longest match wins.
- **D2.11** Nesting deeper than 256 is a compile error.
