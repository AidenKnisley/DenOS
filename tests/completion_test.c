#include "completion.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int has_item(CCompletionItem *items, int count, const char *word, int kind) {
    for (int i = 0; i < count; ++i)
        if (strcmp(items[i].text, word) == 0 && items[i].kind == kind) return 1;
    return 0;
}

int main(void) {
    static const char source[] =
        "int process_data(int value) {\n"
        "  int previous_value = value;\n"
        "  return previous_value;\n"
        "}\n"
        "// process_comment must not be suggested\n"
        "char *s = \"process_string\";\n"
        "/* process_block_comment */\n";
    CCompletionItem items[CCOMPLETE_MAX_ITEMS];
    int count = c_completion_collect(source, sizeof(source) - 1, "pr", items,
                                     CCOMPLETE_MAX_ITEMS);
    assert(count > 0);
    assert(has_item(items, count, "printf", CCOMPLETE_FUNCTION));
    assert(has_item(items, count, "process_data", CCOMPLETE_FUNCTION));
    assert(!has_item(items, count, "process_comment", CCOMPLETE_SYMBOL));
    assert(!has_item(items, count, "process_string", CCOMPLETE_SYMBOL));
    assert(!has_item(items, count, "process_block_comment", CCOMPLETE_SYMBOL));

    /* Same-buffer identifiers must be offered ahead of generic built-ins. */
    count = c_completion_collect(source, sizeof(source) - 1, "pr", items, 1);
    assert(count == 1);
    assert(has_item(items, count, "process_data", CCOMPLETE_FUNCTION));

    /* The suggestion catalog includes the full Aiden C ctype built-in set. */
    count = c_completion_collect("", 0, "isp", items, CCOMPLETE_MAX_ITEMS);
    assert(count == 2);
    assert(has_item(items, count, "isprint", CCOMPLETE_FUNCTION));
    assert(has_item(items, count, "ispunct", CCOMPLETE_FUNCTION));
    count = c_completion_collect("", 0, "isg", items, CCOMPLETE_MAX_ITEMS);
    assert(has_item(items, count, "isgraph", CCOMPLETE_FUNCTION));
    count = c_completion_collect("", 0, "isc", items, CCOMPLETE_MAX_ITEMS);
    assert(has_item(items, count, "iscntrl", CCOMPLETE_FUNCTION));

    count = c_completion_collect(source, sizeof(source) - 1, "pre", items, 2);
    assert(count <= 2);
    assert(has_item(items, count, "previous_value", CCOMPLETE_SYMBOL));

    count = c_completion_collect(source, sizeof(source) - 1,
                                 "this_identifier_is_far_too_long_to_fit", items,
                                 CCOMPLETE_MAX_ITEMS);
    assert(count == 0);
    count = c_completion_collect(source, sizeof(source) - 1, "x", items,
                                 CCOMPLETE_MAX_ITEMS);
    assert(count == 0);

    puts("C completion tests passed: prioritized source symbols, C keywords, Aiden C VM built-ins, comment/string exclusion, prefix bounds, and output capacity.");
    return 0;
}
