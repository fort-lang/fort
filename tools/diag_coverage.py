#!/usr/bin/env python3
"""Which diagnostics of a ported pass does no test/fort suite assert?

Phase B ports a C file of src/bootstrap into src/fort, and its test suite is
written beside it rather than translated from the C one: T-035's review found
22 diagnostics of src/fort/check.ft with no fort test, 11 of them asserted by
the C suites the ticket was porting, because only 6 of its 85 test-function
names matched a C TEST name. Nothing mechanical had held the two together.
This is that mechanism, and it is meant to be run before a ticket claims any
coverage universal.

A diagnostic is one `check_error(ck, loc, "...")` or one run of `msg_str`
pieces between `check_msg_begin` and `check_msg_end`/`check_note_end`; two
sites that build the same message are one diagnostic. It counts as asserted
when a file under test/fort holds a piece of it, or a 22-character prefix of
one, that belongs to that message alone -- a hint such as ": write move(" is
shared by two messages and is evidence for neither.

The result is a list of candidates and not a verdict: a message composed from
pieces the suites assert only in combination ("<what> expects <T>, not <U>")
is a false positive, because the finished text exists nowhere in the source.
Verify what it reports by hand and write the measured list into the ticket.
This is why it is a tool a ticket runs and not a ctest.

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
