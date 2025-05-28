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

#ifndef DA_H
#define DA_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define DA_INIT_CAP 128

#define da_free(l)                                                                                 \
    do {                                                                                           \
        free((l)->data);                                                                           \
        memset((l), 0, sizeof(*(l)));                                                              \
    } while (0)

#define da_push(l, v)                                                                              \
    do {                                                                                           \
        if ((l)->count >= (l)->capacity) {                                                         \
            (l)->capacity = (l)->capacity == 0 ? DA_INIT_CAP : (l)->capacity * 2;                  \
            (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                    \
            assert((l)->data);                                                                     \
        }                                                                                          \
                                                                                                   \
        (l)->data[(l)->count++] = (v);                                                             \
    } while (0)

#define da_grow(l, c)                                                                              \
    do {                                                                                           \
        if ((l)->count + (c) > (l)->capacity) {                                                    \
            if ((l)->capacity == 0) {                                                              \
                (l)->capacity = DA_INIT_CAP;                                                       \
            }                                                                                      \
                                                                                                   \
            while ((l)->count + (c) > (l)->capacity) {                                             \
                (l)->capacity *= 2;                                                                \
            }                                                                                      \
                                                                                                   \
            (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                    \
            assert((l)->data);                                                                     \
        }                                                                                          \
    } while (0)

#define da_push_many(l, v, c)                                                                      \
    do {                                                                                           \
        da_grow((l), (c));                                                                         \
        memcpy((l)->data + (l)->count, (v), (c) * sizeof(*(l)->data));                             \
        (l)->count += (c);                                                                         \
    } while (0)

#endif // DA_H
