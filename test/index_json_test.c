// Unit tests of the `"symbols"` array the identifier index writes
// (toolchain.md 9.1): the members of a record, their order, the two shapes of
// `"decl"` and the order of the records in the array.
// D20.2, D20.3
//
// The array is compared as one string, since that is what a client reads; the
// file name of a record is the sandbox's, so every expectation is a format
// taking it. The sandbox, the checker and the index are index_helpers.h.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check_helpers.h"
#include "index.h"
#include "index_helpers.h"
#include "json.h"
#include "modules_helpers.h"
#include "str.h"

#include "test.h"

enum { TEXT_CAP = 4096 };

// The `"symbols"` array of the index the last walk left, as the document
// carries it. Valid until the next call.
static const char* symbols_text(void) {
    static sb_t out;
    static bool ready = false;
    if (!ready) {
        sb_init(&out);
        ready = true;
    }
    sb_clear(&out);
    json_t j;
    json_init(&j, &out);
    index_write_json(&ix, &j);
    return sb_cstr(&out);
}

// `<sandbox>/<rel>`, which is the `"file"` of every record of that module.
static const char* file_of(const char* rel) {
    static char path[PATH_CAP];
    join_sandbox_path(path, sizeof path, sandbox, rel);
    return path;
}

// An expectation taking the entry file's path once, and one taking it twice.
static const char* want1(const char* format) {
    static char text[TEXT_CAP];
    TEST_UNUSED(snprintf(text, sizeof text, format, file_of("main.ft")));
    return text;
}

static const char* want2(const char* format) {
    static char text[TEXT_CAP];
    const char* main_ft = file_of("main.ft");
    char entry[PATH_CAP];
    TEST_UNUSED(snprintf(entry, sizeof entry, "%s", main_ft));
    TEST_UNUSED(snprintf(text, sizeof text, format, entry, entry));
    return text;
}

// NOLINTBEGIN(readability-magic-numbers) the ranges in the expected documents
// are the test data: they are read off the fixture of each test.

TEST(an_empty_index_writes_an_empty_array, {
    index_reset();
    index_init(&ix);
    index_live = true;
    // The index is a member of every document and is empty without --index.
    // D20.2, D20.3
    TEST_ASSERT_EQ_STR(symbols_text(), "[]");
})

TEST(a_record_writes_every_member_in_order, {
    TEST_ASSERT_TRUE(index_src("fn main() i32 { return 0; }\n"));
    // The range, the name, the kind, the type, whether it is the declaration
    // and the declaration's range, in the order.
    // D20.3
    TEST_ASSERT_EQ_STR(symbols_text(),
                       want2("[{\"file\":\"%s\",\"line\":1,\"col\":4,\"end_line\":1,"
                             "\"end_col\":8,\"name\":\"main\",\"kind\":\"fn\","
                             "\"type\":\"fn () i32\",\"is_decl\":true,\"decl\":{\"file\":\"%s\","
                             "\"line\":1,\"col\":4,\"end_line\":1,\"end_col\":8}}]"));
})

TEST(a_builtin_writes_a_null_declaration, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn say() void { println(1); }\n"));
    // No source declares a universe function, and a record with no type
    // writes the empty string rather than dropping the member.
    // D12.2, D20.3
    TEST_ASSERT_NONNULL(strstr(symbols_text(),
                               "\"name\":\"println\",\"kind\":\"builtin\",\"type\":\"\","
                               "\"is_decl\":false,\"decl\":null}"));
})

TEST(a_module_writes_the_empty_range_of_its_file, {
    begin();
    add("util.ft", "i32 LIMIT = 4;\n");
    add("main.ft", "import util;\nfn main() i32 { return util.LIMIT; }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    // A module is declared by a file and has no name token, so a client that
    // follows the record opens the file at 1:1.
    // D9.1, D20.3
    char want[TEXT_CAP];
    TEST_UNUSED(snprintf(want,
                         sizeof want,
                         "\"name\":\"util\",\"kind\":\"module\",\"type\":\"\","
                         "\"is_decl\":false,\"decl\":{\"file\":\"%s\",\"line\":1,\"col\":1,"
                         "\"end_line\":1,\"end_col\":1}}",
                         file_of("util.ft")));
    TEST_ASSERT_NONNULL(strstr(symbols_text(), want));
})

TEST(the_records_are_written_in_the_index_order, {
    TEST_ASSERT_TRUE(index_src("fn main() i32 {\n    i32 n = 1;\n    return n;\n}\n"));
    const char* text = symbols_text();
    const char* first = strstr(text, "\"name\":\"main\"");
    const char* second = strstr(text,
                                "\"name\":\"n\",\"kind\":\"local\",\"type\":\"i32\","
                                "\"is_decl\":true");
    const char* third = strstr(text,
                               "\"name\":\"n\",\"kind\":\"local\",\"type\":\"i32\","
                               "\"is_decl\":false");
    TEST_ASSERT_NONNULL(first);
    TEST_ASSERT_NONNULL(second);
    TEST_ASSERT_NONNULL(third);
    // The array is the index in order: the declaration before the use, both
    // after the function that holds them.
    // D20.3
    TEST_ASSERT_TRUE(first < second);
    TEST_ASSERT_TRUE(second < third);
})

TEST(a_type_with_a_marker_is_written_as_a_declaration_spells_it, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn f(i32 mut* own p) void { del(p); }\n"));
    // The spelling is type_to_str_decl's, level-0 mutability included.
    // D5.2, D5.3
    TEST_ASSERT_NONNULL(
        strstr(symbols_text(), "\"name\":\"p\",\"kind\":\"parameter\",\"type\":\"i32 mut* own\""));
})

TEST(the_array_nests_under_the_symbols_key_of_a_document, {
    TEST_ASSERT_TRUE(index_src("fn main() i32 { return 0; }\n"));
    sb_t out;
    sb_init(&out);
    json_t j;
    json_init(&j, &out);
    json_object_begin(&j);
    json_key(&j, "symbols");
    index_write_json(&ix, &j);
    json_object_end(&j);
    // The writer emits the value of a key the caller has written, which is
    // how the document holds it.
    // D20.2
    const char* text = sb_cstr(&out);
    TEST_ASSERT_NONNULL(strstr(text, "{\"symbols\":[{\"file\":"));
    TEST_ASSERT_EQ_STR(text + strlen(text) - 3, "}]}");
    sb_free(&out);
})

TEST(a_document_of_a_module_with_an_error_still_carries_what_resolved, {
    TEST_ASSERT_FALSE(index_src("fn main() i32 {\n    i32 n = nope;\n    return n;\n}\n"));
    TEST_ASSERT_TRUE(said("unknown name 'nope'"));
    const char* text = symbols_text();
    // Everything the checker resolved is in the index; the name it could not
    // resolve is in none of it.
    // D20.3
    TEST_ASSERT_NONNULL(strstr(text, "\"name\":\"n\",\"kind\":\"local\""));
    TEST_ASSERT_NULL(strstr(text, "nope"));
})

TEST(one_record_per_occurrence_and_no_more, {
    want_main = false;
    TEST_ASSERT_TRUE(index_src("fn twice(i32 n) i32 { return n + n; }\n"));
    const char* text = symbols_text();
    uint64_t records = 0;
    // Every record has one "name" and its nested "decl" has none, so the
    // member counts the records of the array.
    for (const char* p = strstr(text, "\"name\":"); p != NULL; p = strstr(p + 1, "\"name\":")) {
        records++;
    }
    // `twice`, `n` and its two uses: one record per identifier occurrence the
    // checker resolved and none for anything else.
    // D20.3
    TEST_ASSERT_EQ_UINT64(records, (uint64_t)4);
    TEST_ASSERT_EQ_UINT64(index_count(&ix), (uint64_t)4);
})

TEST(an_alias_declares_a_name_here_for_a_declaration_elsewhere, {
    begin();
    add("util.ft", "fn twice(i32 n) i32 { return n + n; }\n");
    add("main.ft", "import util.twice as double;\nfn main() i32 { return double(2); }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    char want[TEXT_CAP];
    TEST_UNUSED(snprintf(want,
                         sizeof want,
                         "\"name\":\"double\",\"kind\":\"fn\",\"type\":\"fn (i32) i32\","
                         "\"is_decl\":true,\"decl\":{\"file\":\"%s\",\"line\":1,\"col\":4,"
                         "\"end_line\":1,\"end_col\":9}}",
                         file_of("util.ft")));
    // The alias declares `double` in this module while the declaration it
    // binds stands in the other file, so a rename anchors here and a jump
    // lands there.
    // D9.3, D20.3
    TEST_ASSERT_NONNULL(strstr(symbols_text(), want));
})

TEST(a_declaration_that_failed_to_check_writes_a_null_type, {
    TEST_ASSERT_FALSE(index_src("fn main() i32 {\n    nope n = 1;\n    return 0;\n}\n"));
    // `null` is the type of a declaration that failed and `""` the type of a
    // name that denotes no value type, so a client tells them apart.
    // D20.3
    TEST_ASSERT_NONNULL(strstr(symbols_text(), "\"name\":\"n\",\"kind\":\"local\",\"type\":null,"));
    TEST_ASSERT_NULL(strstr(symbols_text(), "<error>"));
})

TEST(the_array_carries_the_records_of_every_file_of_the_closure, {
    begin();
    add("util.ft", "i32 LIMIT = 4;\n");
    add("main.ft", "import util;\nfn main() i32 { return util.LIMIT; }\n");
    TEST_ASSERT_TRUE(check_entry("main.ft"));
    index_closure();
    const char* text = symbols_text();
    const char* imported = strstr(text, file_of("util.ft"));
    const char* entry = strstr(text, file_of("main.ft"));
    TEST_ASSERT_NONNULL(imported);
    TEST_ASSERT_NONNULL(entry);
    // One array holds every module of the closure, the imported one first.
    // D9.10, D20.3
    TEST_ASSERT_TRUE(imported < entry);
})

TEST(a_record_of_the_entry_names_the_file_the_compiler_opened, {
    TEST_ASSERT_TRUE(index_src("fn main() i32 { return 0; }\n"));
    // The file is spelled as the `<file>` of a diagnostic is, so a client
    // matches a record against the document's "files".
    // D14.2, D20.2
    TEST_ASSERT_NONNULL(strstr(symbols_text(), want1("[{\"file\":\"%s\",")));
})

// NOLINTEND(readability-magic-numbers)

int main(int argc, char** argv) {
    TEST_INIT("index_json", argc, argv);
    TEST_RUN(an_empty_index_writes_an_empty_array);
    TEST_RUN(a_record_writes_every_member_in_order);
    TEST_RUN(a_builtin_writes_a_null_declaration);
    TEST_RUN(a_module_writes_the_empty_range_of_its_file);
    TEST_RUN(the_records_are_written_in_the_index_order);
    TEST_RUN(a_type_with_a_marker_is_written_as_a_declaration_spells_it);
    TEST_RUN(the_array_nests_under_the_symbols_key_of_a_document);
    TEST_RUN(a_document_of_a_module_with_an_error_still_carries_what_resolved);
    TEST_RUN(one_record_per_occurrence_and_no_more);
    TEST_RUN(an_alias_declares_a_name_here_for_a_declaration_elsewhere);
    TEST_RUN(a_declaration_that_failed_to_check_writes_a_null_type);
    TEST_RUN(the_array_carries_the_records_of_every_file_of_the_closure);
    TEST_RUN(a_record_of_the_entry_names_the_file_the_compiler_opened);
    index_reset();
    check_reset();
    done();
    TEST_EXIT();
}
