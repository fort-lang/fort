# fort environment: the VM, the build and the shared folder

This document holds every fact about the machine the project builds on: the Vagrant VM, the
`tools/vm` wrapper, the VirtualBox shared folder, the cross toolchain, and the CMake presets and
targets. It is not normative about the language; `spec/decisions.md` and `spec/grammar.md` win
over it (D1.2).

T-098 created the headings below. T-099 moved into them the `## Environment` section of
`AGENTS.md` and the build half of its `## Build and test` section, one bullet at a time and
without a rewrite.

## 1. The VM

- The Linux target builds and runs inside the Ubuntu 24.04 arm64 Vagrant VM defined by `Vagrantfile`
  (VirtualBox, `bento/ubuntu-24.04`). The Mac target builds and runs on the Mac host.
  Run vagrant only through
  `tools/vm`: `up` creates, provisions and starts the VM and caches its ssh config; `halt` stops
  it; `destroy [-f]` removes it (the box stays installed); `provision` re-runs
  `tools/provision.sh`; `status` and `ssh` do what they say.
- The VM directory (whose `Vagrantfile` and `.vagrant/`, or `.vagrant-<slot>/`, are used) is
  `$FORT_VM_DIR` if set,
  otherwise the main checkout of the current repository, so every worktree shares one VM. It is
  `/vagrant` in the guest and worktrees are `/vagrant/.worktrees/<name>`. The VirtualBox machine
  is named `fort-dev-<directory name>` (`fort-dev-fort` for the main checkout, `fort-dev-fort-2`
  for slot 2), so a VM brought
  up from another directory does not collide with it; destroy one before bringing up the other
  if memory is tight. `FORT_VM_CPUS` (default 6) and `FORT_VM_MEMORY` (MiB, default 8192) size
  the VM at `up`.
- `tools/vm run <cmd>` executes in the guest directory matching the host cwd, which must lie
  inside the VM directory (`ssh` and the cmake subcommands too; the lifecycle subcommands work
  from anywhere inside the repository, or anywhere with `FORT_VM_DIR` set). The cmake
  subcommands (`configure`, `build`, `test`, `workflow`, the targets below and `gate`) run at
  the top of the host git worktree containing the cwd and default to the `debug` preset. Every
  guest command sources `/etc/profile.d/fort.sh` and disables core dumps.
- **A per-ticket VM (`FORT_VM_DIR="$PWD"` inside a worktree) is retired; use a slot instead**
  (T-114, below). It is kept here as history because its measurements still hold: on 2026-09-12,
  on a host with 10 CPUs and 32 GiB, `FORT_VM_DIR="$PWD" FORT_VM_CPUS=4 FORT_VM_MEMORY=8192
  tools/vm up` created, provisioned and booted in **99 s**, and one preset from cold took
  **6 m 22 s** on 4 CPUs, against 22 to 30 minutes for a three-preset gate on a shared 6-CPU VM
  **because four agents were queuing on it**. What retired it: it mounted one worktree at
  `/vagrant`, so `git` did not work in the guest and every absolute path in the CMake cache was
  bound to that mapping -- switching later meant deleting `build/` -- and T-101's was left running
  idle for hours after its ticket merged. A slot keeps the speed and loses all three costs.
- **What one shared VM cost on 2026-09-12**, so the trade is on the record: four agents gating at
  once drove the load to 19 on 6 CPUs, produced two false red gates that each cost an hour of
  diagnosis, and made one agent run `pkill -f ctest` in a machine three other worktrees were
  using. Every one of those is a contention failure and not a code failure.
- When the Mac sleeps, VirtualBox pauses the VM ("paused due to host power management") and
  `tools/vm status` shows `paused`; guest commands then fail after the 10 s ssh timeout and
  `tools/vm up` cannot resume it. Recover with `VBoxManage controlvm fort-dev-<name> savestate`
  followed by `tools/vm up`. **A gate that is already connected does not fail: it stops.** T-064
  met it mid-gate on 2026-09-13. The log had not advanced for 42 minutes while `ps` showed
  `/bin/bash tools/vm gate` alive at 46 minutes, which looks like a slow test and is not one.
  `VBoxManage controlvm <name> resume` refuses such a VM with `VM is paused due to host power
  management`, so savestate is the only way back. Order: `kill -TERM` the process whose command
  *is* the gate (it exits 143 and releases its hold on the worktree), then savestate, then
  `tools/vm up`, then start the gate again. A log that stops advancing is worth one
  `tools/vm status` before any other diagnosis, and the other slot keeps running throughout,
  since the pause is one machine's.

- **A wall-clock measurement in the guest can hold a VirtualBox pause, and the pause reads as a
  slow test.** T-094 measured `test/fort/driver_lifetime_index_test.ft` at 1449.81 s and at
  1862.02 s under `/usr/bin/time`, and the test passed both times. `uptime` said `up 41 min`
  before and `up 42 min` after the run whose wall clock advanced 31 minutes, so the VM was paused
  for 30 of them. The guest monotonic clock stops while the VM is paused: `run_tests.py` times a
  step inside `subprocess.communicate(timeout=...)`, which Python measures on the monotonic clock,
  so no step timed out, and `/usr/bin/time` reads the wall clock that the host corrects on resume,
  so it counted the pause. Print `uptime` before and after any measurement
  you will quote, and compare the two "up N min" values with the wall time. The same command at
  the same guest load measured 35.33 s (T-094).
- **Before you call a failure contention, look for your own abandoned processes.** A dedicated VM
  stops one agent competing with another. It does nothing about an agent competing with itself,
  and that is what a whole afternoon of "contention from other worktrees" was on 2026-09-12. Two
  shapes. A background waiter loop outlives the thing it waits for: it builds nothing, and it
  polls on a timer for as long as it lives. T-041 found **three of its own**, still spinning after
  nine hours. An orphaned `tools/vm gate` keeps driving a ninja in the guest: one ran 87 minutes
  against a normal 23, with parent PID 1. The two look the same from the outside as the failure
  they cause, which is a slow gate or a test that times out. The check is two commands:

      ps -eo pid,etime,command | awk '$3=="/bin/bash" && $4 ~ /tools\/vm$/ && $5=="gate"'
      lsof -a -p <pid> -d cwd        # names the worktree it belongs to

  **Match the process whose command *is* the gate, not every line that mentions it.** A plain
  `grep "tools/vm gate"` also matches every waiter, because a waiter's command line quotes the
  gate command it waits for. On 2026-09-12 it reported three gates when one was running: four of
  the five matches were `sleep` loops. This is the same failure as the bullet below -- a bare
  pattern answering confidently about the wrong subject -- and here it misled the coordinator
  rather than a waiter.

  A third shape of the same failure, which cost a wrong number rather than a wrong wait: a `||`
  path fallback reads the wrong tree. From inside `.worktrees/fort-<id>`, `../../test/foo.py`
  **is** the main checkout, so `grep X ../../test/foo.py || grep X test/foo.py` never reaches the
  fallback and answers about `main` while the reader believes it answered about the branch. It
  reported `CORPUS_FILES = 604` off `main` for a branch whose value is 615. Nothing failed; the
  number was simply wrong. Name the file you mean, and read one tree per command so the output
  says which tree answered.

  A parent of 1 is an orphan. An elapsed time past 30 minutes on a gate is suspect, because a full
  gate takes about 23 minutes. The working directory in `lsof` says whether the process is yours
  or a neighbour's, which is the whole question (T-041, T-094).
- **A waiter must match its own job, not the pattern.** This is the cause of the shape above. A
  loop written `until ! pgrep -f 'tools/vm gate'; do sleep 90; done` matches **every** worktree's
  gate, so it waits for a build it does not own and its own agent reads the delay as contention.
  One such loop ran 2 hours 21 minutes on 2026-09-12, against a worktree that was deleted an hour
  before, and it held up a second agent's waiter while that agent's gate held up its own. Write
  the waiter against one job. Three shapes do it: capture the pid at launch
  (`tools/vm gate > build/gate.log 2>&1 & pid=$!; wait "$pid"`), which is the shortest and needs
  no loop; wait for a marker the job writes (`[ -s build/gate.log ]` and the last line of the
  log); or, where a pattern is unavoidable, put the worktree path in it
  (`pgrep -f "fort-t094.*tools/vm gate"`). A bare pattern makes every agent's waiter a dependency
  on every other agent's build, which looks exactly like contention and is not (T-094).
  **Prefer the first shape** (T-113). The second failed four times on 2026-09-13: two waiters polled
  `build/gate*.log` in worktrees deleted hours earlier (one for 11 h 12 m), one polled `gate.log`
  for a line the run wrote to `gate2.log`, and one polled an abandoned run whose green line could
  never appear -- and each looked exactly like a job still running. A marker waiter is the
  fallback for a job you did not start, and it needs a second condition that ends it.

- **Do not run a binary in the slot that did not build it.** The two slots share one build
  directory through the VirtualBox shared folder, and the folder serves an executable's pages from
  a cache that `md5sum` does not read. T-135 built `build/debug/fort` in slot 1 and ran it in slot
  2: both slots reported the md5 `786cb6b9559f368f9399d2a52af43164`, slot 1 answered with the
  compiler in that file and slot 2 answered with the compiler it had built there itself an hour
  before. `ninja` said `no work to do`, because the file was current; only the mapping was stale.
  The symptom is a test that fails in one slot and passes in the other with the same bytes, which
  reads as a flaky test and is not one. Two cures: run the binary in the slot that built it, or
  copy it into the guest first (`cp build/debug/fort /tmp/f && /tmp/f ...`), which T-135 used to
  prove the diagnosis, since the copy answered correctly from the same md5. Use the second slot
  for a build of its own, never for a run against the first slot's build directory.
- **Two VMs, both from the main checkout, selected by `FORT_VM_SLOT`** (T-114, set by the user
  on 2026-09-13: two VMs at all times, both in use). The host has 10 physical cores and one
  6-vCPU guest cannot reach the other four, so slot 2 is a second VM brought up from the **same**
  directory: `FORT_VM_SLOT=2 tools/vm up`. Both mount the main checkout at `/vagrant`, so both
  see every worktree and `git` works in both; a `build/` configured under one slot builds under
  the other, because the mapping is identical. **One condition: both guests must carry the same
  toolchain.** `Vagrantfile` pins the box as `>= 202510.26.0`, which is open-ended. CMake caches
  `CMAKE_C_COMPILER_VERSION` and does not re-detect a changed compiler at the same path. So two
  guests provisioned weeks apart could mix objects in one `build/`, and nothing would report it.
  Bring both slots up from one box version, or delete `build/` when they differ. What differs
  between slots is only vagrant's state directory
  (`.vagrant-2/`, through `VAGRANT_DOTFILE_PATH`) and the machine name (`fort-dev-fort-2`). Slot
  2 is sized 4 vCPUs and 8 GB, so 6 + 4 fits the cores exactly and 16 GB of 32 leaves room.
  This replaces the per-ticket VM (`FORT_VM_DIR` inside a worktree, T-100 and T-101), which
  mounted one worktree, had no working `git`, bound its build directory to that mapping, and was
  left running idle for hours once. Two rules. **The coordinator tells each ticket its slot**, so
  two long gates land on different slots. And the Vagrantfile vagrant reads is the **main
  checkout's**, so a change to it is live only after it merges -- a slot-2 `up` run from a
  branch that added the slot logic failed with "machine fort-dev-fort already exists".
  A gate is reused only on the same source SHA with the same `build/` outputs; a doubt means a
  rerun (the identity record of T-148 and its darwin form of T-147 were retired on 2026-09-25).
- **Every `tools/vm` subcommand that drives `build/<preset>` holds its worktree, and a build is
  stopped with TERM** (T-113). The perimeter is `configure`, `build`, `test`, `workflow`, `check`,
  `check-lang`, `check-all`, `format`, `format-check`, `tidy`, `lines` and `gate`. **`run` and
  `ssh` take no hold**: the first runs whatever it is given, and the second is an interactive
  shell that may sit open for hours. So a build started through `run` is the one way round the
  hold. One worktree
  has one `build/<preset>`, so two gates in it make two ninja processes rewrite each other's
  objects and the log names whichever test lost -- a collision that reads as a test failure
  (T-087; again 2026-09-13, an orphan at ppid 1 driving `fort-t064` for 23 minutes beside the
  live gate). The gate now writes its pid to `build/vm-hold.pid`, refuses to start while that pid is
  alive, and takes a stale hold whose process is gone, so a killed build never wedges its
  worktree. **Taking over a stale hold is not atomic.** Two builds started in one worktree within
  milliseconds can both take it: the `rm` acts on a file the process verified a moment earlier.
  Measured 2026-09-13: 1 double-take in 6 trials at a 1 ms skew, 0 in 60 at natural skew. POSIX
  shell has no atomic create-or-break, and `/usr/bin/shlock` refuses a stale lock on this host
  rather than breaking one -- exit 1 even for a lock it wrote itself whose pid had died -- so it
  would wedge a worktree instead of freeing one. `gate_release` removes only a hold the process
  still owns, so losing that race costs a refusal and not a deleted lock.
  The orphan itself comes from three things that compound: a gate started with `&` outlives the
  wrapper that started it and is reparented to init, so a kill aimed at the wrapper never reaches
  it; `tools/vm` had no trap; and `ssh -T` allocates no pty, so the guest command gets no `SIGHUP`
  when the connection drops. The hold makes a leftover harmless rather than preventing it. Two
  facts about stopping one, both measured: a trap cannot run while a foreground child runs, so
  the guest `ssh` is now a waited background job and TERM ends it within a second; and **INT
  cannot be trapped at all in a gate an agent started**, because a script started with `&` from a
  non-interactive shell has SIGINT ignored on entry (POSIX). Use `kill -TERM <pid>`. Find the pid
  by matching the process whose command *is* the gate. Never use `pkill -f`: it also matches every
  waiter that quotes the command.
  **`$!` names a subshell when `&` follows an AND-list**, so the pid a wrapper prints is often not
  the gate. In `cd <worktree> && tools/vm gate > build/gate.log 2>&1 & pid=$!`, the `&` binds to
  the whole `cd && tools/vm` list, so bash forks a subshell for the list and `$!` is that subshell;
  the gate is its child. `kill -TERM "$pid"` then ends the subshell and reparents the gate to init,
  where it holds the worktree and the next gate refuses to start. Measured 2026-09-13:
  `bash -c 'cd /tmp && sleep 8 >/dev/null 2>&1 & pid=$!; echo $pid'` printed the subshell and
  `ps -o ppid` showed `sleep` as its child; without the `cd &&` prefix, `$!` is the command itself.
  So `cd` on its own line before the background job, or take the pid from the refusal message,
  which reads the hold file and names the right process. The hold and this rule are both correct;
  the wrapper form is what was wrong, and it was in the coordinator's own task prompts all day
  (T-121).
  **The same subshell, used on purpose, gives a gate that outlives the session.** A gate started
  through an agent's background shell dies when that session ends: on 2026-09-14 four runs died
  that way in one hour, two at 0 `self-hosted` and two about four minutes in, and each death cost
  the whole run. Start the gate inside its own subshell instead:

      cd <worktree> && ( FORT_VM_SLOT=<n> nohup tools/vm gate > build/gate.log 2>&1 & )

  The parentheses bind the `&` to the one command and not to the AND-list, so the command returns
  at once and the gate detaches to ppid 1. **A gate at ppid 1 is the normal state and not a
  fault**, which is the one difference from the orphan above: that orphan ran beside a live gate,
  and this one holds `build/vm-hold.pid` alone and releases the hold when it finishes. Poll the
  hold file and `build/gate.log` for the result (T-134).
  **Codex command calls require a supervised gate (T-146).** On 2026-09-15, a detached gate
  printed pid 63084 and ended before debug configure finished. Its log had no result.
  A 9-second `nohup sleep` probe with the documented subshell also vanished before the next `ps`.
  This Codex command runner ends background children when its shell call returns.
  For a branch gate, set the command session working directory to the ticket worktree.
  Run a final main gate from main only after the ticket merges.
  Start the gate as a supervised command session and keep its session handle:

      FORT_VM_SLOT=<n> tools/vm gate > build/gate.log 2>&1

  Poll that handle for exit 0. Require the final `tools/vm gate: green` log line.
  Run read-only review in another session while the gate runs.
  The detached form above remains for a host shell that keeps its child alive after handoff.
  **A host-side timeout does not reach the guest.** `ssh -T` allocates no pty, so a command that
  kills the host process leaves the guest command running. T-078 measured it on the new
  `guest_run`: `subprocess.run(["tools/vm", "run", "sleep 40"], timeout=3)` raised its timeout,
  and `tools/vm run 'pgrep -af sleep'` then showed the guest's `sleep 40` and its `bash -c`
  alive. A host tool that gives a guest command a timeout therefore stops rather than starting
  the next one, because a second runner in one build directory reads as a finding
  (notes/testing.md 1). `tools/mutate.py` does exactly that and names the `pgrep` to run.

## 2. The shared folder

- Only the VM directory is shared: `/tmp` in the guest is not the host's `/tmp`. A scratch file
  a host command writes there is invisible to `tools/vm run`, which is worse than an error,
  because the guest may hold an unrelated file of that name from an earlier run and the command
  then answers about it -- an experiment on a hand-written `.ll` verified a module that was not
  the one being tested. Put scratch files under the worktree (`build/` is gitignored) so both
  sides see the same bytes.
- The shared folder can serve stale pages to tools that `mmap` a file the host rewrote (seen
  with `clang-format` reporting a line past the end of a shrunk file while `md5sum` read the
  right bytes). The other face of it is the compiler reading the tail of a file the host has just
  rewritten as NUL bytes, `error: null character ignored [-Werror,-Wnull-character]` at a line
  past the end, while `git diff` on the host shows a clean edit. Recover with
  `tools/vm run 'sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"'`, and delete that target's
  object as well, since ninja has already recorded the failed compile.
  **The rule is the inode, not the side that writes: rewrite the file so it gets a new one.**
  `tools/vm run 'cp <file> /tmp/c && rm -f <file> && cp /tmp/c <file>'` in the guest, or
  `cp <file> /tmp/x && mv /tmp/x <file>` on the host; either clears it at once, with no `sudo`
  and no cache drop. Run one of them after every edit of a source a build has already compiled,
  whichever side wrote it. A write that **truncates in place** and keeps the inode --
  `open(p, 'w')` in python, `>` in the shell, an editor that does not write through a temporary
  -- can leave the stale page on either side.
  **The symptom to search for: clang reports `expected identifier or '('` in `<built-in>`, or
  NUL bytes past the old end of the file, while both sides read the same bytes.** The two reads
  agreeing while the compiler disagrees is what identifies this and rules out the source, so the
  first suspicion for a `<built-in>` diagnostic is the shared folder and never the file. Four
  measurements, in the order they were taken.
  T-124 rewrote one line of `bootstrap0/src/check.c` with a python heredoc **inside the guest**;
  clang reported `expected identifier or '('` in `<built-in>` and segfaulted, four rebuilds in a
  row, while `clang -fsyntax-only` on a copy in the guest's own `/tmp` exited 0 and
  `tr -d '\0' | wc -c` read the same 124493 bytes on both sides. `touch` in the guest did not
  clear it and a host-side rewrite of the identical bytes did, which T-124 read as "write from
  the host".
  T-125 met the same crash 13 times with the same guest-side heredoc and found the new-inode
  cure, which is cheaper than dropping the caches.
  T-128 wrote four mutants of `bootstrap0/src/check.c` and `src/fort/check.ft` **from the host**,
  one at a time, and the first two hit the stale mapping and show its two faces: the file grew by
  9 bytes and clang read NUL bytes at line 3693, past the old end, and then the file shrank by 43
  bytes and clang reported `expected identifier or '('` in `<built-in>`. Both times the guest's
  own read was clean -- `python3` in the guest counted 130438 bytes and 0 NUL bytes, and
  `clang -fsyntax-only` on a copy in the guest's `/tmp` exited 0 -- and both times `touch` in the
  guest and a second `tools/vm build` changed nothing. The guest `cp`, `rm`, `cp` above cleared
  both at once.
  T-130 measured the case that fixes the axis. It rewrote `bootstrap0/test/check_conv_test.c` **from
  the host** with `open(p, 'w').write(...)`, which truncates in place, and clang crashed three times
  all the same -- twice through ninja, once as a direct `CCACHE_DISABLE=1` command -- while both
  sides read `md5sum 0591673e...`, 42878 bytes and valid UTF-8, and a `cp` of the file into the
  guest's own `/tmp` compiled with exit 0. `cp f /tmp/x && mv /tmp/x f` on the host, identical
  bytes and a new inode, cleared it. So writing from the host is not immunity; it helped T-124
  because that rewrite replaced the file. A mutation experiment still edits from the host, for
  the reason the next bullet gives, and it writes each variant through a new inode.
  T-125 also measured the worse face of it.
  An in-place restore inside the guest, followed by a rebuild, left a **compiler that linked and
  ran and was wrong**: `check_conv_test`, `check_extern_test` and `check_const_test` went red on
  diagnostics the restored source cannot produce, and `build/debug/fort --check` printed
  `constant expression out of range` on a program with no constant in it. No build error said so.
  `ninja -t clean` and a full rebuild cleared it, and the same three suites then passed.
- **A mutation harness must restore on the path it does not plan to take, and must prove the
  restore.** The stale page above was the face of T-125's incident that a cure exists for; the
  face that caused the damage was different. `bash harness.sh | head -8` closed the pipe, SIGPIPE
  killed the harness between a mutation and its restore, and the next iteration's backup then
  copied the **mutant** and wrote it back as the "restore". 4 of 11 call sites were reverted into
  a commit that way. `rm` and `cp` do not help a process that never reaches the restore. Three
  lines do: put the restore in `trap 'restore' EXIT INT TERM` so it runs on a signal, compare
  `md5sum` of the restored file with the pristine copy **before** the next iteration and stop on
  a difference, and **never pipe a mutation harness into `head`** -- write the whole output to a
  file and read the file. Writing each variant through a new inode, as the bullet above asks,
  removes the stale page but not this: a loop that dies mid-iteration leaves the same mutant
  behind, on either side.
  **Do not wait for a red suite to find it.** T-125's tree stayed **green** through the whole
  incident, because the suite that covers the reverted sites is `test/fort` and only the C unit
  tests had been run since. The check that sees it is a count of the thing the branch changed --
  `grep -c 'check_operand(ck,' src/fort/check.ft` read 6 where it had read 8 -- so a branch whose
  work is countable counts it again after any mutation experiment, before the commit and again
  before the gate.
- The same folder can hand ninja a stale mtime, so a rebuild after an edit prints "no work to do"
  and the suite keeps failing on text the file no longer holds; `md5sum` in the guest reads the
  new bytes and dropping the caches does not help, because it is the timestamp and not the
  content that is stale. Delete that target's object
  (`build/<preset>/CMakeFiles/<target>.dir/<path>.o`; a target of `bootstrap0/CMakeLists.txt`
  keeps it in `build/<preset>/bootstrap/CMakeFiles`) and build again. A mutation experiment --
  break a rule in the compiler, watch the test go red, restore it, watch it go green -- runs into
  this more than anything else, because every step rewrites a file the last step just built from,
  and a stale mtime makes the next step report the previous binary's colours. Edit and restore
  from the host, `touch` the sources there, and prove the restore by comparing the rebuilt
  binary's `md5sum` with the baseline's: a green suite after the restore does not prove the
  restore was compiled, and an md5 that differs from the baseline says some earlier step built
  nothing. The guard caught **1 of the 88** rebuilds of T-077's mutation audit. There
  `tools/vm build debug` exited 0 and left the baseline binary in place, although the source held
  a mutation that does not compile. Without the guard that row would have read as caught by
  nothing. A re-run by hand of it gave the build error the batch had missed. That check is also how
  a build that silently skipped a source is caught, which is worth one `md5sum` before any
  measurement that will be quoted as evidence. Undo the mutation from a saved copy of the file,
  never with `git checkout <file>`: the file usually also holds the
  ticket's own uncommitted work, which that command throws away silently, and the suite stays
  green afterwards because the deleted work was the part with no test of its own yet.

## 3. Provisioning

- Provisioning installs `nodejs` (Node 18) for the extension's unit tests and fails loudly when it
  is older or has no built-in test runner.
- Provisioning disables apport and sets `kernel.core_pattern=core`: Ubuntu's piped core pattern
  ignores `ulimit -c 0` and made every SIGABRT cost about a second. A VM provisioned before that
  change needs `tools/vm provision` once (or the same two commands by hand).
- git runs on the host; it also works in the guest: provisioning symlinks the host path of the
  VM directory to `/vagrant`, so worktree `.git` files (absolute host paths) resolve there.
- **git in the guest reads one repository: the VM directory.** `tools/provision.sh` symlinks that
  directory alone (`ln -s /vagrant "$FORT_HOST_REPO"`, line 63). A worktree that runs its own VM
  with `FORT_VM_DIR="$PWD"` is therefore `/vagrant`, and the main checkout is not there, so the
  `gitdir:` line of the worktree's `.git` file points at a path the guest cannot open. Every git
  command in the guest then exits 128, and `tools/vm run 'python3 agents/lines.py --since main'`
  fails inside `git diff main...HEAD` rather than reporting a ratio. Run a measurement that needs
  git history on the host, which has the whole repository, or share the main checkout's VM
  (T-099).

## 4. The cross toolchain

- The target is x86-64 Linux. The compiler runs natively on arm64, emits LLVM IR and runs `clang
  --target=x86_64-linux-gnu` over it, so `--cc` names a clang (the guest `cc` is a native gcc and
  would build for aarch64); generated programs run under `qemu-x86_64` transparently. The verified
  line is in `spec/toolchain.md` 2 (D14.3), and `--target` names the triple (D14.1). Provisioning
  sets `QEMU_LD_PREFIX`; the test harness sets it itself. When a cross program dies by a signal,
  qemu-user appends `qemu: uncaught target signal 6 (Aborted) - core dumped` to the program's
  stderr; native execution prints nothing, so a harness comparing stderr drops that line
  (`test/pipeline_test.sh` and `test/lang/run_tests.py` do). The number and the name vary with
  the signal, so the filter matches the shape (`qemu: uncaught target signal`) and not one line.
  **No language test can go red from a leaked notice**, and the same holds for anything else a
  harness claims to strip from stderr: `//! stderr:` is a containment check, so an extra line
  only enlarges the text the substrings are sought in and every expectation still matches.
  Deleting the `drop_qemu_notice` call leaves the whole corpus green. The witness for a
  stripping rule is therefore a unit test that asserts the negative -- that a substring taken
  from the stripped line is *not* found -- which is `run_tests_test.py`'s
  `test_judge_run_qemu_notice_is_not_stderr` and `test_judge_run_signal_ignores_the_qemu_notice`;
  a `run` test that merely passes witnesses nothing here (T-071's review).

## 5. The build

- Presets (`CMakePresets.json`, Ninja, clang unless noted): `linux`, `darwin`, `debug`,
  `release` (RelWithDebInfo),
  `gcc`, `asan`, `msan`, `tsan`, `ubsan` (the last four set `FORT_SANITIZER` for
  `cmake/sanitizers.cmake`, which instruments every native target but never the cross-compiled
  runtime object). `tools/vm workflow <preset>` configures, builds and runs ctest; build
  directories are `build/<preset>` inside the worktree. `-Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wundef` apply to every C target.
- C-started mode (`FORT_ENABLE_BOOTSTRAP0=ON`) adds `bootstrap0/CMakeLists.txt` with the binary
  directory `build/<preset>/bootstrap`. That file builds `fort_core` and `fort_bootstrap0`. It sets
  its own C11 settings, warnings and definitions. It reads the target and the tool paths from the
  top-level file, and it does not configure alone. The top-level chain starts at `fort_bootstrap0`,
  which builds bootstrap-1 (T-160). Each source pin builds the next one.
  `bootstrap0/extract_pin.sh <sha> <dir> <linux|darwin>` extracts `src/fort`, `std` and
  `tools/assemble_std.sh` of a pin with `git archive`, moves the pin's `std` to `<dir>/std-pin` and
  runs the pin's own `tools/assemble_std.sh`, which writes the pin's standard root for the target to
  `<dir>/std`. The pin builds from
  `<dir>/src/fort/main.ft` with that root. No compiler of the chain gets `--target`: each one
  builds for its own built target, `std.os.TARGET` of its standard root (D14.1).
  The last source compiler builds working-tree HEAD as `fort`.
- `FORT_BOOTSTRAP0_BUILD_TESTS` (default OFF; every preset sets it ON) adds the C unit tests on
  linux and on darwin. `FORT_BOOTSTRAP0_E2E_TEST` (default ON, and OFF when
  `FORT_BOOTSTRAP0_BUILD_TESTS` is OFF) adds the ctest `bootstrap-e2e`: bootstrap-0 builds
  bootstrap-1 in `build/<preset>/bootstrap/e2e/bootstrap1`. That tree is not the tree of the chain. The
  contract suites read its standard root, so it is extracted whenever the tests are on.
- External-stage1 mode builds only HEAD with `FORT_STAGE1_COMPILER`. It does not create C
  compiler or C unit-test targets.
  Both modes build `fort_std`, a copy of
  `std/*.ft` and `std/<target>/*.ft` in `build/<preset>/std`, which is
  what the compiler reads as `--std-dir`; `FORT_TARGET_CC`, a clang (default `clang`) with
  `--target=${FORT_TARGET_TRIPLE}` (default `x86_64-linux-gnu`), is what the driver runs over the
  emitted module), `lang_ffi_helpers` (`test/lang/ffi/*.c` built natively so `-Werror` and
  tidy cover them), `check` (ctest label `unit`, including `lang_lint` and `lang_selftest`),
  `check-lang` (ctest label `lang`: the corpus, `test/fort`, `lang-json`, `fixpoint`,
  `lsp-binary`, `fort_lint_cross` and `tty`), `check-all` (every test, what `test` runs too),
  `gate` (the merge gate, `notes/testing.md` 1), `format` and `format-check` (clang-format
  over `src`, `runtime`, `test`), `tidy` (`run-clang-tidy` over the same),
  `fort-lint` (`tools/fort_lint.py`: the identifier conventions of D1.4 over `std/*.ft` and
  `src/fort/*.ft`, read off `fort --index`; the ctests are `fort_lint` and `fort_lint_selftest`),
  `agents/lines.py` (run by hand: test lines per source line, source being the compiler, `std/*.ft`
  and the runtime (D14.6, amended by T-076); `TARGET_RATIO` in that file holds the corpus
  target, which is above 2:1 since T-123, and is printed and never compared, so `--min RATIO`
  from the caller is the only value that fails a run; `--since REF` measures a branch's own diff
  instead of the whole repository, which is how a ticket answers for the code it introduces
  rather than hiding behind the corpus already there; **`--since` reads the commits, not the
  working tree**, so a file that is only written or only staged counts as 0 lines and the ratio
  answers about the last commit: commit first, then measure (T-093); its own tests are
  `agents/lines_test.py`).
  `tools/vm <target> [preset]` runs one. CMake detects Linux or Darwin from the host system.
  The `linux` and `darwin` presets set no `FORT_TARGET` cache value.
- **The `darwin` build copies target library modules** (T-145, T-152).
  `cmake --workflow --preset darwin` builds and tests on a Darwin host.
  `fort_std` copies `std/*.ft` and `std/darwin/*.ft` to the standard root: the target's
  `libc.ft`, `net.ft` and `os.ft` stand only in `std/linux/` and `std/darwin/`.
  The root holds 13 fort files under `build/darwin/std`.
  `build/darwin/fort` loads that root when a program imports `std.net`.
- A CMake variable derived from a cache variable must not be cached itself: `find_program`
  caches by default, so `FORT_TARGET_CC_PATH` kept resolving to the old program after
  `FORT_TARGET_CC` changed in an existing build directory, and the build then ran gcc with
  clang's arguments. It uses `NO_CACHE`; check for the same trap before adding a `find_program`
  or `find_file` whose `NAMES` come from a cache variable, or delete `build/<preset>` after such
  a change.
- Put generated source that a CTest reads under an `ALL` target. `tools/vm workflow` builds
  default targets before CTest starts (T-141).
- The `gcc` preset is the project's only cross-compiler check and it is **not** part of the gate,
  whose three presets are all clang, so nothing runs it unless someone does: run
  `tools/vm workflow gcc` by hand whenever compiler or test-helper code changes. It went unbuilt
  long enough to accumulate errors in shared test helpers (`-Wformat-truncation` on every sandbox
  path join, `-Warray-compare` on a pointer comparison written as an array one), which clang does
  not diagnose at all. `tools/vm build gcc` on its own, in a worktree that never configured the
  preset, reports "not a directory": that is the missing build directory, not the failure, and
  `workflow` does the configure itself. gcc's truncation warning is level 1, so using the
  `snprintf` result silences it: check it against the capacity and fail the test with a message
  (`join_sandbox_path`, `gen_join_path`), never widen the buffer or cast the result away. A guard
  only gcc enforces is a guard no test holds, so assert each one -- the call sites too, not only
  the helper -- as `bootstrap0/test/modules_test.c` and `bootstrap0/test/gen_test.c` do.
- Binaries: `build/<preset>/bootstrap/fort` is the C compiler.
  `build/<preset>/bootstrap/bootstrap-N/fort` is source pin N from `tools/bootstrap.ref`, which
  starts at bootstrap-1. `build/<preset>/fort` is HEAD built by the last source pin (T-155).
  Stage1 is the compiler that builds HEAD: the last pin, or `FORT_STAGE1_COMPILER`. Stage2 is
  `build/<preset>/fort`, and `tools/fixpoint.sh` builds stage3 and stage4 from it. Text before
  2026-09-24 calls the C compiler stage1 and the first pin bootstrap-0.
  CMake validates the list and owns all extraction and build edges. `FORT_STAGE1_COMPILER` names an
  external compiler and skips the list. `FORT_ENABLE_BOOTSTRAP0=OFF` requires that external path.
  `FORT_ENABLE_BOOTSTRAP0=ON` rejects that external path. External-stage1 mode reads no pin
  (T-160).
  The host interface is CMake itself: `cmake --workflow --preset <p>` and the `gate` target of
  the base preset. `tools/vm gate` enters the linux VM once and builds that target there.
- **A full clone is a build requirement, and a shallow one cannot build** (T-131). Every pin of
  `tools/bootstrap.ref` is a commit of this repository. CMake reaches it with `git cat-file`,
  `git merge-base`, and `git archive`. Validation runs at configure time. A missing pin stops the
  configure and names the pin instead of failing a compile later. It
  works from a worktree and in the guest: a worktree shares the object store of the checkout that
  made it, and the guest reaches that store through the symlink provisioning makes (see the git
  entry below). It therefore does **not** work in the guest of a VM a worktree owns, for the reason
  that entry gives.
  `build/<preset>/fort-lsp` is the language server. The `fort_lsp` target builds it from
  `src/lsp/main.ft` with `build/<preset>/fort`.
- **A ninja target may not have the name of an output path in the same directory.** `fort_lsp`
  writing `${CMAKE_BINARY_DIR}/fort_lsp` configures cleanly and then fails the build with
  `ninja: error: build.ninja:2880: multiple rules generate fort_lsp`, preceded by
  `phony target 'fort_lsp' names itself as an input`. CMake does not diagnose it, so the failure
  arrives at the first build and looks like a rule conflict. Give the two different names: the
  target is `fort_lsp` and the binary is `fort-lsp` (T-064).

## 6. The host side

- **The `darwin` gate uses the Darwin host** (T-147, T-152). The host needs clang-format 18
  and clang-tidy 18 (`brew install llvm@18`, keg-only under `/opt/homebrew/opt/llvm@18/bin`,
  where `CMakeLists.txt` looks first), because the gate runs `format-check` and `tidy` there as
  on linux, and the lint configuration is clang 18's: clang-format 19 wraps one ternary of
  `bootstrap0/test/driver_test.c` differently and clang-tidy 19 adds checks the configuration
  does not name (measured 2026-09-25: 23 `readability-enum-initial-value` and 10
  `readability-math-missing-parentheses` findings). The `tidy` target passes
  `-clang-tidy-binary` (run-clang-tidy runs the `clang-tidy` of the PATH otherwise) and, on
  darwin, `-extra-arg=-isysroot <sdk>` (clang-tidy does not find the SDK headers as the darwin
  clang does). The three tool variables are `NO_CACHE`; a build directory configured before
  2026-09-25 still holds `FORT_CLANG_FORMAT` and `FORT_RUN_CLANG_TIDY` in its cache, and
  `find_program` skips its search for a variable that the cache defines: run
  `cmake -U FORT_CLANG_FORMAT -U FORT_RUN_CLANG_TIDY --preset darwin` once. Check the host tools
  before the build:

  ```sh
  xcode-select -p
  xcrun --sdk macosx --show-sdk-path
  xcrun --sdk macosx --show-sdk-version
  xcrun --sdk macosx --find clang
  xcrun clang --version
  /opt/homebrew/bin/opt --version
  cmake --version
  node --version
  rg --version
  ```

  The verifier is the `FORT_OPT` cache value (`/opt/homebrew/bin/opt` by default on darwin);
  pass `-DFORT_OPT:FILEPATH=<path>` to `cmake --preset darwin` only when that tool verifies
  LLVM IR. Configure and build the `darwin` compiler and language server on the darwin host:

  ```sh
  cmake --workflow --preset darwin
  ```

  `build/darwin/fort` is the compiler from the CMake graph.
  `build/darwin/fort-lsp` is the `darwin` language server.
  The compiler and language server use the target that CMake selects. The build calls `tools/vm`
  zero times while it builds the source chain and tests the fixed point.
  The host scripts take the SDK from `xcrun --sdk macosx --show-sdk-path` and pass it as
  `-isysroot`; the compiler's own `--cc` link finds it through clang.
  Run the `darwin` gate:

  ```sh
  cmake --preset darwin && cmake --build --preset darwin --target gate > build/darwin-gate.log 2>&1
  ```

  The darwin gate is the `gate` target of the `darwin` preset (`notes/testing.md` 1):
  `format-check`, `tidy`, every test, then the `darwin-asan` and `darwin-ubsan` workflows. The
  same tests register on both targets; the gate excludes no corpus fixture. `darwin-asan`
  selects Apple clang (`/usr/bin/clang`): the Homebrew clang 19 address sanitizer runtime hangs
  at exit on darwin 25 (measured 2026-09-25 on a five-line C program), and LeakSanitizer does
  not exist on arm64 darwin, so `cmake/sanitizers.cmake` drops `detect_leaks=1` there. There is
  no `darwin-msan` and no `darwin-tsan`: MemorySanitizer is linux-only and ThreadSanitizer needs
  no run, since nothing here has threads.

- An ssh `ControlPath` under `os.tmpdir()` does not work on macOS: the host's temporary directory
  is `/var/folders/<...>/T`, ssh binds the socket under a temporary name of its own, and the total
  passes the 104-byte Unix domain socket limit, so ssh exits 255 with `unix_listener: path ... too
  long` -- indistinguishable, to a caller, from a VM that is down. Anything that multiplexes ssh
  from the host must measure the path and fall back to `/tmp/<something short>`. Nothing in the
  repository does any more: T-089 deleted the extension's own transport, and `tools/vm` is now the
  only thing that crosses into the guest.
- **`tools/vm run` is how anything on the host reaches the guest, and that includes the editor.**
  VS Code runs on the host, there is no `~/.vscode-server` in the guest, and the extension crosses
  by spawning `<workspace>/tools/vm run '<command>'` with its working directory set to the
  workspace folder: `tools/vm run` resolves the VM directory and the cached ssh configuration
  itself and runs the command in the guest directory matching the host's. It costs about 0.1 s
  (five samples of a real check over this repository: 0.09, 0.13, 0.10, 0.10, 0.09 s), which is
  well inside a save. The path needs no host-to-guest mapping in either direction, because the
  compiler names each file the path it opened it by, the entry file as given on the command line
  (`toolchain.md` 2, D14.2): pass a path relative to the workspace folder and the document names it
  the same way, so it resolves against that folder on the host. That is the premise the whole
  crossing rests on, and `a_relative_entry_is_named_in_the_document_exactly_as_it_was_given` in
  `bootstrap0/test/driver_check_test.c` is what pins it: an absolute path there would put every
  record outside the workspace and the extension would go **silent** rather than wrong, which is the
  worst failure shape an editor has. What comes back absolute is what the compiler found for itself
  -- the standard library under `/vagrant/build/release/std` -- and those files are the guest's, so
  an editor drops them rather than painting a path the host cannot open. The argument of `run` is
  handed to a shell in the guest, so a path is quoted before it goes in.
- **git does not work in the guest of a VM that a worktree owns.** `/vagrant` is then the worktree,
  and the worktree's `.git` file names `<main checkout>/.git/worktrees/<name>`, which that guest
  has no path to. `git diff main...HEAD` exits 128, and `agents/lines.py --since main` ends in a
  CalledProcessError from that command. Run `python3 agents/lines.py --since main --min 3.0` on the
  host: it needs git and Python 3 and nothing of the build. On the shared VM git works, because
  provisioning symlinks the host path of the main checkout to `/vagrant` (T-094).
