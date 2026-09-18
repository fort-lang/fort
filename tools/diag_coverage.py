#!/usr/bin/env python3
"""Which diagnostics of a ported pass does no test/fort suite assert?

The report compares diagnostics in one bootstrap C pass with a fort module.
It then shows diagnostics that no test/fort suite asserts.
Use the report before a change claims complete diagnostic coverage.

A diagnostic is one `check_error(ck, loc, "...")` or one run of `msg_str`
pieces between `check_msg_begin` and `check_msg_end`/`check_note_end`; two
sites that build the same message are one diagnostic. It counts as asserted
when a file under test/fort holds a piece of it, or a 22-character prefix of
one, that belongs to that message alone -- a hint such as ": write move(" is
shared by two messages and is evidence for neither.

The result is a list of candidates and not a verdict: a message composed from
pieces the suites assert only in combination ("<what> expects <T>, not <U>")
is a false positive, because the finished text exists nowhere in the source.
Verify each reported candidate by hand. The tool is not a ctest because its
output needs this review.

Usage: diag_coverage.py [src/fort/<module>.ft], from the top of the worktree.
"""

import glob
import re
import sys
from collections import Counter

PREFIX = 22
source = sys.argv[1] if len(sys.argv) > 1 else 'src/fort/check.ft'
lines = open(source).read().split('\n')
tests = ''
for p in sorted(glob.glob('test/fort/*.ft')):
    tests += open(p).read()

def held(text):
    if len(text) < 6:
        return False
    if text in tests:
        return True
    return len(text) >= PREFIX and text[:PREFIX] in tests

sites = []
open_at, pieces = None, []
for i, ln in enumerate(lines, 1):
    if 'check_msg_begin(ck)' in ln:
        open_at, pieces = i, []
        continue
    m = re.search(r'check_error\(ck, [^,]+, ("(?:[^"\\]|\\.)*")\)', ln)
    if m:
        sites.append((i, [m.group(1)[1:-1]]))
        continue
    for m in re.finditer(r'msg_str\(ck, ("(?:[^"\\]|\\.)*")\)', ln):
        pieces.append(m.group(1)[1:-1])
    if ('check_msg_end(ck' in ln or 'check_note_end(ck' in ln) and open_at is not None:
        if pieces:
            sites.append((open_at, pieces))
        open_at, pieces = None, []

msgs = {}
for l, ps in sites:
    msgs.setdefault(tuple(ps), []).append(l)

freq = Counter()
for key in msgs:
    for p in set(key):
        freq[p] += 1

missing = []
for key, at in sorted(msgs.items(), key=lambda kv: kv[1][0]):
    if any(freq[p] == 1 and held(p) for p in key):
        continue
    missing.append((at, key))
for at, key in missing:
    print(f'{",".join(str(x) for x in at)}: {" | ".join(key)}')
print(f'--- {source}: {len(msgs)} distinct diagnostics, '
      f'{len(missing)} with no test under test/fort')
