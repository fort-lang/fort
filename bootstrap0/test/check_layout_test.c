// Tests lazy struct layout and its resolution paths.
// A stored struct value requires layout. A reference or function signature does not.
// Other suites test infinite-size diagnostics and written layout symbols.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "check.h"
#include "common/check_helpers.h"
#include "sym.h"
#include "types.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the sizes and offsets below are the
// layout the tests state.

// A module with declarations `first` and `second`, followed by a main that reads both sizes.
// The next call invalidates the returned shared-buffer result.
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

// A span of pointers to a struct that holds the span by value. Neither size
// depends on the other, so both orders must check.
TEST(a_span_of_pointers_is_laid_out_in_either_order, {
    const char* vec = "struct a {\n    b mut* mut@ own items;\n}";
    const char* node = "struct b {\n    a list;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(vec, node)));
    // A span is a fat pointer, two words; the struct holding it is the same
    // two words.
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
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
})

TEST(a_pointer_to_an_array_is_laid_out_in_either_order, {
    // The array stands behind the pointer, so the field is one word and the
    // element's size is not part of this struct's layout.
    const char* holder = "struct a {\n    b[3]* block;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

// A group stores what it groups (D3.6). A grouped pointer is one word whatever its pointee is, and
// a grouped array of function pointers holds its pointers by value. The offsets are C's for the
// same fields without the parentheses.
TEST(a_grouped_field_is_laid_out_as_the_type_it_groups, {
    const char* holder = "struct a {\n    u8 tag;\n    (fn (b) i32)[2] table;\n"
                         "    ((b mut*)) link;\n    ((u8)) last;\n}";
    const char* node = "struct b {\n    a value;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)40);
    TEST_ASSERT_EQ_UINT64(field_offset("table"), (uint64_t)8);
    TEST_ASSERT_EQ_UINT64(field_offset("link"), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(field_offset("last"), (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(struct_size("b"), (uint64_t)40);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)40);
    TEST_ASSERT_EQ_UINT64(field_offset("last"), (uint64_t)32);
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)0);
})

// A group around a struct stores the struct by value, so it is a layout edge as the plain field
// is, and a group around a pointer to it is not.
TEST(a_grouped_struct_field_is_a_layout_edge, {
    const char* holder = "struct a {\n    i32 tag;\n    ((b)) part;\n}";
    const char* node = "struct b {\n    i64 v;\n    (a)* back;\n}";
    TEST_ASSERT_TRUE(check_src(two_structs(holder, node)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(field_offset("part"), (uint64_t)8);
    TEST_ASSERT_TRUE(check_src(two_structs(node, holder)));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
    TEST_ASSERT_EQ_UINT64(struct_size("b"), (uint64_t)16);
    TEST_ASSERT_FALSE(
        check_src(two_structs("struct a {\n    ((b)) part;\n}", "struct b {\n    (a) whole;\n}")));
    TEST_ASSERT_TRUE(said("infinite size"));
})

TEST(a_function_type_parameter_is_laid_out_in_either_order, {
    // A function pointer is a word and its signature stores nothing, so a
    // struct named in it is not contained by value.
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
    // Nothing forces b's resolution while a is laid out. The second phase is what resolves it: its
    // fields must still carry their symbols and their offsets.
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

TEST(an_error_inside_a_struct_reached_only_through_a_pointer_is_still_reported, {
    // No lazy resolution edge reaches `b`.
    // The second phase must resolve it once and report its error once.
    TEST_ASSERT_FALSE(check_src("struct a {\n    b* link;\n}\n"
                                "struct b {\n    nosuch x;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("unknown type 'nosuch'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_TRUE(sym_main("b")->error);
})

TEST(a_self_infinite_struct_reached_only_through_a_pointer_is_still_reported, {
    // The same for the infinite size itself: `b` contains itself by value and nothing outside it
    // stores a `b`. The diagnostic is the second phase's too.
    TEST_ASSERT_FALSE(check_src("struct a {\n    b* link;\n}\n"
                                "struct b {\n    b inner;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("struct b has infinite size"));
    // At b's `struct` keyword, which is line 4 column 1.
    TEST_ASSERT_TRUE(said("main.ft:4:1: error:"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    // `a` holds a pointer to a struct that failed, and a pointer is a word whatever it points at.
    // `a` itself is still laid out.
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)8);
})

TEST(an_owning_aggregate_is_recognised_in_either_order, {
    // The layout must give the same ownership result in either order.
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

TEST(a_struct_containing_itself_is_infinite, {
    TEST_ASSERT_FALSE(check_src("struct a {\n    i32 v;\n    a self;\n}\n"
                                "fn main() i32 {\n    return 0;\n}\n"));
    TEST_ASSERT_TRUE(said("struct a has infinite size"));
    // At the `struct` keyword, line 1 column 1.
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
    // A fixed array stores its elements by value and needs the later struct's size.
    // Layout resolves `b` before it completes `a`.
    TEST_ASSERT_TRUE(check_src("struct a {\n    b[3] row;\n}\n"
                               "struct b {\n    i32 x;\n    i32 y;\n}\n"
                               "fn main() i32 {\n    return cast(sizeof(a), i32);\n}\n"));
    TEST_ASSERT_EQ_UINT64(struct_size("a"), (uint64_t)24);
})

TEST(a_forward_enum_field_is_still_resolved, {
    // An enum is four bytes whatever its members are, but its members are
    // values and the field's use of them must still resolve.
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
    TEST_RUN(a_grouped_field_is_laid_out_as_the_type_it_groups);
    TEST_RUN(a_grouped_struct_field_is_a_layout_edge);
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
