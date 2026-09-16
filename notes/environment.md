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
  **Gate input identity (T-148).** A reusable `tools/vm gate` reads the branch tree, the main VM
  configuration, guest packages, the guest profile, kernel, QEMU registration, and build outputs.
  Record `git rev-parse HEAD main` in the worktree before the gate and after final review.
  Require empty `git status --porcelain --untracked-files=all` output at both times.
  That status omits ignored `.ft` inputs. Record their path and content manifest too:

  ```sh
  FORT_VM_SLOT=<n> tools/vm run 'bash tools/gate_ft_manifest.sh'
  ```

  The manifest hashes `.ft` paths, symlink targets, and contents outside `build/`, `.git/`,
  and `.worktrees/`.
  Require the same manifest value after final review. A new ignored `.ft` file changes it.
  The VM uses the main checkout's `Vagrantfile`, not the branch copy.
  Record its content hash before the gate and after final review:

  ```sh
  FORT_VM_SLOT=<n> tools/vm run 'sha256sum /vagrant/Vagrantfile'
  ```

  A changed main `Vagrantfile` invalidates reuse even when the main SHA stays equal.
  Record the physical VM directory, `FORT_VM_SLOT`, and the VM UUID.
  Read the UUID from `.vagrant/machines/default/virtualbox/id` for slot 1.
  Read it from `.vagrant-2/machines/default/virtualbox/id` for slot 2.
  Record the guest values with these read-only commands in the same slot:

  ```sh
  FORT_VM_SLOT=<n> tools/vm run 'uname -rm'
  FORT_VM_SLOT=<n> tools/vm run 'sha256sum /var/lib/dpkg/status /etc/profile.d/fort.sh'
  FORT_VM_SLOT=<n> tools/vm run 'sha256sum /proc/sys/kernel/core_pattern'
  FORT_VM_SLOT=<n> tools/vm run 'sha256sum /proc/sys/fs/binfmt_misc/qemu-x86_64'
  ```

  The package hash detects package version changes. The profile hash detects fort environment
  changes. The kernel, core, and QEMU values detect changes to the test runtime.
  The gate also uses `build/debug`, `build/asan`, and `build/ubsan` as cache inputs.
  Capture the gate output in `build/gate.log`. Record its SHA256 value after the gate.
  At final review, require the same log SHA256 value and this count to equal zero:

  ```sh
  test -f build/gate.log && test -d build/debug && test -d build/asan &&
    test -d build/ubsan &&
    bash -o pipefail -c 'find build/debug build/asan build/ubsan -newer build/gate.log | wc -l'
  ```

  Use the actual gate log path if it differs. A changed or missing log invalidates reuse.
  A positive count means a preset file or directory changed after the gate.
  A missing log, missing preset directory, or failed `find` command invalidates reuse.
  A review log in `build/` does not change a preset input.
  A manual guest edit outside this identity also invalidates reuse. Rerun if evidence is missing.
  This identity applies to `tools/vm gate`, not native Mac tests. T-147 owns the Mac host identity.
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
  T-124 rewrote one line of `src/bootstrap/check.c` with a python heredoc **inside the guest**;
  clang reported `expected identifier or '('` in `<built-in>` and segfaulted, four rebuilds in a
  row, while `clang -fsyntax-only` on a copy in the guest's own `/tmp` exited 0 and
  `tr -d '\0' | wc -c` read the same 124493 bytes on both sides. `touch` in the guest did not
  clear it and a host-side rewrite of the identical bytes did, which T-124 read as "write from
  the host".
  T-125 met the same crash 13 times with the same guest-side heredoc and found the new-inode
  cure, which is cheaper than dropping the caches.
  T-128 wrote four mutants of `src/bootstrap/check.c` and `src/fort/check.ft` **from the host**,
  one at a time, and the first two hit the stale mapping and show its two faces: the file grew by
  9 bytes and clang read NUL bytes at line 3693, past the old end, and then the file shrank by 43
  bytes and clang reported `expected identifier or '('` in `<built-in>`. Both times the guest's
  own read was clean -- `python3` in the guest counted 130438 bytes and 0 NUL bytes, and
  `clang -fsyntax-only` on a copy in the guest's `/tmp` exited 0 -- and both times `touch` in the
  guest and a second `tools/vm build` changed nothing. The guest `cp`, `rm`, `cp` above cleared
  both at once.
  T-130 measured the case that fixes the axis. It rewrote `test/check_conv_test.c` **from the
  host** with `open(p, 'w').write(...)`, which truncates in place, and clang crashed three times
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
  (`build/<preset>/CMakeFiles/<target>.dir/<path>.o`) and build again. A mutation experiment --
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
  command in the guest then exits 128, and `tools/vm run 'python3 tools/lines.py --since main'`
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

- Presets (`CMakePresets.json`, Ninja, clang unless noted): `debug`, `release` (RelWithDebInfo),
  `gcc`, `asan`, `msan`, `tsan`, `ubsan` (the last four set `FORT_SANITIZER` for
  `cmake/sanitizers.cmake`, which instruments every native target but never the cross-compiled
  runtime object). `tools/vm workflow <preset>` configures, builds and runs ctest; build
  directories are `build/<preset>` inside the worktree. `-Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wvla -Wstrict-prototypes -Wmissing-prototypes -Wundef` apply to every C target.
- Targets: `fort_core` (static library, `src/bootstrap/*.c` except `main.c`, globbed), `fort`
  (`build/<preset>/fort`), `fort_std` (a copy of `std/*.ft` in `build/<preset>/std`, which is
  what the compiler reads as `--std-dir`; `FORT_TARGET_CC`, a clang (default `clang`) with
  `--target=${FORT_TARGET_TRIPLE}` (default `x86_64-linux-gnu`), is what the driver runs over the
  emitted module), `lang_ffi_helpers` (`test/lang/ffi/*.c` built natively so `-Werror` and
  tidy cover them), `check` (ctest label `unit`, including `lang_lint` and `lang_selftest`),
  `check-lang` (`test/lang/run_tests.py` with the built compiler; the same command is the ctest
  `lang`, label `lang`), `check-all` (both), `format` and `format-check` (clang-format
  over `src`, `runtime`, `test`), `check-comments` (`tools/check_comments.py`, which
  `format-check` depends on: it rejects a `/* */` in the same sources, D2.2, and its own unit
  tests are the ctest `check_comments_selftest`), `tidy` (`run-clang-tidy` over the same),
  `fort-lint` (`tools/fort_lint.py`: the identifier conventions of D1.4 over `std/*.ft` and
  `src/fort/*.ft`, read off `fort --index`; the ctests are `fort_lint` and `fort_lint_selftest`),
  `lines` (`tools/lines.py`: test lines per source line, source being the compiler, `std/*.ft`
  and the runtime (D14.6, amended by T-076), target 3:1, `--min RATIO` fails
  below it; `--since REF` measures a branch's own diff instead of the whole repository, which
  is how a ticket answers for the code it introduces rather than hiding behind the corpus
  already there; **`--since` reads the commits, not the working tree**, so a file that is only
  written or only staged counts as 0 lines and the ratio answers about the last commit: commit
  first, then measure (T-093); its own tests are the ctest `lines_selftest`).
  `tools/vm <target> [preset]` runs one.
- **The native Mac build copies target library modules** (T-145).
  `tools/mac fixpoint` runs the `mac-native` CMake workflow on Mac arm64.
  `fort_std` copies `std/mac/libc.ft` and `std/mac/net.ft` to the standard root.
  The root holds 12 fort files under `build/mac-native/std`.
  `build/mac-native/fort` loads that root when a program imports `std.net`.
- A CMake variable derived from a cache variable must not be cached itself: `find_program`
  caches by default, so `FORT_TARGET_CC_PATH` kept resolving to the old program after
  `FORT_TARGET_CC` changed in an existing build directory, and the build then ran gcc with
  clang's arguments. It uses `NO_CACHE`; check for the same trap before adding a `find_program`
  or `find_file` whose `NAMES` come from a cache variable, or delete `build/<preset>` after such
  a change.
- Put generated source that a CTest reads under an `ALL` target. `tools/vm workflow` builds
  default targets before CTest starts (T-141). Ninja does not track a changed `sw_vers` result
  through file dependencies. Run the Mac platform generator on each default build. Keep each
  output file's time when its text stays equal. This stops rebuilds of targets that read it.
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
  the helper -- as `test/modules_test.c` and `test/gen_test.c` do.
- Binaries: `build/<preset>/fort` is stage1 (the C compiler); `build/<preset>/pin/<n>/fort` is the
  compiler of pin `n` of `tools/bootstrap.ref`; `build/<preset>/stage2/fort` is the self-hosted
  compiler, which the **last pin** builds from HEAD's `src/fort` (T-131, `notes/compiler.md` 8).
  The `fort_stage2` target builds the whole chain at every build, so a module the last pin rejects
  fails the build rather than the test run; stage2 is an x86-64 binary and runs under qemu like
  every program the compiler builds. Each pin's tree is `git archive`d into `build/<preset>/pin/<n>`
  and stamped `.tree-<sha>`, so it is extracted once per sha and per build directory, and a pin
  rebuilds only when its sha moves or the compiler before it changes. Measured under `debug` on
  2026-09-14: an edit to `src/fort` rebuilds `stage2` and `fort-lsp` alone in 3.3 s, an edit to
  `src/bootstrap` cascades stage1, pin 0, stage2 and `fort-lsp` in 11.4 s, one hop costs 2.6 s
  checked and 3.1 s release, and an extraction 0.03 s. `rm -rf build/<preset>` pays the chain once.
  `tools/bootstrap.sh [--preset <preset>] [--stage3]` builds the chain and that stage2 by hand in
  the guest, through the CMake target rather than by spelling a hop. With `--stage3` it hands the
  fixed point to `tools/fixpoint.sh`, and it writes no stage2 of its own, since that script builds
  one per build mode.
- **A full clone is a build requirement, and a shallow one cannot build** (T-131). Every pin of
  `tools/bootstrap.ref` is a commit of this repository, and `tools/pin.sh` reaches it with
  `git cat-file`, `git merge-base` and `git archive`. `tools/pin.sh verify` runs at configure time,
  so a missing pin stops the configure and names the pin rather than failing a compile later. It
  works from a worktree and in the guest: a worktree shares the object store of the checkout that
  made it, and the guest reaches that store through the symlink provisioning makes (see the git
  entry below). It therefore does **not** work in the guest of a VM a worktree owns, for the reason
  that entry gives.
  `build/<preset>/fort-lsp` is the language server, which the `fort_lsp` target builds at every
  build by compiling `src/lsp/main.ft` **with stage2**, so it stands beside a stage1 `fort` that
  did not compile it (T-064). It is x86-64 and runs under qemu like stage2.
- **A ninja target may not have the name of an output path in the same directory.** `fort_lsp`
  writing `${CMAKE_BINARY_DIR}/fort_lsp` configures cleanly and then fails the build with
  `ninja: error: build.ninja:2880: multiple rules generate fort_lsp`, preceded by
  `phony target 'fort_lsp' names itself as an input`. CMake does not diagnose it, so the failure
  arrives at the first build and looks like a rule conflict. Give the two different names: the
  target is `fort_lsp` and the binary is `fort-lsp` (T-064). `fort_stage2` avoids the collision by
  accident, since its binary is `stage2/fort`.

## 6. The host side

- **The native Mac gate uses the Mac arm64 host and VM slot 2** (T-147).
  Check the host tools before the build:

  ```sh
  xcode-select -p
  xcrun --sdk macosx --show-sdk-path
  xcrun --sdk macosx --show-sdk-version
  xcrun --sdk macosx --find clang
  xcrun clang --version
  /opt/homebrew/bin/opt --version
  cmake --version
  node --version
  ```

  Set `FORT_MAC_OPT` to a different `opt` path only when that tool verifies LLVM IR.
  Configure and build the native compiler and language server on the Mac host:

  ```sh
  FORT_VM_SLOT=2 tools/vm configure debug
  cmake --preset mac-native
  FORT_VM_SLOT=2 cmake --build --preset mac-native
  ```

  `build/mac-native/fort` is the native compiler.
  `build/mac-native/fort-lsp` is the native language server.
  The build uses `build/debug/stage2/fort` as its Linux seed.
  Run the counted native gate on a clean source tree:

  ```sh
  tools/mac gate > build/mac-gate.log 2>&1
  ```

  The gate runs the pipeline, native CMake tests, corpus, trap, lint, core, and network tests.
  It names each Linux-only corpus exclusion and gives the selected corpus count.
  `tools/mac identity` records the source and main SHAs and requires empty git status.
  It records host CPU, OS, SDK, Xcode, clang, verifier, CMake, Node, and tool hashes.
  It records the slot-2 VM UUID, guest tool hashes, package hash, profile hash, and QEMU interpreter.
  It records the Linux seed hash and a counted path, symlink, and content manifest for `.ft` inputs.
  The manifest includes ignored `.ft` files, `build/debug`, and `build/mac-native`.
  Capture identity before the gate and after final review:

  ```sh
  tools/mac identity > build/mac-identity-before.log
  tools/mac gate > build/mac-gate.log 2>&1
  tools/mac identity > build/mac-identity-after.log
  cmp build/mac-identity-before.log build/mac-identity-after.log
  shasum -a 256 build/mac-gate.log
  ```

  Record the gate log SHA256 again after final review.
  Require the same gate log hash and identity record before gate reuse.
  A changed SHA, status, tool, SDK, VM, seed, or `.ft` manifest invalidates reuse.
  A changed or missing build output invalidates reuse.
  Require each native output directory before a freshness count:

  ```sh
  test -f build/mac-gate.log && test -d build/debug &&
    test -d build/mac-native && test -d build/mac-native/std &&
    test -d build/mac-native/mac-platform && test -d build/mac-native/fixpoint &&
    bash -o pipefail -c 'find build/debug build/mac-native -newer build/mac-gate.log | wc -l'
  ```

  The freshness command must exit 0 and print 0.
  A missing value requires a new `tools/mac gate` run.

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
  `test/driver_check_test.c` is what pins it: an absolute path there would put every record outside
  the workspace and the extension would go **silent** rather than wrong, which is the worst failure
  shape an editor has. What comes back absolute is what the compiler found for itself -- the
  standard library under `/vagrant/build/release/std` -- and those files are the guest's, so an
  editor drops them rather than painting a path the host cannot open. The argument of `run` is
  handed to a shell in the guest, so a path is quoted before it goes in.
- **git does not work in the guest of a VM that a worktree owns.** `/vagrant` is then the worktree,
  and the worktree's `.git` file names `<main checkout>/.git/worktrees/<name>`, which that guest
  has no path to. `git diff main...HEAD` exits 128, and `tools/lines.py --since main` ends in a
  CalledProcessError from that command. Run `python3 tools/lines.py --since main --min 3.0` on the
  host: it needs git and Python 3 and nothing of the build. On the shared VM git works, because
  provisioning symlinks the host path of the main checkout to `/vagrant` (T-094).
