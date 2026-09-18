#!/usr/bin/env python3
"""Report diagnostics of a ported pass that no test/fort suite asserts.

The report compares diagnostics in one bootstrap C pass with a fort module.
Use the report before a change claims complete diagnostic coverage.

A diagnostic is one `check_error(ck, loc, "...")`. It can also be one run of
`msg_str` pieces between its begin and end calls. Two sites that build the same
message count as one diagnostic. A test asserts it when a file under test/fort
contains its unique piece or 22-character prefix. A shared hint, such as
": write move(", is evidence for neither message.

The result is a candidate list, not a verdict. A message can combine pieces
that suites assert only together, such as "<what> expects <T>, not <U>".
Such a message is a false positive because its full text is not in the source.
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
