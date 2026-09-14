// Unit tests of the checker's lazy struct layout: which field puts another
// struct on the resolution path, and which does not. A struct is resolved
// when a written type stores it by value; a reference suffix and a function
// type's signature store a word and nothing of the struct, so they must leave
// the declaration order free. The diagnostics of an infinite size and of the
// symbols the layout writes are in check_test.c.
// D3.8, D7.10, D3.11, D5.8, D3.10
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "check_helpers.h"
#include "sym.h"
#include "types.h"

#include "test.h"

// NOLINTBEGIN(readability-magic-numbers) the sizes and offsets below are the
// layout the tests state.

// A module holding the two declarations `first` and `second` in that order,
// with a main that reads both sizes, so the pair is written once and checked
// in both orders.
static char layout_source[1024];

static const char* two_structs(const char* first, const char* second) {
    TEST_UNUSED(snprintf(layout_source,
                         sizeof layout_source,
                         "%s\n%s\n"
                         "fn main() i32 {\n"
                         "    return cast(sizeof(a) + sizeof(b), i32);\n"
                         "}\n",
                         first,
                         second));
    return layout_source;
}

// The size of the entry module's struct `name`.
static uint64_t struct_size(const char* name) {
    const sym_t* s = sym_main(name);
    return s != NULL && s->type != NULL ? type_sizeof(s->type) : 0;
}

// The byte offset of the field `name` of the entry module.
static uint64_t field_offset(const char* name) {
    const ast_node_t* f = node_in_main(AST_FIELD_DECL, name);
    return f != NULL ? f->aux : 0;
}

// ---- a reference suffix frees the declaration order ---------------------------------
// D3.11, D5.8

// A span of pointers to a struct that holds the span by value. Neither size
// depends on the other, so both orders must check.
// T-033: the ticket whose program found this shape
TEST(a_span_of_pointers_is_laid_out_in_either_order, {
    const char* vec = "struct a {\n    b mut* mut@ own items;\n}";
    const char* node = "struct b {\n    a list;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(vec, node)));
    // A span is a fat pointer, two words; the struct holding it is the same
    // two words.
    // D3.5
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(struct_size("b"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
    TEST_ASSERT_TRUE(check_src(two_structs(node, vec)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(struct_size("b"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
})

TEST(a_pointer_field_is_laid_out_in_either_order, {
    const char* holder = "struct a {\n    i32 tag;\n    b mut* link;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    // i32 then a pointer at its natural alignment: 4 + 4 padding + 8.
    // D3.8
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(field_offset("link"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(field_offset("link"), (uint64_t)8);
})

TEST(an_array_of_pointers_is_laid_out_in_either_order, {
    const char* holder = "struct a {\n    b mut*[3] links;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    // Three pointers, whatever a `b` is.
    // D3.4
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
})

TEST(a_pointer_to_an_array_is_laid_out_in_either_order, {
    // The array stands behind the pointer, so the field is one word and the
    // element's size is not part of this struct's layout.
    // D3.4, D3.11
    const char* holder = "struct a {\n    b[3]* block;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

TEST(a_function_type_parameter_is_laid_out_in_either_order, {
    // A function pointer is a word and its signature stores nothing, so a
    // struct named in it is not contained by value.
    // D3.10
    const char* holder = "struct a {\n    fn (b) i32 apply;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

TEST(a_function_type_return_is_laid_out_in_either_order, {
    const char* holder = "struct a {\n    fn () b make;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

TEST(a_chain_of_three_structs_is_laid_out_in_either_order, {
    // b is reached from a by value and reaches a again through a span, so the
    // cycle crosses one reference and closes nothing.
    // D3.5
    const char* source = "struct a {\n    b middle;\n}\n"
                         "struct b {\n    c mut@ own edge;\n}\n"
                         "struct c {\n    a back;\n}\n"
                         "fn main() i32 {\n    return cast(sizeof(a), i32);\n}\n";
    const char* reversed = "struct c {\n    a back;\n}\n"
                           "struct b {\n    c mut@ own edge;\n}\n"
                           "struct a {\n    b middle;\n}\n"
                           "fn main() i32 {\n    return cast(sizeof(a), i32);\n}\n";
    TEST_ASSERT_TRUE(check_src(source));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(struct_size("c"), (uint64_t)16);
    TEST_ASSERT_TRUE(check_src(reversed));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)16);
    TEST_ASSERT_EQ_UINT64(struct_size("c"), (uint64_t)16);
})

TEST(a_struct_reached_only_through_a_pointer_still_gets_its_fields, {
    // Nothing forces b's resolution while a is laid out, so the second phase
    // is what resolves it: its fields must still carry their symbols and
    // their offsets.
    // D7.10
    TEST_ASSERT_TRUE(check_src("struct a {\n    b* link;\n}\n"
                               "struct b {\n    i32 tag;\n    i64 value;\n}\n"
                               "fn main() i32 {\n"
                               "    b one = {1, 2};\n    a holder = {&one};\n"
                               "    return cast(holder.link->value, i32);\n}\n"));
    const ast_node_t* f = node_in_main(AST_FIELD_DECL, "value");
    TEST_ASSERT_NONNULL(f);
    TEST_ASSERT_NONNULL(f->sym);
    TEST_ASSERT_EQ_STR(sym_kind_name(f->sym->kind), "field");
    TEST_ASSERT_EQ_UINT64(f->aux, (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(struct_size("b"), (uint64_t)16);
})

// ---- what the laziness must not lose ------------------------------------------------
// D14.2

TEST(an_error_inside_a_struct_reached_only_through_a_pointer_is_still_reported, {
    // The hazard the laziness creates: `b` is on no resolution edge at all
    // now, so the only thing that resolves it is the second phase, and a
    // diagnostic inside it would disappear if that phase ever stopped
    // reaching it. Once, not twice: resolving a declaration twice would
    // report its errors twice.
    // D7.10, D14.2
    TEST_ASSERT_FALSE(check_src("struct a {\n    b* link;\n}\n"
                                "struct b {\n    nosuch x;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("b")->error);
})

TEST(a_self_infinite_struct_reached_only_through_a_pointer_is_still_reported, {
    // The same for the infinite size itself: `b` contains itself by value and
    // nothing outside it stores a `b`, so the diagnostic is the second
    // phase's too.
    // D3.8, D7.10
    TEST_ASSERT_FALSE(check_src("struct a {\n    b* link;\n}\n"
                                "struct b {\n    b inner;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("struct b has infinite size"));
    // At b's `struct` keyword, which is line 4 column 1.
    // D14.2
    TEST_ASSERT_TRUE(said("main.ft:4:1: error:"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // `a` holds a pointer to a struct that failed, and a pointer is a word
    // whatever it points at, so `a` itself is still laid out.
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

TEST(an_owning_aggregate_is_recognised_in_either_order, {
    // The owning answer is read off the layout, so it must be right in the
    // order that used to fail as well.
    // D17.7
    const char* vec = "struct a {\n    b mut* mut@ own items;\n}";
    const char* node = "struct b {\n    a list;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(vec, node)));
    TEST_ASSERT_TRUE(check_owning(sym_main("a")->type));
    TEST_ASSERT_TRUE(check_owning(sym_main("b")->type));
    TEST_ASSERT_TRUE(check_src(two_structs(node, vec)));
    TEST_ASSERT_TRUE(check_owning(sym_main("a")->type));
    TEST_ASSERT_TRUE(check_owning(sym_main("b")->type));
})

// ---- value containment is still a cycle ---------------------------------------------
// D3.8

TEST(a_struct_containing_itself_is_infinite, {
    TEST_ASSERT_FALSE(check_src("struct a {\n    i32 v;\n    a self;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("struct a has infinite size"));
    // At the `struct` keyword, line 1 column 1.
    // D14.2
    TEST_ASSERT_TRUE(said("main.ft:1:1: error:"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_cycle_of_two_structs_is_infinite_in_either_order, {
    const char* holder = "struct a {\n    b inner;\n}";
    const char* node = "struct b {\n    a outer;\n}";
    TEST_ASSERT_FALSE(check_src(two_structs(holder, node)));
    TEST_ASSERT_TRUE(said("has infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("a")->error);
    TEST_ASSERT_TRUE(sym_main("b")->error);
    TEST_ASSERT_FALSE(check_src(two_structs(node, holder)));
    TEST_ASSERT_TRUE(said("has infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("a")->error);
    TEST_ASSERT_TRUE(sym_main("b")->error);
})

TEST(a_cycle_of_three_structs_is_infinite, {
    TEST_ASSERT_FALSE(check_src("struct a {\n    b one;\n}\n"
                                "struct b {\n    c two;\n}\n"
                                "struct c {\n    a three;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("has infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_cycle_through_a_fixed_array_is_infinite_in_either_order, {
    // A fixed array is N of its element and adds no indirection, so it
    // carries the containment through.
    // D3.4
    const char* holder = "struct a {\n    b[2] pair;\n}";
    const char* node = "struct b {\n    a outer;\n}";
    TEST_ASSERT_FALSE(check_src(two_structs(holder, node)));
    TEST_ASSERT_TRUE(said("has infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_FALSE(check_src(two_structs(node, holder)));
    TEST_ASSERT_TRUE(said("has infinite size"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
})

TEST(a_forward_array_of_a_struct_is_still_laid_out, {
    // The positive half of the same rule: an array of a struct declared later
    // needs that struct's size, so the layout must reach it.
    // D3.4, D7.10
    TEST_ASSERT_TRUE(check_src("struct a {\n    b[3] row;\n}\n"
                               "struct b {\n    i32 x;\n    i32 y;\n}\n"
                               "fn main() i32 {\n    return cast(sizeof(a), i32);\n}\n"));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
})

TEST(a_forward_enum_field_is_still_resolved, {
    // An enum is four bytes whatever its members are, but its members are
    // values and the field's use of them must still resolve.
    // D3.9
    TEST_ASSERT_TRUE(check_src("struct a {\n    color tint;\n}\n"
                               "enum color {\n    red,\n    green = 5,\n}\n"
                               "fn main() i32 {\n    a v = {color.green};\n"
                               "    return cast(v.tint, i32) - 5;\n}\n"));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)4);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_layout", argc, argv);
    TEST_RUN(a_span_of_pointers_is_laid_out_in_either_order);
    TEST_RUN(a_pointer_field_is_laid_out_in_either_order);
    TEST_RUN(an_array_of_pointers_is_laid_out_in_either_order);
    TEST_RUN(a_pointer_to_an_array_is_laid_out_in_either_order);
    TEST_RUN(a_function_type_parameter_is_laid_out_in_either_order);
    TEST_RUN(a_function_type_return_is_laid_out_in_either_order);
    TEST_RUN(a_chain_of_three_structs_is_laid_out_in_either_order);
    TEST_RUN(a_struct_reached_only_through_a_pointer_still_gets_its_fields);
    TEST_RUN(an_error_inside_a_struct_reached_only_through_a_pointer_is_still_reported);
    TEST_RUN(a_self_infinite_struct_reached_only_through_a_pointer_is_still_reported);
    TEST_RUN(an_owning_aggregate_is_recognised_in_either_order);
    TEST_RUN(a_struct_containing_itself_is_infinite);
    TEST_RUN(a_cycle_of_two_structs_is_infinite_in_either_order);
    TEST_RUN(a_cycle_of_three_structs_is_infinite);
    TEST_RUN(a_cycle_through_a_fixed_array_is_infinite_in_either_order);
    TEST_RUN(a_forward_array_of_a_struct_is_still_laid_out);
    TEST_RUN(a_forward_enum_field_is_still_resolved);
    check_reset();
    done();
    TEST_EXIT();
}
