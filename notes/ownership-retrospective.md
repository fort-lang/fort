# Why the ownership wave failed

The ownership wave ran on `main` from 2026-10-02 to 2026-10-18. It tried to prove the ownership
rules of D17.13 to D17.19 over the whole compiler, with no annotations. On 2026-10-22 the user
cancelled it: "This effort has been a disaster. [...] Let's start fresh." The user also ruled that
the wave commits are reverted. This file gives 6 reasons why the wave failed (sections 1 to 6)
and 2 lessons: what worked (section 7) and the rules for the next design (section 8). Each point
gives its measurement.

## 0. How to read and reproduce this file

The tag `ownership-wave-final` (d9595ac4, on `origin`) holds the tree before the revert. Each
command below reads that tag, GitHub, or the ticket board. Run the git commands in a clone of the
repository. Run the board commands in `.tickets/` of the main checkout; the board is not checked
in. The audit commands read the CI artifact of the merge-group run 39145343116, which tested
d9595ac4. GitHub keeps that artifact until 2027-01-16.

```sh
T=ownership-wave-final
gh run download 39145343116 -R fort-lang/fort -n ownership-source-ubuntu-latest -D art
A=art/Linux/release/ownership-source-evidence/audit   # report-0000.json is src/fort/main.ft
```

Each measurement says how it was established (`AGENTS.md`, Tickets):

- `measured`: a command ran on the tag, on GitHub, on the artifact or on the board. The command
  is here.
- `read`: the value comes from the text of a ticket or a ledger. No command counted it.

## 1. The goal could not be reached by inference

The rules asked for complete proof of the whole closure. D17.14 rejects an operation when the
analysis cannot prove its obligation. D17.14 and D17.19 add no annotations and no contract syntax.
D17.18 makes a work-budget failure an error. So the compiler had to prove each body of its own
closure from the source alone.

On d9595ac4 the closure of `src/fort/main.ft` holds 3,954 bodies (`measured`). The audit proves
0 of them completely, so the verdict is `rejected` and the exit status is 1. Each analysis proves
a different part:

| analysis | bodies with complete proof | share of 3,954 |
|---|---:|---:|
| `local` | 712 | 18 % |
| `stored_borrows` | 785 | 20 % |
| `raw` | 1,037 | 26 % |
| `calls_heap` | 196 | 5 % |
| `process_exit` | 3,634 | 92 % |
| all analyses together | 0 | 0 % |

```sh
python3 -c '
import json, sys
r = json.load(open(sys.argv[1]))
print(r["invocation"]["entry"], r["totals"]["bodies"], r["verdict"], r["exit_status"])
print("all", r["totals"]["ownership"]["complete"])
for a in r["totals"]["analyses"]:
    print(a["name"], a["proof"]["complete"])' $A/report-0000.json
```

A checkout of the tag gives the same report without the artifact. The CI step ran this command
with the compiler of the tag:
`fort --check --ownership-check --ownership-report r.json --target x86_64-linux-gnu -I src
src/fort/main.ft`.

## 2. Six analyses with six abstractions

The wave built six analyses and one checker, each with its own model of storage: local, stored
borrow, raw, process exit, the summary solver, the heap solver, and the witness checker. The
`calls_heap` row of the report joins the summary and heap solvers. The table of section 1 shows
that the analyses prove different body sets of one closure.

Lines of each group on the tag (`measured`, by file name):

| group | modules | lines |
|---|---|---:|
| local | `ownership_source_local` | 4,310 |
| stored borrow | `ownership_borrows` | 745 |
| raw | `ownership_raw*`, `ownership_source_raw` | 9,277 |
| process exit | `ownership_source_exit`, `ownership_globals` | 4,989 |
| summary solver | `ownership_summary*` | 7,094 |
| heap solver | `ownership_heap` | 7,278 |
| witness checker | `ownership_witness` | 2,166 |
| shared services | the other 25 `ownership_*` modules | 25,496 |

```sh
lines() { git ls-tree --name-only $T src/fort/ | grep -E "/ownership_($1)\.ft$" \
  | sed "s|^|$T:|" | xargs git show | wc -l; }
lines 'source_local'; lines 'borrows'; lines 'raw.*|source_raw'
lines 'source_exit|globals'; lines 'summary.*'; lines 'heap'; lines 'witness'
```

The analyses disagreed at their edges, and reviews found each disagreement as one shape
(`read`, the class text of `test/ownership/solvers/false_accepts.txt` on the tag). Two examples:
the heap solver alone proves each class P body, and the summary solver and `calls_heap` prove
none; the summary solver fails 11 of the 12 class N bodies that the heap solver proves.

## 3. Soundness by enumeration

Reviews found false accepts. The design did not prevent them. The ledger
`test/ownership/solvers/false_accepts.txt` counts the faulty bodies that one solver alone proves.
It changed in 14 commits, and each commit set a new bound (`measured`, by the date of the commit
on `main`):

| date | bounds, in commit order |
|---|---|
| 2026-10-11 | 13, 16, 14, 9 |
| 2026-10-12 | 12, 5 |
| 2026-10-13 | 3 |
| 2026-10-16 | 0, 12, 18 |
| 2026-10-18 | 9, 12, 3, 19 |

```sh
f=test/ownership/solvers/false_accepts.txt
for c in $(git log --reverse --format=%h $T -- $f); do
  echo "$c $(git log -1 --format=%cd --date=short $c) $(git show $c:$f | grep -oE 'at most [0-9]+')"
done
git log -p $T -- $f | grep -oE '^\+# [A-Z]  ' | sort -u | tr -d '+# ' | tr '\n' ' '
```

The bound reached 0 on 2026-10-16 (75182c99). The next reviews then found new classes, and the
bound rose to 19 on the tag. The ledger defines 15 classes (A, B, D to I, K to Q). Two more
classes, R (16 bodies) and T (4 bodies), were found on 2026-10-18 and did not reach `main`
(`read`, the retired series plan in `.tickets/retired/ownership-wave-2026-10-22/`).

The review rounds of PR 146 show the pattern (`read`, its ticket in the same directory). The PR
checked the copy of a loaded caller reference at a heap call. Round 1 found a join of two caller
slots with no target (3 faulty bodies proved) and copies of other loaded inputs (7 faulty callers
proved). Round 2 found a pointer cast that moves a joined input with no copy check (3 bodies
proved). The final review found a slice of a stored input into callee storage (4 bodies proved).
So the routes were copy, then join, then cast, then slice. Each fix closed one route, and the next
review found the next route. Each finding was already on `main`.

No reference semantics and no runtime oracle existed to test a proof against. The oracle (a
sanitizer run over each probe) depended on default enablement, the last step of the series
(`read`). Three summary-rule PRs closed without merge after their cuts: PR 126, PR 132 and PR 134
(`measured`: `gh pr view <n> -R fort-lang/fort --json state,closedAt`).

## 4. Size and cost

The 39 `ownership_*` modules hold 61,355 lines. The other 31 modules of `src/fort` hold 35,734
lines, and FIR (the 8 `fir*` modules) holds 12,314 of them (`measured`). On the tag, 702 test
and tool files whose path names `ownership` hold 156,251 lines. Before the wave, 145 such files
held 2,582 lines.

```sh
git ls-tree --name-only $T src/fort/ | grep '/ownership_' | sed "s|^|$T:|" | xargs git show | wc -l
git ls-tree --name-only $T src/fort/ | grep '\.ft$' | grep -v '/ownership_' | sed "s|^|$T:|" \
  | xargs git show | wc -l
for r in 4c0375ef~1 $T; do git ls-tree -r --name-only $r test tools | grep -i ownership \
  | sed "s|^|$r:|" | xargs git show | wc -l; done
```

From 4c0375ef ("Freeze ownership API and verified test inputs", 2026-10-02) to the tag, `main`
took 226 commits, and 138 of them changed `src/fort/ownership_*` (`measured`):

```sh
git rev-list --count 4c0375ef~1..$T
git rev-list --count 4c0375ef~1..$T -- 'src/fort/ownership_*'
```

Nothing cached an analysis result. The CI audit ran 70 roots, and each root analysed its whole
closure again: 80,127 bodies for 4,319 unique bodies, 18.6 times each (`measured`). The summary
and heap solvers did 91.4 % of the counted work (W): `calls_heap` 51.4 % and `calls_summary`
40.0 %.

```sh
python3 -c '
import collections, glob, json, sys
n, keys, work = 0, set(), collections.Counter()
for path in glob.glob(sys.argv[1] + "/report-*.json"):
    r = json.load(open(path))
    for b in r["bodies"]:
        n += 1
        keys.add((b["source"]["module"], b["source"]["name"], b["source"].get("instance", 0)))
    for m in r["meters"]:
        work[m["name"]] += sum(c["used"] for c in m["counts"] if c["category"] == "W")
total = sum(work.values())
print(n, len(keys), round(n / len(keys), 1))
for name, w in work.most_common(2):
    print(name, round(100 * w / total, 1))' $A
```

The CI step "Audit actual compiler source reports" grew from about 1 minute to 37.9 minutes
(`measured`, minutes on the slowest host of each merge-group run):

| date | run | head | slowest host | minutes |
|---|---|---|---|---:|
| 2026-10-07 | 37697382252 | 121f42f3 | macos-latest | 1.1 |
| 2026-10-12 | 38257854847 | 36f1970a | ubuntu-latest | 7.3 |
| 2026-10-13 | 38427569240 | 3983778f | ubuntu-latest | 36.2 |
| 2026-10-18 | 39122735140 | 0d995df2 | ubuntu-24.04 | 37.9 |
| 2026-10-18 | 39145343116 | d9595ac4 | ubuntu-latest | 12.4 |

The last row is after e2147be1, which runs the 70 roots in parallel. That change cut the wall
time, not the work.

```sh
RUN=38427569240   # a run of the table
gh api "repos/fort-lang/fort/actions/runs/$RUN/jobs?per_page=100" --jq '.jobs[] | .name as $j
  | .steps[] | select(.name == "Audit actual compiler source reports")
  | "\($j) \(((.completed_at | fromdate) - (.started_at | fromdate)) / 6 | round / 10)"'
```

The size also hid the structure. The wave added 39 flat modules with one prefix and no directory,
such as `ownership_summary_body.ft` and `ownership_source_calls.ft`. The user had ruled on
2026-10-07 that a module name is a noun and that modules nest in directories (`read`, the
ticket board).

## 5. The rules were written while the code was written

Six commits rewrote D17.14 and wrote D17.15 to D17.19. They reached `main` between 00:31 and
04:13 (UTC-7) on 2026-10-02 (77cb393b to 32693aa0). The first code commit, 4c0375ef, reached
`main` at 08:26 on the same day (`measured`, committer times). D17.13 existed before. The
reviews then changed the rules again and again: 11 commits of the wave changed D17.13 to D17.19,
and these entries hold 11 `Amended` notes dated 2026-10-06 to 2026-10-17. D17.18 grew from 26
lines to 196 lines.

```sh
git log --format='%h %cd %s' --date=iso 77cb393b~1..4c0375ef -- spec/decisions.md
git log -1 --format='%h %cd' --date=iso 4c0375ef
git log --format='C %h' -L '/^### D17.13 /,/^### D18.1 /:spec/decisions.md' 4c0375ef~1..$T \
  | grep -c '^C '
git show $T:spec/decisions.md | sed -n '/^### D17.13 /,/^### D18.1 /p' \
  | grep -cE 'Amended 2026-10-(0[3-9]|[12][0-9])'
for r in 4c0375ef~1 $T; do
  git show $r:spec/decisions.md | awk '/^### /{p=($2=="D17.18")} p' | wc -l
done
```

Rulings arrived late in review (`read`, the retired directory
`.tickets/retired/calls-witness-2026-10-10/`). PR 103 closed after 8 review rounds and 58
findings. Its review round 7 ratified the readings of a user ruling that the implementation
already depended on. The retired series plan records 5 user rulings of the form "ledger + new
ticket": a review finds a class, the ledger takes it, and a new ticket fixes it later.

## 6. Process

The series manifest lists 114 tickets. 73 are done and 41 were open at the cancellation. The
retired directory holds 49 tickets: the 41 open series tickets and 8 others (`measured`, the
board commands below).

The gap between tickets filed and PRs merged grew from 4 to 35 (`measured`: the board for
tickets filed, GitHub for PRs merged, all subjects):

| days | tickets filed | PRs merged |
|---|---:|---:|
| 2026-10-02 to 2026-10-09 | 103 | 99 |
| 2026-10-10 to 2026-10-18 | 70 | 35 |
| 2026-10-19 to 2026-10-22 | 15 | 0 |

```sh
ls retired/ownership-wave-2026-10-22/T*.md | wc -l    # in .tickets/
for w in '0[2-9]' '1[0-8]' '(19|2[0-2])'; do
  find todo inprogress inreview done retired -name 'T*.md' \
    | xargs grep -lE "^- created: 2026-10-$w" | wc -l
done
gh pr list -R fort-lang/fort --state merged --search 'merged:2026-10-10..2026-10-18' \
  --limit 200 --json number --jq length
```

The soundness PRs changed the same count files, so parallel PRs waited for each other's rebases.
In the commit range of section 4 (`measured`):

| file | commits |
|---|---:|
| `test/ownership/solvers/faulty.json` | 11 |
| `test/ownership/solvers/false_accepts.txt` (the bound) | 14 |
| `test/ownership/heap_rows.txt` (the golden rows) | 15 |
| `notes/testing.md` (its counts) | 48 |

29 of the 138 commits that changed `src/fort/ownership_*` also changed one of these four files.

```sh
R=4c0375ef~1..$T
S="test/ownership/solvers/faulty.json test/ownership/solvers/false_accepts.txt"
S="$S test/ownership/heap_rows.txt notes/testing.md"
for f in $S; do echo "$f $(git rev-list --count $R -- $f)"; done
comm -12 <(git rev-list $R -- 'src/fort/ownership_*' | sort) \
  <(git rev-list $R -- $S | sort) | wc -l
```

Review rounds went past the limit of `AGENTS.md` (Review Workflow: split a ticket at round 3).
7 of the 114 series tickets went past review round 3, and one
reached round 10 (`measured`: the command greps the ticket text, so it misses a round that no
ticket names). PR 103, outside the manifest, ran 8 rounds (`read`).

```sh
# in .tickets/
python3 -c '
import collections, glob, json, os, re
ids = {t["id"] for t in json.load(open("retired/ownership-wave-2026-10-22/series.json"))}
done, rounds = set(), collections.Counter()
for path in glob.glob("*/T*.md") + glob.glob("retired/*/T*.md"):
    n = int(os.path.basename(path)[1:].split("-")[0])
    if n in ids:
        done |= {n} if path.startswith("done/") else set()
        found = re.findall(r"(?:rev-[a-z]+|[Rr]eview) round ([0-9]+)", open(path).read())
        rounds[n] = max([rounds[n]] + [int(x) for x in found])
print(len(ids), len(done), len(ids - done))
print(sum(1 for r in rounds.values() if r > 3), max(rounds.values()))'
```

## 7. What worked and stays

Each value below is `measured` on the tag; the commands follow the list.

- FIR and its verifier: 12,314 lines in 8 `fir*` modules, merged before the wave (f9072d9e and
  a09ec049, 2026-09-29). The revert keeps them.
- `src/fort/fir_flow.ft`: a forward dataflow over FIR blocks with bit sets and a loop fixpoint,
  382 lines (401e47c3). The static overwrite check of the next plan reuses it.
- The counted false-accept set as a review method. It made each class visible as a number, and it
  showed the bound go from 0 back to 19 (section 3).
- Probes before fixes: 67 probe files in `test/ownership/probes/` on the tag. Each of the 19
  ledger entries on the tag names a probe body in that directory.
- Mutation rows: 864 rows in 15 tables `tools/mutations/ownership_*.json` on the tag.
- The merge queue (`tools/queue_pr.sh`, 7b6ec3f5) and the parallel audit (e2147be1).

```sh
git ls-tree --name-only $T test/ownership/probes/ | grep -c '\.ft$'
git show $T:test/ownership/solvers/false_accepts.txt | grep -c '^test/ownership/probes/'
git ls-tree --name-only $T tools/mutations/ | grep '/ownership_' | sed "s|^|$T:|" \
  | while read b; do git show "$b" | python3 -c \
    'import json, sys; print(len(json.load(sys.stdin)["mutations"]))'; done | paste -sd+ - | bc
```

## 8. Rules for the next design

1. Write a semantic model before code: an executable reference semantics that reports leak,
   double release, use after release or move, and overwrite at run time. Test each rule against
   it from the first rule on (sections 1 and 3).
2. Build one analysis, not six, on FIR and `fir_flow` (section 2).
3. Put contracts or summaries at module and library boundaries. Do not infer the whole program
   (section 1).
4. Set a size budget for the analysis and a CI time bound for each rule (section 4).
5. Compute each summary once for each library, not once for each root (section 4).
6. Write the rules first and get the rulings before the code. Add one rule for each PR, each as a
   compile error, with a migration count first (section 5).
7. Keep each count in the PR's own test or ticket, not in a shared file (section 6).
8. Name each module with a noun, and nest a subsystem in a directory, for example
   `src/fort/fir/emptiness.ft` (section 4).
