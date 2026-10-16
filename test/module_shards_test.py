#!/usr/bin/env python3
"""Check that the shards of a corpus run each of its tests exactly once.

The build runs the corpus test/fort as PARTS ctest tests, and each one passes
`--shard K/PARTS` to run_tests.py. This test lists the corpus whole and in each
part, with `--list`, and holds the parts against the whole: they are disjoint,
they hold every test, and their sizes differ by one at most. `--list` needs no
compiler, so the test runs before the build.

The CI job `modules` in .github/workflows/fort.yml runs one part on each runner.
This test also reads that job. Its matrix `shard` must list each part 1..PARTS
once, and the part name in its `ctest -R` and in its job name must hold PARTS.
"""

import argparse
import importlib.util
import re
import subprocess
import sys
import unittest


OPTIONS = None


def listed(*extra):
    """The paths that `run_tests.py --list` selects, in its order."""
    argv = [sys.executable, OPTIONS.harness, "--root", OPTIONS.root, "--list", *extra]
    proc = subprocess.run(argv, capture_output=True, text=True, check=True)
    lines = proc.stdout.splitlines()
    # The last line counts the paths above it.
    count = int(lines[-1].split()[0])
    paths = lines[:-1]
    if count != len(paths):
        raise AssertionError("%s: %d paths under '%s'" % (argv, len(paths), lines[-1]))
    return paths


def all_parts():
    """The listed paths of each part, from part 1 to part PARTS."""
    return [listed("--shard", "%d/%d" % (k, OPTIONS.parts)) for k in range(1, OPTIONS.parts + 1)]


def usage_status(value):
    """The exit status of run_tests.py for `--shard <value>`."""
    argv = [sys.executable, OPTIONS.harness, "--root", OPTIONS.root, "--list"]
    argv += ["--shard", value]
    return subprocess.run(argv, capture_output=True, text=True).returncode


def job_block(text, job):
    """The lines of job `job` under `jobs:`, from its key to the next job key."""
    lines = text.splitlines()
    starts = [n for n, line in enumerate(lines) if re.match(r"^  [A-Za-z0-9_-]+:\s*$", line)]
    for n in starts:
        if lines[n].strip() == job + ":":
            later = [m for m in starts if m > n]
            return lines[n : later[0] if later else len(lines)]
    raise ValueError("no job '%s'" % job)


def workflow_problems(text, parts):
    """The problems of the job `modules` against PARTS parts; empty when it runs each once."""
    block = job_block(text, "modules")
    problems = []
    lists = [re.match(r"^\s+shard:\s*\[(.*)\]\s*$", line) for line in block]
    lists = [m.group(1) for m in lists if m]
    if len(lists) != 1:
        return ["the job holds %d lines 'shard: [...]', not 1" % len(lists)]
    items = [item.strip() for item in lists[0].split(",") if item.strip()]
    if not all(item.isdigit() for item in items):
        return ["the matrix shard [%s] holds an item that is not a number" % lists[0]]
    shards = sorted(int(item) for item in items)
    if shards != list(range(1, parts + 1)):
        problems.append("the matrix shard [%s] is not each of 1..%d once" % (lists[0], parts))
    joined = "\n".join(block)
    counts = re.findall(r"fort-modules-\$\{\{ matrix\.shard \}\}-of-(\d+)", joined)
    if counts != [str(parts)]:
        problems.append("the ctest pattern holds -of-%s, not one -of-%d" % (counts, parts))
    names = re.findall(r"\$\{\{ matrix\.shard \}\}/(\d+)\)", joined)
    if names != [str(parts)]:
        problems.append("the job name holds /%s, not one /%d" % (names, parts))
    return problems


def harness_module():
    spec = importlib.util.spec_from_file_location("run_tests", OPTIONS.harness)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ShardTest(unittest.TestCase):
    def test_parts_hold_each_test_once(self):
        whole = listed()
        parts = all_parts()
        joined = [path for part in parts for path in part]
        self.assertEqual(len(joined), len(set(joined)), "a test stands in two parts")
        self.assertEqual(sorted(joined), sorted(whole))
        print(
            "%d tests in %d parts of %s"
            % (len(whole), OPTIONS.parts, ", ".join(str(len(p)) for p in parts))
        )

    def test_parts_differ_by_one_test_at_most(self):
        sizes = [len(part) for part in all_parts()]
        self.assertLessEqual(max(sizes) - min(sizes), 1)

    def test_shard_takes_every_nth_position(self):
        shard = harness_module().shard
        self.assertEqual(shard(list(range(7)), 1, 3), [0, 3, 6])
        self.assertEqual(shard(list(range(7)), 2, 3), [1, 4])
        self.assertEqual(shard(list(range(7)), 3, 3), [2, 5])
        self.assertEqual(shard(list(range(2)), 3, 3), [])
        self.assertEqual(shard(list(range(4)), 1, 1), [0, 1, 2, 3])

    def test_parse_shard_reads_k_of_n(self):
        parse = harness_module().parse_shard
        self.assertEqual(parse("1/2"), (1, 2))
        self.assertEqual(parse("2/2"), (2, 2))
        self.assertEqual(parse("1/1"), (1, 1))
        for bad in ("0/2", "3/2", "1/0", "2", "1/", "/2", "a/b", "-1/2", "1/2/3", ""):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                parse(bad)

    def test_workflow_runs_each_part_once(self):
        with open(OPTIONS.workflow) as file:
            self.assertEqual(workflow_problems(file.read(), OPTIONS.parts), [])

    def test_bad_workflow_matrix_is_a_problem(self):
        with open(OPTIONS.workflow) as file:
            good = file.read()
        line = re.search(r"^\s+shard:\s*\[.*\]\s*$", good, re.M).group(0)
        indent = line[: len(line) - len(line.lstrip())]
        name = "fort-modules-${{ matrix.shard }}-of-%d" % OPTIONS.parts
        bad_lists = ["[1]", "[1, 1]", "[2]", "[0, 1]", "[1, %d]" % (OPTIONS.parts + 1)]
        bad_lists.append("[%s]" % ", ".join(str(k) for k in range(1, OPTIONS.parts + 2)))
        for bad in bad_lists:
            with self.subTest(shard=bad):
                text = good.replace(line, indent + "shard: " + bad)
                self.assertNotEqual(workflow_problems(text, OPTIONS.parts), [])
        with self.subTest(case="no shard line"):
            text = good.replace(line + "\n", "")
            self.assertNotEqual(workflow_problems(text, OPTIONS.parts), [])
        with self.subTest(case="pattern of another count"):
            text = good.replace(
                name, "fort-modules-${{ matrix.shard }}-of-%d" % (OPTIONS.parts + 1)
            )
            self.assertNotEqual(workflow_problems(text, OPTIONS.parts), [])
        with self.subTest(case="name of another count"):
            text = good.replace("${{ matrix.shard }}/%d)" % OPTIONS.parts, "${{ matrix.shard }}/9)")
            self.assertNotEqual(workflow_problems(text, OPTIONS.parts), [])
        with self.subTest(case="no job modules"):
            with self.assertRaises(ValueError):
                workflow_problems(good.replace("\n  modules:\n", "\n  parts:\n"), OPTIONS.parts)

    def test_shuffled_matrix_is_no_problem(self):
        with open(OPTIONS.workflow) as file:
            good = file.read()
        line = re.search(r"^\s+shard:\s*\[.*\]\s*$", good, re.M).group(0)
        indent = line[: len(line) - len(line.lstrip())]
        shuffled = ", ".join(str(k) for k in range(OPTIONS.parts, 0, -1))
        text = good.replace(line, indent + "shard: [%s]" % shuffled)
        self.assertEqual(workflow_problems(text, OPTIONS.parts), [])

    def test_bad_shard_is_a_usage_error(self):
        for bad in ("0/2", "3/2", "1/0", "2", "a/b"):
            with self.subTest(bad=bad):
                self.assertEqual(usage_status(bad), 2)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--harness", required=True, help="test/lang/run_tests.py")
    parser.add_argument("--root", required=True, help="the corpus root, such as test/fort")
    parser.add_argument("--parts", type=int, required=True, help="the number of shards")
    parser.add_argument("--workflow", required=True, help=".github/workflows/fort.yml")
    OPTIONS, rest = parser.parse_known_args()
    if OPTIONS.parts < 1:
        parser.error("--parts must be 1 or more")
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)


if __name__ == "__main__":
    main()
