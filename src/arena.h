// Copyright (C) 2025 Shoumodip Kar <shoumodipkar@gmail.com>

// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#ifndef ARENA_API
#    define ARENA_API
#endif // ARENA_API

#ifndef ARENA_MINIMUM_CAPACITY
#    define ARENA_MINIMUM_CAPACITY 16000
#endif // ARENA_MINIMUM_CAPACITY

typedef struct ArenaRegion ArenaRegion;

typedef struct {
    ArenaRegion *head;
} Arena;

ARENA_API void  arena_free(Arena *a);
ARENA_API void *arena_alloc(Arena *a, size_t size);

#endif // ARENA_H

#ifdef ARENA_IMPLEMENTATION

#include <stdlib.h>

struct ArenaRegion {
    ArenaRegion *next;
    size_t       count;
    size_t       capacity;
    char         data[];
};

ARENA_API void arena_free(Arena *a) {
    ArenaRegion *it = a->head;
    while (it) {
        ArenaRegion *next = it->next;
        free(it);
        it = next;
    }
}

ARENA_API void *arena_alloc(Arena *a, size_t size) {
    ArenaRegion *region = NULL;
    for (ArenaRegion *it = a->head; it; it = it->next) {
        if (it->count + size <= it->capacity) {
            region = it;
            break;
        }
    }

    size = (size + 7) & -8; // Alignment
    if (!region) {
        size_t capacity = size;
        if (capacity < ARENA_MINIMUM_CAPACITY) {
            capacity = ARENA_MINIMUM_CAPACITY;
        }

        region = malloc(sizeof(ArenaRegion) + capacity);
        region->next = a->head;
        region->count = 0;
        region->capacity = capacity;
        a->head = region;
    }

    void *ptr = &region->data[region->count];
    region->count += size;
    return ptr;
}

#endif // ARENA_IMPLEMENTATION
