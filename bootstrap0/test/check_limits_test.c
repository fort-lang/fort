// Tests the suffix limit of D2.11 in the checker: a type takes 256 suffixes, and
// the parser refuses the 257th before the checker sees the type.
#include <stdbool.h>
#include <stdint.h>

#include "check.h"
#include "common/check_helpers.h"
#include "str.h"
#include "sym.h"
#include "types.h"

#include "common/test.h"

// NOLINTBEGIN(readability-magic-numbers) the suffix counts below are the test data.

static sb_t deep_src;

// The module `<base> <suffix * inner> ... deep = null;` and a `main`. A
// nonzero `inner` puts `base` and the first `inner` suffixes into a group, and
// `outer` more suffixes follow it. D2.11 counts the suffixes of the group and
// its suffixes together.
static const char* deep_decl(const char* base, const char* suffix, uint32_t inner, uint32_t outer) {
    sb_clear(&deep_src);
    if (inner > 0) {
        sb_push(&deep_src, '(');
    }
    sb_append(&deep_src, base);
    for (uint32_t i = 0; i < inner; i++) {
        sb_append(&deep_src, suffix);
    }
    if (inner > 0) {
        sb_push(&deep_src, ')');
    }
    for (uint32_t i = 0; i < outer; i++) {
        sb_append(&deep_src, suffix);
    }
    sb_append(&deep_src, " deep = null;\nfn main() i32 {\n    return 0;\n}\n");
    return sb_cstr(&deep_src);
}

// The number of pointer levels above the type spelled `base` in the type of
// `deep`.
static uint64_t pointers_of_deep(const char* base) {
    const type_t* t = sym_main("deep")->type;
    uint64_t n = 0;
    while (t->kind == TYPE_PTR) {
        n++;
        t = t->elem;
    }
    TEST_ASSERT_EQ_STR(type_text(t), base);
    return n;
}

// The parser refuses the 257th suffix before the checker sees the type, so
// the checker's own refusal never shows.
// It answers false when a frame of the suffix stack stays open.
static bool refused_at_the_nesting_limit(const char* src) {
    return !check_src(src) && said("nesting deeper than 256") && !said("too many type suffixes") &&
           diag_lines() == 1 && checker.suffix_top == 0;
}

// Checks `src` and answers whether the checker took it and closed each frame of
// its suffix stack.
static bool taken(const char* src) {
    return check_src(src) && checker.suffix_top == 0;
}

TEST(a_type_takes_256_suffixes_and_not_257, {
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 0, 256)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 0, 257)));
    // The array suffix counts as one of the 256.
    TEST_ASSERT_TRUE(taken(deep_decl("i32[1]", "*", 0, 255)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32[1]"), (uint64_t)255);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32[1]", "*", 0, 256)));
    sb_free(&deep_src);
})

// The array length is the size of a type of 200 suffixes. The frame of that type
// grows the suffix stack while the frame of `deep` is open, so the stack moves.
TEST(a_type_in_an_array_length_grows_the_suffix_stack, {
    sb_t base;
    sb_init(&base);
    sb_append(&base, "i32[sizeof(i32");
    for (uint32_t i = 0; i < 200; i++) {
        sb_push(&base, '*');
    }
    sb_append(&base, ")]");
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl(sb_cstr(&base), "*", 0, 50)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32[8]"), (uint64_t)50);
    TEST_ASSERT_TRUE(checker.suffix_cap >= (uint64_t)251);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl(sb_cstr(&base), "*", 0, 256)));
    sb_free(&deep_src);
    sb_free(&base);
})

// A frame that fails closes too: an unknown base stops the frame before its
// loop, and a bad array length stops it in the loop.
TEST(a_failed_type_closes_its_suffix_frame, {
    sb_init(&deep_src);
    TEST_ASSERT_FALSE(check_src(deep_decl("nothing", "*", 0, 256)));
    TEST_ASSERT_TRUE(said("unknown type 'nothing'"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(checker.suffix_top, (uint64_t)0);
    TEST_ASSERT_FALSE(check_src(deep_decl("i32[-1]", "*", 0, 255)));
    TEST_ASSERT_TRUE(said("an array length must be greater than 0"));
    TEST_ASSERT_EQ_UINT64(diag_lines(), (uint64_t)1);
    TEST_ASSERT_EQ_UINT64(checker.suffix_top, (uint64_t)0);
    sb_free(&deep_src);
})

TEST(a_group_does_not_reset_the_suffix_count, {
    sb_init(&deep_src);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 128, 128)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(taken(deep_decl("i32", "*", 255, 1)));
    TEST_ASSERT_EQ_UINT64(pointers_of_deep("i32"), (uint64_t)256);
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 128, 129)));
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 256, 1)));
    TEST_ASSERT_TRUE(refused_at_the_nesting_limit(deep_decl("i32", "*", 1, 256)));
    sb_free(&deep_src);
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("check_limits", argc, argv);
    TEST_RUN(a_type_takes_256_suffixes_and_not_257);
    TEST_RUN(a_type_in_an_array_length_grows_the_suffix_stack);
    TEST_RUN(a_failed_type_closes_its_suffix_frame);
    TEST_RUN(a_group_does_not_reset_the_suffix_count);
    check_reset();
    done();
    TEST_EXIT();
}
