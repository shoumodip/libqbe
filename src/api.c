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

#include <ctype.h>
#include <stdarg.h>

#include "da.h"
#include "qbe.h"

// Helpers
#define SVFmt    "%.*s"
#define SVArg(s) (int) ((s).count), ((s).data)

__attribute__((format(printf, 2, 3))) static void sb_fmt(Qbe *q, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    assert(n >= 0);
    da_grow(&q->sb, n + 1);

    va_start(args, fmt);
    vsnprintf(q->sb.data + q->sb.count, n + 1, fmt, args);
    q->sb.count += n;
    va_end(args);
}

static_assert(QBE_COUNT_TYPES == 4, "");
static void sb_type(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I32:
        sb_fmt(q, "w");
        break;

    case QBE_TYPE_I64:
    case QBE_TYPE_PTR:
        sb_fmt(q, "l");
        break;

    default:
        assert(false && "unreachable");
    }
}

static_assert(QBE_COUNT_VALUES == 3, "");
static void sb_value(Qbe *q, QbeValue value) {
    char prefix = 0;
    switch (value.kind) {
    case QBE_VALUE_INT:
        sb_fmt(q, "%zu", value.iota);
        return;

    case QBE_VALUE_LOCAL:
        prefix = '%';
        break;

    case QBE_VALUE_GLOBAL:
        prefix = '$';
        break;

    default:
        assert(false && "unreachable");
    }

    if (value.name.count) {
        sb_fmt(q, "%c" SVFmt, prefix, SVArg(value.name));
    } else {
        sb_fmt(q, "%c.%zu", prefix, value.iota);
    }
}

static void sb_type_value(Qbe *q, QbeValue value) {
    sb_type(q, value.type);
    sb_fmt(q, " ");
    sb_value(q, value);
}

static size_t local_iota;
static size_t global_iota;

// API
QbeSV qbe_sv_from_cstr(const char *cstr) {
    return (QbeSV) {.data = (cstr), .count = strlen(cstr)};
}

QbeType qbe_type_basic(QbeTypeKind kind) {
    return (QbeType) {.kind = kind};
}

QbeValue qbe_value_int(QbeTypeKind kind, size_t n) {
    QbeValue value = {0};
    value.kind = QBE_VALUE_INT;
    value.type.kind = kind;
    value.iota = n;
    return value;
}

QbeValue qbe_value_import(QbeSV name, QbeType type) {
    QbeValue call = {0};
    call.kind = QBE_VALUE_GLOBAL;
    call.type = type;
    call.name = name;
    return call;
}

void qbe_free(Qbe *q) {
    da_free(&q->sb);
    da_free(&q->types);
}

QbeValue qbe_emit_str(Qbe *q, QbeSV sv) {
    QbeValue str = {0};
    str.kind = QBE_VALUE_GLOBAL;
    str.type.kind = QBE_TYPE_PTR;
    str.iota = global_iota++;

    sb_fmt(q, "data ");
    sb_value(q, str);
    sb_fmt(q, " = align 1 { b \"");

    for (size_t i = 0; i < sv.count; i++) {
        const char it = sv.data[i];
        if (it == '"') {
            sb_fmt(q, "\\\"");
        } else if (isprint(it)) {
            sb_fmt(q, "%c", it);
        } else {
            sb_fmt(q, "\\x%x", it);
        }
    }

    sb_fmt(q, "\", b 0 }\n");
    return str;
}

QbeValue qbe_emit_func(Qbe *q, QbeSV name, QbeType return_type, QbeType *arg_types, size_t arity) {
    QbeValue func = {0};
    func.kind = QBE_VALUE_GLOBAL;
    func.type.kind = QBE_TYPE_PTR;
    func.name = name;

    bool export = true;
    if (!name.count) {
        func.iota = global_iota++;
        export = false;
    }

    if (export) {
        sb_fmt(q, "export ");
    }
    sb_fmt(q, "function");

    if (return_type.kind != QBE_TYPE_I0) {
        sb_fmt(q, " ");
        sb_type(q, return_type);
    }

    sb_fmt(q, " ");
    sb_value(q, func);
    sb_fmt(q, "(");
    for (size_t i = 0; i < arity; i++) {
        if (i) {
            sb_fmt(q, ", ");
        }

        sb_type(q, arg_types[i]);
        sb_fmt(q, " %%a%zu", i);
    }

    sb_fmt(q, ") {\n@start\n");

    return func;
}

QbeValue qbe_emit_call(Qbe *q, QbeValue func, QbeType return_type, QbeValue *args, size_t arity) {
    QbeValue call = {0};
    call.kind = QBE_VALUE_LOCAL;
    call.type = return_type;

    if (return_type.kind != QBE_TYPE_I0) {
        call.iota = local_iota++;

        sb_value(q, call);
        sb_fmt(q, " =");
        sb_type(q, return_type);
        sb_fmt(q, " ");
    }

    sb_fmt(q, "call ");
    sb_value(q, func);

    sb_fmt(q, "(");
    for (size_t i = 0; i < arity; i++) {
        if (i) {
            sb_fmt(q, ", ");
        }

        sb_type_value(q, args[i]);
    }
    sb_fmt(q, ")\n");

    return call;
}

void qbe_emit_return(Qbe *q, QbeValue *value) {
    sb_fmt(q, "ret");
    if (value) {
        sb_fmt(q, " ");
        sb_value(q, *value);
    }

    sb_fmt(q, "\n");
}

void qbe_emit_func_end(Qbe *q) {
    sb_fmt(q, "}\n");
}
