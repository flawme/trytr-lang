#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct TrytrArenaBlock {
    TrytrArenaBlock *next;
    size_t used;
    size_t capacity;
    max_align_t alignment;
    unsigned char data[];
};

void trytr_arena_init(TrytrArena *arena, size_t limit)
{
    if (arena == NULL) return;
    arena->blocks = NULL;
    arena->committed_bytes = 0;
    arena->limit = limit;
}

void *trytr_arena_alloc(TrytrArena *arena, size_t size)
{
    if (arena == NULL) return NULL;
    const size_t alignment = _Alignof(max_align_t);
    TrytrArenaBlock *block = arena->blocks;

    if (size == 0) {
        size = 1;
    }

    if (block != NULL) {
        const size_t remainder = block->used % alignment;
        const size_t padding = remainder == 0 ? 0 : alignment - remainder;
        if (block->used <= SIZE_MAX - padding) {
            const size_t aligned = block->used + padding;
            if (aligned <= block->capacity && size <= block->capacity - aligned) {
                void *result = block->data + aligned;
                block->used = aligned + size;
                return result;
            }
        }
    }

    if (arena->committed_bytes > arena->limit ||
        sizeof(*block) > arena->limit - arena->committed_bytes) {
        return NULL;
    }
    size_t capacity = size > 4096 ? size : 4096;
    const size_t available = arena->limit - arena->committed_bytes - sizeof(*block);
    if (capacity > available) capacity = available;
    if (size > capacity) return NULL;
    if (capacity > SIZE_MAX - sizeof(*block)) {
        return NULL;
    }

    block = malloc(sizeof(*block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = arena->blocks;
    block->used = size;
    block->capacity = capacity;
    arena->blocks = block;
    arena->committed_bytes += sizeof(*block) + capacity;
    return block->data;
}

void *trytr_arena_calloc(TrytrArena *arena, size_t count, size_t size)
{
    if (arena == NULL) return NULL;
    if (count != 0 && size > SIZE_MAX / count) {
        return NULL;
    }
    const size_t total = count * size;
    void *result = trytr_arena_alloc(arena, total);
    if (result != NULL) {
        memset(result, 0, total);
    }
    return result;
}

char *trytr_arena_copy_string(TrytrArena *arena, const char *text, size_t length)
{
    if (arena == NULL || length == SIZE_MAX || (text == NULL && length != 0)) {
        return NULL;
    }
    char *copy = trytr_arena_alloc(arena, length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0) {
        memcpy(copy, text, length);
    }
    copy[length] = '\0';
    return copy;
}

void trytr_arena_destroy(TrytrArena *arena)
{
    if (arena == NULL) return;
    TrytrArenaBlock *block = arena->blocks;
    while (block != NULL) {
        TrytrArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena->blocks = NULL;
    arena->committed_bytes = 0;
}
