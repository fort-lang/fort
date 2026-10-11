#!/usr/bin/env python3
"""Test tools/fact_joins.py over small heap modules that each seed one class of problem."""

import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fact_joins", REPOSITORY / "tools/fact_joins.py")
joins = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(joins)

# A heap module that holds each rule: each struct of STRUCTS, each join function, each graft
# function, each fact type, and one established site of each table.
GOOD = """\
import ownership_facts;
import ownership_facts.{nullness, status_fact, may_set};

struct atom {
    // A comment line above a field is no declaration.
    atom_kind kind; // join: same
    status_fact status;
    nullness root;
    bool fresh; // join: may
    bool strict; // join: must
    u32 into; // join: none
}

struct slot_key {
    u32 id;
}

struct value {
    value_kind kind; // join: same
    u32 atom; // join: owner
    ownership_facts.nullness nullness;
}

struct state {
    bool reached; // join: may
    atom mut@ own atoms; // join: owner
    may_set released;
    ownership_facts.must_set held;
}

struct summary {
    ownership_api.proof_status proof; // join: none
    state post; // join: owner
}

struct interface_entry {
    slot_key key; // join: same
    bool require_null; // join: may
}

fn join_atom(atom a, atom b, bool witness_a, bool witness_b) atom {
    atom mut out = a; // join: copy
    out.status = ownership_facts.join_status_fact(a.status,
        b.status);
    out.root = join_nullness(a.root, b.root);
    if (witness_a && witness_b) {
        out.root.value = root_kind.free;
    }
    out.fresh = a.fresh || b.fresh;
    out.strict = a.strict && b.strict;
    if (a.kind != b.kind) { out.kind = atom_kind.opaque; }
    return out;
}

fn join_values(state mut* out, value vx, value vy) value {
    value mut v = vx; // join: copy
    v.nullness = ownership_facts.join_nullness(vx.nullness, vy.nullness);
    if (v.atom == 0) { return v; }
    return v;
}

fn join(state* a, state* b, state mut* out) bool {
    state mut joined = state{};
    joined.reached = a->reached || b->reached;
    joined.released = ownership_facts.join_may_set(a->released, b->released);
    joined.held = ownership_facts.join_must_set(a->held, b->held);
    joined.atoms[0].root = ownership_facts.join_nullness(a->atoms[0].root, b->atoms[0].root);
    joined.atoms[1] = a->atoms[1]; // join: copy
    joined.atoms[2] = join_atom(a->atoms[2], b->atoms[2], true, true);
    return joined.reached == a->reached;
}

fn graft_value(value v) value {
    value mut out = v;
    out.nullness = ownership_facts.assume(out.nullness);
    return out;
}

fn apply(state mut* s, atom p) void {
    atom mut made = p;
    made.root = ownership_facts.assume(p.root);
    made.status = ownership_facts.assume_status(p.status);
    s->atoms[0].root = nullness{root_kind.nonnull, provenance.assumed};
}

fn null_value() value {
    value mut v = value{};
    v.nullness = nullness{root_kind.null_root, provenance.established};
    return v;
}

fn release(state mut* s) void {
    s->released.bits = 1;
    s->atoms[0].root.how = provenance.established;
    // A comment may name provenance.established and null_value().
    s->atoms[1].status = status_fact{atom_status.released, "provenance.established"};
}

fn moved(state mut* s) void {
    s->slots[0].v = null_value();
}
"""
# The tables of allowed sites for GOOD.
ESTABLISHED = {"null_value": 1, "release": 1}
NULL_VALUES = {"moved": 1}


def run(text, established=None, null_values=None):
    """The exit status, the report and the problems of the tool over one source text."""
    out = io.StringIO()
    err = io.StringIO()
    status = joins.run(text, out, err,
                       ESTABLISHED if established is None else established,
                       NULL_VALUES if null_values is None else null_values)
    return status, out.getvalue(), err.getvalue()


def replace(old, new, text=GOOD):
    assert text.count(old) == 1, old
    return text.replace(old, new)


class GoodTest(unittest.TestCase):
    def test_a_module_that_holds_the_rule_passes(self):
        status, out, err = run(GOOD)
        self.assertEqual(status, 0, err)
        self.assertEqual(err, "")
        self.assertIn("fact_joins: 17 fields, 5 typed, 12 tagged, 0 untagged\n", out)
        self.assertIn("fact_joins: 2 provenance.established, 1 null_value() in their "
                      "functions\n", out)
        self.assertTrue(out.endswith("fact_joins: no problems\n"))

    def test_each_field_has_one_row_in_declaration_order(self):
        _, out, _ = run(GOOD)
        rows = [line.split(":")[0] for line in out.splitlines()[:-3]]
        self.assertEqual(rows[:3], ["atom.kind", "atom.status", "atom.root"])
        self.assertEqual(rows[-1], "interface_entry.require_null")
        self.assertEqual(len(rows), 17)
        # A struct outside STRUCTS has no row.
        self.assertNotIn("slot_key.id", rows)

    def test_a_row_counts_the_joins_and_the_partial_writes(self):
        _, out, _ = run(GOOD)
        self.assertIn("atom.root: nullness, join_atom assigns join_nullness 1 time(s), "
                      "1 partial write(s)\n", out)
        self.assertIn("atom.status: status_fact, join_atom assigns join_status_fact 1 time(s)\n",
                      out)
        self.assertIn("value.nullness: nullness, join_values assigns join_nullness 1 time(s)\n",
                      out)
        self.assertIn("state.held: must_set, join assigns join_must_set 1 time(s)\n", out)
        self.assertIn("atom.fresh: tag may, 1 join assignment(s)\n", out)
        self.assertIn("atom.kind: tag same\n", out)
        # A struct without a join function has no join assignment to count.
        self.assertIn("interface_entry.require_null: tag may\n", out)

    def test_a_provenance_write_outside_a_join_function_passes(self):
        # `release` writes `how` and a part of `released`; the join rules hold join functions.
        status, _, err = run(GOOD)
        self.assertEqual(status, 0, err)

    def test_the_heap_module_of_this_checkout_passes(self):
        out = io.StringIO()
        err = io.StringIO()
        status = joins.main(["--root", str(REPOSITORY)], out=out, err=err)
        self.assertEqual(status, 0, err.getvalue())
        self.assertIn(" 0 untagged\n", out.getvalue())
        for row in ("atom.root: nullness", "atom.status: status_fact", "value.nullness: nullness",
                    "state.released: may_set"):
            self.assertIn(row, out.getvalue())
        self.assertIn("fact_joins: %d provenance.established, %d null_value() in their "
                      "functions\n" % (sum(joins.ESTABLISHED_SITES.values()),
                                       sum(joins.NULL_VALUE_SITES.values())), out.getvalue())


class FieldTest(unittest.TestCase):
    def test_a_field_with_no_type_and_no_tag_fails(self):
        status, out, err = run(replace("    u32 into; // join: none\n", "    u32 into;\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.into has no fact type and no `// join:` tag", err)
        self.assertIn("atom.into: NO JOIN\n", out)
        self.assertIn(" 1 untagged\n", out)

    def test_a_comment_without_a_tag_is_no_tag(self):
        status, _, err = run(replace("    u32 into; // join: none\n",
                                     "    u32 into; // an index\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.into has no fact type and no `// join:` tag", err)

    def test_an_unknown_kind_fails(self):
        status, _, err = run(replace("    u32 into; // join: none\n",
                                     "    u32 into; // join: some\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.into has the tag `// join: some`", err)

    def test_two_tags_fail(self):
        status, _, err = run(replace("    u32 into; // join: none\n",
                                     "    u32 into; // join: none same\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.into has the tag", err)
        status, _, err = run(replace("    u32 into; // join: none\n",
                                     "    u32 into; // join: none // join: same\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.into has the tag", err)

    def test_a_typed_field_with_a_tag_fails(self):
        status, _, err = run(replace("    nullness root;\n", "    nullness root; // join: same\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.root has the fact type nullness and the tag same", err)

    def test_a_typed_field_in_a_struct_without_a_join_fails(self):
        status, _, err = run(replace("    bool require_null; // join: may\n",
                                     "    may_set require_null;\n"))
        self.assertEqual(status, 1)
        self.assertIn("interface_entry.require_null has the fact type may_set, but no join "
                      "function joins a interface_entry", err)

    def test_a_pointer_to_a_fact_type_is_no_fact_type(self):
        status, _, err = run(replace("    nullness root;\n", "    nullness* root;\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.root has no fact type and no `// join:` tag", err)

    def test_a_missing_struct_fails(self):
        text = replace("""struct summary {
    ownership_api.proof_status proof; // join: none
    state post; // join: owner
}
""", "")
        status, _, err = run(text)
        self.assertEqual(status, 1)
        self.assertIn("the file declares no struct summary", err)

    def test_an_unreadable_field_line_fails(self):
        status, _, err = run(replace("    u32 into; // join: none\n",
                                     "    u32 into = 0; // join: none\n"))
        self.assertEqual(status, 1)
        self.assertIn("struct atom: a field line that the tool cannot read", err)


class JoinTest(unittest.TestCase):
    def test_a_typed_field_without_its_join_fails(self):
        status, _, err = run(replace(
            "    joined.released = ownership_facts.join_may_set(a->released, b->released);\n", ""))
        self.assertEqual(status, 1)
        self.assertIn("state.released has the fact type may_set, but join assigns it from no "
                      "join_may_set", err)

    def test_a_typed_field_joined_by_hand_fails(self):
        status, _, err = run(replace(
            "    out.status = ownership_facts.join_status_fact(a.status,\n        b.status);\n",
            "    out.status = b.status;\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_atom assigns atom.status without join_status_fact", err)
        self.assertIn("atom.status has the fact type status_fact, but join_atom assigns it from "
                      "no join_status_fact", err)

    def test_a_second_assignment_by_hand_fails(self):
        status, _, err = run(replace(
            "    out.fresh = a.fresh || b.fresh;\n",
            "    out.fresh = a.fresh || b.fresh;\n    if (witness_a) { out.root = b.root; }\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_atom assigns atom.root without join_nullness", err)

    def test_the_join_of_another_type_fails(self):
        status, _, err = run(replace(
            "    joined.held = ownership_facts.join_must_set(a->held, b->held);\n",
            "    joined.held = ownership_facts.join_may_set(a->held, b->held);\n"))
        self.assertEqual(status, 1)
        self.assertIn("join assigns state.held without join_must_set", err)

    def test_a_typed_field_of_another_struct_in_a_join_function_needs_its_join(self):
        status, _, err = run(replace(
            "    joined.atoms[0].root = ownership_facts.join_nullness(a->atoms[0].root, "
            "b->atoms[0].root);\n",
            "    joined.atoms[0].root = a->atoms[0].root;\n"))
        self.assertEqual(status, 1)
        self.assertIn("join assigns atom.root without join_nullness", err)

    def test_a_write_through_an_arrow_needs_its_join(self):
        status, _, err = run(replace(
            "    return joined.reached == a->reached;\n",
            "    out->released = a->released;\n    return joined.reached == a->reached;\n"))
        self.assertEqual(status, 1)
        self.assertIn("join assigns state.released without join_may_set", err)

    def test_a_one_sided_argument_fails(self):
        # The fact of one side reaches the joined value through another call.
        status, _, err = run(replace(
            "    if (v.atom == 0) { return v; }\n",
            "    if (v.atom == 0) { return address_value(v.atom, vx.nullness); }\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_values reads value.nullness outside join_nullness, so one side "
                      "gives its fact", err)

    def test_a_provenance_written_in_a_join_function_fails(self):
        status, _, err = run(replace(
            "    v.nullness = ownership_facts.join_nullness(vx.nullness, vy.nullness);\n",
            "    v.nullness = ownership_facts.join_nullness(vx.nullness, vy.nullness);\n"
            "    v.nullness.how = provenance.assumed;\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_values assigns a provenance by hand", err)

    def test_a_comparison_is_no_assignment(self):
        status, _, err = run(replace(
            "    if (v.atom == 0) { return v; }\n",
            "    if (v.nullness.value == vx.nullness.value) { return v; }\n"))
        self.assertEqual(status, 0, err)

    def test_a_join_in_a_comment_counts_nothing(self):
        text = replace(
            "    v.nullness = ownership_facts.join_nullness(vx.nullness, vy.nullness);\n",
            "    // v.nullness = ownership_facts.join_nullness(vx.nullness, vy.nullness);\n")
        status, _, err = run(text)
        self.assertEqual(status, 1)
        self.assertIn("value.nullness has the fact type nullness, but join_values assigns it "
                      "from no join_nullness", err)

    def test_a_missing_join_function_fails(self):
        text = replace("fn join_values(state mut* out, value vx, value vy) value {",
                       "fn values_of(state mut* out, value vx, value vy) value {")
        status, _, err = run(text)
        self.assertEqual(status, 1)
        self.assertIn("the file declares no join function join_values", err)


class OperatorTest(unittest.TestCase):
    def test_a_must_flag_tagged_may_fails(self):
        status, _, err = run(replace("    bool strict; // join: must\n",
                                     "    bool strict; // join: may\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.strict has the tag may, but join_atom joins it as "
                      "`a.strict && b.strict`", err)

    def test_a_may_flag_tagged_must_fails(self):
        status, _, err = run(replace("    bool fresh; // join: may\n",
                                     "    bool fresh; // join: must\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.fresh has the tag must, but join_atom joins it as "
                      "`a.fresh || b.fresh`", err)

    def test_a_may_join_with_both_operators_fails(self):
        status, _, err = run(replace("    out.fresh = a.fresh || b.fresh;\n",
                                     "    out.fresh = a.fresh || (b.fresh && witness_b);\n"))
        self.assertEqual(status, 1)
        self.assertIn("atom.fresh has the tag may", err)

    def test_a_union_of_bits_is_a_may_join(self):
        status, _, err = run(replace("    out.fresh = a.fresh || b.fresh;\n",
                                     "    out.fresh = a.fresh | b.fresh;\n"))
        self.assertEqual(status, 0, err)

    def test_a_may_flag_without_a_join_assignment_fails(self):
        status, _, err = run(replace("    out.fresh = a.fresh || b.fresh;\n", ""))
        self.assertEqual(status, 1)
        self.assertIn("atom.fresh has the tag may, but join_atom assigns it nowhere", err)

    def test_an_identity_joined_by_an_operator_fails(self):
        status, _, err = run(replace("    joined.reached = a->reached || b->reached;\n",
                                     "    joined.reached = a->reached || b->reached;\n"
                                     "    joined.atoms_len = a->atoms_len | b->atoms_len;\n",
                                     text=replace("    atom mut@ own atoms; // join: owner\n",
                                                  "    atom mut@ own atoms; // join: owner\n"
                                                  "    u64 atoms_len; // join: owner\n")))
        self.assertEqual(status, 1)
        self.assertIn("state.atoms_len has the tag owner, but join joins it as", err)

    def test_a_tag_of_a_struct_without_a_join_needs_no_assignment(self):
        status, _, err = run(replace("    bool require_null; // join: may\n",
                                     "    bool require_null; // join: must\n"))
        self.assertEqual(status, 0, err)


class RecordTest(unittest.TestCase):
    def test_a_record_copy_without_the_tag_fails(self):
        status, _, err = run(replace("    joined.atoms[1] = a->atoms[1]; // join: copy\n",
                                     "    joined.atoms[1] = a->atoms[1];\n"))
        self.assertEqual(status, 1)
        self.assertIn("join reads a whole record of `atoms` outside join_atom and without "
                      "`// join: copy`", err)

    def test_a_record_in_its_join_needs_no_tag(self):
        status, _, err = run(GOOD)
        self.assertEqual(status, 0, err)
        self.assertNotIn("join_atom(a->atoms[2]", err)

    def test_a_part_of_a_record_is_no_copy(self):
        status, _, err = run(replace("    joined.atoms[1] = a->atoms[1]; // join: copy\n",
                                     "    joined.atoms[1].into = a->atoms[1].into;\n"))
        self.assertEqual(status, 0, err)

    def test_a_record_parameter_without_the_tag_fails(self):
        status, _, err = run(replace("    value mut v = vx; // join: copy\n",
                                     "    value mut v = vx;\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_values reads the whole record `vx` without `// join: copy`", err)
        status, _, err = run(replace("    if (v.atom == 0) { return v; }\n",
                                     "    if (v.atom == 0) { return vy; }\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_values reads the whole record `vy`", err)


class GraftTest(unittest.TestCase):
    def test_a_callee_fact_without_assume_fails(self):
        status, _, err = run(replace("    made.root = ownership_facts.assume(p.root);\n",
                                     "    made.root = p.root;\n"))
        self.assertEqual(status, 1)
        self.assertIn("apply reads atom.root outside join_nullness or assume; the caller "
                      "assumes each callee fact", err)

    def test_a_callee_status_without_assume_fails(self):
        status, _, err = run(replace("    made.status = ownership_facts.assume_status(p.status);\n",
                                     "    made.status = p.status;\n"))
        self.assertEqual(status, 1)
        self.assertIn("apply reads atom.status outside join_status_fact or assume_status", err)

    def test_a_grafted_value_without_assume_fails(self):
        status, _, err = run(replace("    out.nullness = ownership_facts.assume(out.nullness);\n",
                                     "    out.nullness = out.nullness;\n"))
        self.assertEqual(status, 1)
        self.assertIn("graft_value reads value.nullness outside join_nullness or assume", err)

    def test_a_graft_without_its_assume_fails(self):
        status, _, err = run(replace("    out.nullness = ownership_facts.assume(out.nullness);\n",
                                     ""))
        self.assertEqual(status, 1)
        self.assertIn("graft_value assigns no nullness from assume, so a callee fact can pass "
                      "as it is", err)
        status, _, err = run(replace("    made.status = ownership_facts.assume_status(p.status);\n",
                                     ""))
        self.assertEqual(status, 1)
        self.assertIn("apply assigns no status from assume_status", err)

    def test_a_missing_graft_function_fails(self):
        status, _, err = run(replace("fn apply(state mut* s, atom p) void {",
                                     "fn applied(state mut* s, atom p) void {"))
        self.assertEqual(status, 1)
        self.assertIn("the file declares no graft function apply", err)


class SiteTest(unittest.TestCase):
    def test_an_established_literal_outside_the_table_fails(self):
        status, _, err = run(replace(
            "    s->atoms[0].root = nullness{root_kind.nonnull, provenance.assumed};\n",
            "    s->atoms[0].root = nullness{root_kind.nonnull, provenance.established};\n"))
        self.assertEqual(status, 1)
        self.assertIn("apply has 1 provenance.established, and the table allows 0", err)

    def test_a_second_established_site_in_an_allowed_function_fails(self):
        status, _, err = run(replace(
            "    s->released.bits = 1;\n",
            "    s->released.bits = 1;\n"
            "    s->atoms[2].status = status_fact{atom_status.live, provenance.established};\n"))
        self.assertEqual(status, 1)
        self.assertIn("release has 2 provenance.established, and the table allows 1", err)

    def test_a_table_entry_without_its_site_fails(self):
        status, _, err = run(GOOD, established={"null_value": 1, "release": 1, "refine": 2})
        self.assertEqual(status, 1)
        self.assertIn("refine has 0 provenance.established, and the table allows 2", err)

    def test_an_established_global_fails(self):
        status, _, err = run(replace(
            "struct slot_key {\n",
            "nullness SEEN = nullness{root_kind.nonnull, provenance.established};\n\n"
            "struct slot_key {\n"))
        self.assertEqual(status, 1)
        self.assertIn("provenance.established outside a function", err)

    def test_a_null_value_outside_the_table_fails(self):
        status, _, err = run(replace("    if (v.atom == 0) { return v; }\n",
                                     "    if (v.atom == 0) { return null_value(); }\n"))
        self.assertEqual(status, 1)
        self.assertIn("join_values has 1 null_value(), and the table allows 0", err)

    def test_the_declaration_of_null_value_is_no_call(self):
        status, out, err = run(GOOD)
        self.assertEqual(status, 0, err)
        self.assertIn(" 1 null_value() in their functions", out)


class ArgumentTest(unittest.TestCase):
    def test_a_root_without_the_heap_module_is_a_bad_argument(self):
        with tempfile.TemporaryDirectory() as root:
            err = io.StringIO()
            status = joins.main(["--root", root], out=io.StringIO(), err=err)
        self.assertEqual(status, 2)
        self.assertIn("is no file", err.getvalue())

    def test_source_names_the_module(self):
        # The module tables apply to a module given by --source, so GOOD fails their counts.
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "heap.ft"
            path.write_text(GOOD)
            err = io.StringIO()
            status = joins.main(["--root", root, "--source", str(path)], out=io.StringIO(),
                                err=err)
        self.assertEqual(status, 1)
        self.assertIn("release has 1 provenance.established, and the table allows 4",
                      err.getvalue())


if __name__ == "__main__":
    unittest.main()
