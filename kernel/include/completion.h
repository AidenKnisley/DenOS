#ifndef DENOS_COMPLETION_H
#define DENOS_COMPLETION_H

#include "types.h"

#define CCOMPLETE_WORD_CAP 32
#define CCOMPLETE_MAX_ITEMS 24

enum {
    CCOMPLETE_KEYWORD = 1,
    CCOMPLETE_TYPE = 2,
    CCOMPLETE_FUNCTION = 3,
    CCOMPLETE_SYMBOL = 4
};

typedef struct {
    char text[CCOMPLETE_WORD_CAP];
    u8 kind;
} CCompletionItem;

/* Collect bounded C/Aiden-C suggestions for prefix. Source identifiers are
 * discovered outside comments and literals. Returns the number of results. */
int c_completion_collect(const char *source, usize source_length,
                         const char *prefix, CCompletionItem *items,
                         int capacity);

#endif
