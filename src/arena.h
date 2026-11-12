#ifndef TRYTR_ARENA_H
#define TRYTR_ARENA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrArenaBlock TrytrArenaBlock;

typedef struct TrytrArena {
    TrytrArenaBlock *blocks;
    size_t committed_bytes;
    size_t limit;
} TrytrArena;

void trytr_arena_init(TrytrArena *arena, size_t limit);
void *trytr_arena_alloc(TrytrArena *arena, size_t size);
void *trytr_arena_calloc(TrytrArena *arena, size_t count, size_t size);
char *trytr_arena_copy_string(TrytrArena *arena, const char *text, size_t length);
void trytr_arena_destroy(TrytrArena *arena);

#endif
