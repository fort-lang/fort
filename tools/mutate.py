#!/usr/bin/env python3
"""Run compiler mutation tables and report the first failing test stage.

Each round restores the sources, applies one substitution, and rebuilds the compiler.
It then runs stages from narrowest to widest. The input table supplies sources, commands, and mutations.

Commands run from the worktree root through `tools/vm run`.
`--runner` selects another command runner. A timeout ends the run because the guest command can continue.

The built binary must differ from the baseline, or the round reports STALE.
After all rounds, the tool restores sources and rebuilds the baseline.
It restores saved bytes and does not use `git checkout`.

The gate does not run mutation rounds. `--check` only verifies that each anchor matches once.

A stage must not run tests that validate the mutation table.
Such tests would observe the active substitution and report a false catch.
"""
import argparse
import json
import os
import re
import subprocess
import sys

# The runner's own timeout, which is not a status a shell can return (0 to 255).
# A stage that hangs is neither a catch nor a survivor, so it gets its own
# verdict. The run then continues with the next row.
TIMEOUT_STATUS = -1

# A verdict answers the audit question only after a complete round.
# `main` exits 1 for another verdict. A table that stops compiling cannot report success.
ANSWERS = ("caught", "survived")


class AnchorError(Exception):
    """A row's anchor does not match its file exactly once."""


# The summary block of ctest names every test that failed, whatever its label:
# `        3 - unit-gen_cast (Failed)`. The per-test lines above it do not, so
# read this summary pattern instead of the incomplete per-test lines.
CTEST_FAILURE = re.compile(r"^\s*\d+ - (\S+) \((Failed|Timeout|Subprocess aborted)\)")


def failing_tests(text):
    """The ctest names that failed in `text`, in order and without repeats."""
    names = []
    for line in text.splitlines():
        found = CTEST_FAILURE.match(line)
        if found is not None and found.group(1) not in names:
            names.append(found.group(1))
    return names


def as_text(captured):
    """What a timed-out command had written, whatever the capture gave back."""
    if captured is None:
        return ""
    if isinstance(captured, bytes):
        return captured.decode(errors="replace")
    return captured


def read(path):
    with open(path, "rb") as handle:
        return handle.read()


def write(path, data):
    with open(path, "wb") as handle:
        handle.write(data)
    # The shared folder hands ninja the mtime, so a restored file that keeps its
    # old timestamp is not rebuilt.
    os.utime(path, None)


class Guest:
    """Runs a shell command where the build lives."""

    def __init__(self, root, runner):
        self.root = root
        self.runner = runner

    def run(self, command, timeout=3600):
        argv = self.runner.split() + [command]
        try:
            done = subprocess.run(
                argv, cwd=self.root, capture_output=True, text=True, timeout=timeout
            )
        except subprocess.TimeoutExpired as expired:
            return TIMEOUT_STATUS, "%s%s\nmutate: the command ran longer than %d s" % (
                as_text(expired.stdout), as_text(expired.stderr), timeout)
        return done.returncode, done.stdout + done.stderr

    def md5(self, path):
        """The md5 of a file in the guest, or None when the build made none."""
        status, out = self.run("md5sum %s" % path)
        if status != 0 or not out.split():
            return None
        return out.split()[0]


def restore_all(table, saved, root):
    for name in table["sources"]:
        write(os.path.join(root, name), saved[name])


def apply_mutation(root, mutation):
    """Substitutes the anchor, and stops when it is not there exactly once."""
    path = os.path.join(root, mutation["file"])
    text = read(path).decode()
    if text.count(mutation["old"]) != 1:
        raise AnchorError(
            "mutate: %s: the anchor matches %d times in %s, not once"
            % (mutation["decision"], text.count(mutation["old"]), mutation["file"])
        )
    write(path, text.replace(mutation["old"], mutation["new"]).encode())


def mutated_rows(table, root):
    """The rows whose mutation the sources hold right now.

    A round rewrites a source, so a reader of the table during a round sees that
    round's own mutation: the row that is applied holds its `new` text and its
    `old` text is gone. A test that asserts the anchors must skip on such a tree,
    or it fails inside the round and reports the compiler as caught.

    The test is "`old` is absent and `new` is present", and **not** "`new` occurs
    once": a replacement often repeats text the file already had, so `new` occurs
    twice or three times after the substitution. Some table rows have this shape.
    """
    applied = []
    for mutation in table["mutations"]:
        path = os.path.join(root, mutation["file"])
        if not os.path.exists(path):
            continue
        text = read(path).decode()
        if text.count(mutation["old"]) == 0 and mutation["new"] in text:
            applied.append(mutation["decision"])
    return applied


def undo_applied(table, root, applied):
    """The text of each source with the applied rows put back, by file name.

    A round rewrites one source, and a second row that shares those lines then
    has no anchor. That is the round talking and not the table, so `--check`
    reads the reconstructed text: it puts every applied row's `new` back to its
    `old` and counts anchors there. A row whose anchor is missing from the
    reconstructed text has really rotted.

    It puts back the **first** occurrence, which is the mutation site only when
    the replacement text is unique in the file. Three rows of the emitter table
    repeat text the file already held, so the first occurrence is not always the
    site. Two things make that safe, and a table that breaks them is loud rather
    than quiet.
    - Measured, not argued: `test_every_row_of_the_table_leaves_no_row_stale_and
      _names_itself` applies all 88 rows one at a time, and every other anchor
      survives the reconstruction in every one of the 88.
    - A wrong site can only **destroy** an anchor, never invent one, because the
      text it writes is one row's `old` and a second row with that same anchor in
      the same file would already fail `--check` on a clean tree with `2
      matches`. So the failure mode is a stale row and exit 1, and never a silent
      pass.
    """
    text = {}
    for name in {m["file"] for m in table["mutations"]}:
        path = os.path.join(root, name)
        if os.path.exists(path):
            text[name] = read(path).decode()
    for mutation in table["mutations"]:
        if mutation["decision"] in applied and mutation["file"] in text:
            text[mutation["file"]] = text[mutation["file"]].replace(
                mutation["new"], mutation["old"], 1)
    return text


def check_table(table, root):
    """Reports every anchor that no longer matches its file exactly once."""
    applied = mutated_rows(table, root)
    pristine = undo_applied(table, root, applied)
    stale = 0
    for mutation in table["mutations"]:
        name = mutation["file"]
        if name not in pristine:
            print("%s: %s: no such file" % (mutation["decision"], name))
            stale += 1
            continue
        if mutation["decision"] in applied:
            print("%s: %s: the file holds this row's mutation, not its anchor"
                  % (mutation["decision"], name))
            continue
        hits = pristine[name].count(mutation["old"])
        if hits != 1:
            print("%s: %s: %d matches" % (mutation["decision"], name, hits))
            stale += 1
    if applied:
        print("mutate: a round is in progress: %s" % ", ".join(applied))
    print("mutate: %d rows, %d stale" % (len(table["mutations"]), stale))
    return 1 if stale > 0 else 0


def run_round(table, guest, root, saved, mutation, baseline, log_dir):
    """One mutation: restore, apply, build, run the stages, record."""
    row = {"decision": mutation["decision"], "file": mutation["file"],
           "what": mutation["what"]}
    log = []
    restore_all(table, saved, root)
    try:
        apply_mutation(root, mutation)
    except AnchorError as error:
        # One rotted row must not lose the verdicts of the other 75.
        row["verdict"] = "anchor-stale"
        row["tests"] = []
        row["error"] = str(error)
        return row
    status, out = guest.run(table["build"])
    log.append("==== build ====\n" + out)
    if status == TIMEOUT_STATUS:
        row["verdict"] = "timeout"
        row["stage"] = "build"
        row["tests"] = []
        write_log(log_dir, mutation, log)
        return row
    if status != 0:
        row["verdict"] = "build-failed"
        row["tests"] = []
        write_log(log_dir, mutation, log)
        return row
    row["binary_md5"] = guest.md5(table["binary"])
    if row["binary_md5"] == baseline:
        # The build reported success and produced the baseline's binary, so the
        # mutation reached no object. A verdict here would be about the wrong
        # compiler.
        row["verdict"] = "stale"
        row["tests"] = []
        write_log(log_dir, mutation, log)
        return row
    for stage in table["stages"]:
        status, out = guest.run(stage["command"])
        log.append("==== %s ====\n%s" % (stage["name"], out))
        if status == TIMEOUT_STATUS:
            row["verdict"] = "timeout"
            row["stage"] = stage["name"]
            row["tests"] = []
            write_log(log_dir, mutation, log)
            return row
        if status != 0:
            row["verdict"] = "caught"
            row["stage"] = stage["name"]
            row["tests"] = failing_tests(out)
            write_log(log_dir, mutation, log)
            return row
    row["verdict"] = "survived"
    row["stage"] = ""
    row["tests"] = []
    write_log(log_dir, mutation, log)
    return row


def run_table(table, guest, root, saved, baseline, log_dir, wanted=()):
    """The rows of the table, or the rows `wanted` names, in order.

    A timeout ends the run. The guest command can outlive the host timeout.
    A second runner in one build directory causes a collision, so the run
    stops and says so. It does not kill anything: no tool of this project runs
    `pkill` in the VM.
    """
    rows = []
    for mutation in table["mutations"]:
        if wanted and mutation["decision"] not in wanted:
            continue
        row = run_round(table, guest, root, saved, mutation, baseline, log_dir)
        rows.append(row)
        print(json.dumps(row), flush=True)
        if row["verdict"] == "timeout":
            # Quote the `pgrep` pattern. tools/vm inserts its argument into the guest script.
            # An unquoted `|` becomes a guest shell pipe.
            print("mutate: %s timed out at stage '%s'. Its command may still run "
                  "in the guest, so the run stops here: a second runner in one "
                  "build directory reads as a finding. Look with: "
                  "tools/vm run \"pgrep -af 'ninja|ctest'\""
                  % (row["decision"], row.get("stage", "")), flush=True)
            break
    return rows


def restore_and_check(table, guest, root, saved, rows, baseline):
    """Puts the sources back, and rebuilds only when no stage hung.

    The restore runs whatever ended the loop, an interrupt included, so the
    worktree never keeps a mutation. The rebuild is different. After a timeout
    the guest still holds the build directory, and a second `ninja` there is the
    same collision that the run stopped to avoid. Thus, it must not start one.
    The md5 is then not measured. The run does not print an unsupported comparison.
    The `timeout` row already makes the exit status 1.
    """
    restore_all(table, saved, root)
    hung = [row["decision"] for row in rows if row["verdict"] == "timeout"]
    if hung:
        print("restored the sources only: %s timed out, so no rebuild ran. The binary "
              "%s is the hung command's and was not measured. Rebuild by hand once the "
              "guest is idle." % (", ".join(hung), table["binary"]), flush=True)
        return baseline
    guest.run(table["build"])
    back = guest.md5(table["binary"])
    print("restored %s %s baseline %s %s"
          % (table["binary"], back, baseline,
             "MATCH" if back == baseline else "MISMATCH"), flush=True)
    return back


def exit_status(rows, unknown, restored, baseline):
    """0 when every row of the run answered the audit's question, else 1.

    Only `caught` and `survived` are answers. A build that failed, a binary
    equal to the baseline, a stage that hung and a rotted anchor each say that
    the round did not run, and a run of no rows says the `--only` named nothing.
    A table that stops compiling must not report success.
    """
    if restored != baseline:
        return 1
    if unknown or not rows:
        return 1
    return 0 if all(row["verdict"] in ANSWERS for row in rows) else 1


def write_log(log_dir, mutation, log):
    os.makedirs(log_dir, exist_ok=True)
    with open(os.path.join(log_dir, mutation["decision"] + ".log"), "w") as handle:
        handle.write("\n".join(log))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("table", help="the mutation table, a JSON file")
    parser.add_argument("--only", action="append", default=[],
                        help="run this decision alone; repeatable")
    parser.add_argument("--check", action="store_true",
                        help="verify every anchor and build nothing")
    parser.add_argument("--root", default=".", help="the top of the worktree")
    parser.add_argument("--runner", default="tools/vm run",
                        help="how a shell command reaches the build")
    parser.add_argument("--log-dir", default="build/mutate-logs",
                        help="where the whole output of each round is kept")
    args = parser.parse_args()
    root = os.path.abspath(args.root)
    with open(os.path.join(root, args.table) if not os.path.isabs(args.table)
              else args.table) as handle:
        table = json.load(handle)
    if args.check:
        return check_table(table, root)

    saved = {name: read(os.path.join(root, name)) for name in table["sources"]}
    guest = Guest(root, args.runner)
    restore_all(table, saved, root)
    status, out = guest.run(table["build"])
    if status != 0:
        raise SystemExit("mutate: the baseline does not build:\n" + out[-2000:])
    baseline = guest.md5(table["binary"])
    print("baseline %s %s" % (table["binary"], baseline), flush=True)
    wanted = set(args.only)
    rows = []
    unknown = sorted(wanted - {m["decision"] for m in table["mutations"]})
    try:
        rows = run_table(table, guest, root, saved, baseline, args.log_dir, wanted)
    finally:
        back = restore_and_check(table, guest, root, saved, rows, baseline)
    if unknown:
        print("mutate: no row is named %s" % ", ".join(unknown))
    if not rows:
        print("mutate: no row ran")
    return exit_status(rows, unknown, back, baseline)


if __name__ == "__main__":
    sys.exit(main())
