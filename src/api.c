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
#include <stdio.h>

#include "da.h"
#include "qbe.h"

// Helpers
#define SVFmt    "%.*s"
#define SVArg(s) (int) ((s).count), ((s).data)

static __attribute__((format(printf, 2, 3))) void sb_fmt(Qbe *q, const char *fmt, ...) {
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

static void assert_local(Qbe *q, bool local, const char *label) {
    if (q->local != local) {
        fprintf(stderr, "ERROR: Unexpected %s in %s scope\n", label, q->local ? "local" : "global");
        abort();
    }
}

static void assert_not_i0(QbeType type, const char *action) {
    if (type.kind == QBE_TYPE_I0) {
        fprintf(stderr, "ERROR: Cannot %s type 'i0'\n", action);
        abort();
    }
}

static_assert(QBE_COUNT_TYPES == 7, "");
static void sb_type(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
        sb_fmt(q, "b");
        break;

    case QBE_TYPE_I16:
        sb_fmt(q, "h");
        break;

    case QBE_TYPE_I32:
        sb_fmt(q, "w");
        break;

    case QBE_TYPE_I64:
    case QBE_TYPE_PTR:
        sb_fmt(q, "l");
        break;

    case QBE_TYPE_STRUCT:
        sb_fmt(q, ":.%zu", type.data);
        break;

    default:
        assert(false && "unreachable");
    }
}

static_assert(QBE_COUNT_TYPES == 7, "");
static void sb_type_ssa(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
    case QBE_TYPE_I16:
    case QBE_TYPE_I32:
        sb_fmt(q, "w");
        break;

    case QBE_TYPE_I64:
    case QBE_TYPE_PTR:
        sb_fmt(q, "l");
        break;

    case QBE_TYPE_STRUCT:
        sb_fmt(q, ":.%zu", type.data);
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

static void emit_str(Qbe *q, QbeStr str) {
    sb_fmt(q, "data ");
    sb_value(q, str.value);
    sb_fmt(q, " = align 1 { b \"");

    for (size_t i = 0; i < str.sv.count; i++) {
        const char it = str.sv.data[i];
        if (it == '"') {
            sb_fmt(q, "\\\"");
        } else if (isprint(it)) {
            sb_fmt(q, "%c", it);
        } else {
            sb_fmt(q, "\\x%x", it);
        }
    }

    sb_fmt(q, "\", b 0 }\n");
}

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
    da_free(&q->fields);
    da_free(&q->structs);
    da_free(&q->local_strs);
}

static_assert(QBE_COUNT_TYPES == 7, "");
QbeTypeInfo qbe_type_info(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
        return (QbeTypeInfo) {.size = 1, .align = 1};

    case QBE_TYPE_I16:
        return (QbeTypeInfo) {.size = 2, .align = 2};

    case QBE_TYPE_I32:
        return (QbeTypeInfo) {.size = 4, .align = 4};

    case QBE_TYPE_I64:
    case QBE_TYPE_PTR:
        return (QbeTypeInfo) {.size = 8, .align = 8};

    case QBE_TYPE_STRUCT:
        assert(type.data < q->structs.count);
        return q->structs.data[type.data].info;

    default:
        assert(false && "unreachable");
    }
}

// TODO: Intern the structs
QbeType qbe_type_struct(Qbe *q, QbeType *fields, size_t count) {
    QbeStruct st = {0};
    st.fields = q->fields.count;
    st.count = count;
    da_grow(&q->fields, count);

    st.info.size = 0;
    st.info.align = 1;

    QbeField *fs = &q->fields.data[st.fields];
    for (size_t i = 0; i < count; ++i) {
        QbeField *it = &fs[i];
        it->type = fields[i];
        it->info = qbe_type_info(q, it->type);

        QbeTypeInfo info = it->info;
        if (info.align > st.info.align) {
            st.info.align = info.align;
        }
    }

    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        QbeField   *it = &fs[i];
        QbeTypeInfo info = it->info;

        offset += (info.align - (offset % info.align)) % info.align;
        it->offset = offset;
        offset += info.size;
    }

    offset += (st.info.align - (offset % st.info.align)) % st.info.align;
    st.info.size = offset;

    QbeType type = {.kind = QBE_TYPE_STRUCT, .data = q->structs.count};
    da_push(&q->structs, st);
    return type;
}

QbeValue qbe_emit_str(Qbe *q, QbeSV sv) {
    QbeValue value = {0};
    value.kind = QBE_VALUE_GLOBAL;
    value.type.kind = QBE_TYPE_PTR;
    value.iota = q->global_iota++;

    const QbeStr str = {
        .sv = sv,
        .value = value,
    };

    if (q->local) {
        da_push(&q->local_strs, str);
        return value;
    }

    emit_str(q, str);
    return value;
}

QbeValue qbe_emit_var(Qbe *q, QbeSV name, QbeType type) {
    assert_local(q, false, "variable"); // TODO: local variables
    assert_not_i0(type, "define variable with");

    QbeValue var = {0};
    var.kind = QBE_VALUE_GLOBAL;
    var.type.kind = QBE_TYPE_PTR;
    var.name = name;

    bool export = true;
    if (!name.count) {
        var.iota = q->global_iota++;
        export = false;
    }

    if (export) {
        sb_fmt(q, "export ");
    }
    sb_fmt(q, "data ");
    sb_value(q, var);

    QbeTypeInfo info = qbe_type_info(q, type);
    sb_fmt(q, " = align %zu { z %zu }\n", info.align, info.size);
    return var;
}

QbeValue qbe_emit_func(Qbe *q, QbeSV name, QbeType return_type, QbeType *arg_types, size_t arity) {
    assert_local(q, false, "function");

    QbeValue func = {0};
    func.kind = QBE_VALUE_GLOBAL;
    func.type.kind = QBE_TYPE_PTR;
    func.name = name;

    bool export = true;
    if (!name.count) {
        func.iota = q->global_iota++;
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

    q->local = true;
    return func;
}

QbeValue qbe_emit_load(Qbe *q, QbeValue ptr, QbeType type) {
    assert_local(q, true, "load");
    assert_not_i0(type, "load");

    if (type.kind == QBE_TYPE_STRUCT) {
        ptr.type = type;
        return ptr;
    }

    QbeValue load = {0};
    load.kind = QBE_VALUE_LOCAL;
    load.type = type;
    load.iota = q->local_iota++;

    sb_value(q, load);
    sb_fmt(q, " =");
    sb_type_ssa(q, type);
    sb_fmt(q, " ");

    sb_fmt(q, "load");
    sb_type(q, type);
    sb_fmt(q, " ");
    sb_value(q, ptr);
    sb_fmt(q, "\n");
    return load;
}

void qbe_emit_store(Qbe *q, QbeValue ptr, QbeValue value) {
    assert_local(q, true, "store");
    assert_not_i0(value.type, "store");

    if (value.type.kind == QBE_TYPE_STRUCT) {
        sb_fmt(q, "blit ");
        sb_value(q, value);
        sb_fmt(q, ", ");
        sb_value(q, ptr);

        QbeTypeInfo info = qbe_type_info(q, value.type);
        sb_fmt(q, ", %zu\n", info.size);
        return;
    }

    sb_fmt(q, "store");
    sb_type(q, value.type);
    sb_fmt(q, " ");

    sb_value(q, value);
    sb_fmt(q, ", ");
    sb_value(q, ptr);
    sb_fmt(q, "\n");
}

QbeValue qbe_emit_call(Qbe *q, QbeValue func, QbeType return_type, QbeValue *args, size_t arity) {
    assert_local(q, true, "call");

    QbeValue call = {0};
    call.kind = QBE_VALUE_LOCAL;
    call.type = return_type;

    if (return_type.kind != QBE_TYPE_I0) {
        call.iota = q->local_iota++;

        sb_value(q, call);
        sb_fmt(q, " =");
        sb_type_ssa(q, return_type);
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

static_assert(QBE_COUNT_UNARYS == 3, "");
QbeValue qbe_emit_unary(Qbe *q, QbeUnary op, QbeType type, QbeValue operand) {
    assert_local(q, true, "unary operation");

    QbeValue unary = {0};
    unary.kind = QBE_VALUE_LOCAL;
    unary.type = type;
    unary.iota = q->local_iota++;

    sb_value(q, unary);
    sb_fmt(q, " =");
    sb_type_ssa(q, type);
    sb_fmt(q, " ");

    switch (op) {
    case QBE_UNARY_NEG:
        sb_fmt(q, "neg ");
        sb_value(q, operand);
        sb_fmt(q, "\n");
        break;

    case QBE_UNARY_BNOT:
        // QBE doesn't support binary NOT smh
        sb_fmt(q, "xor ");
        sb_value(q, operand);
        sb_fmt(q, ", 18446744073709551615\n");
        break;

    case QBE_UNARY_LNOT:
        sb_fmt(q, "ceqw ");
        sb_value(q, operand);
        sb_fmt(q, ", 0\n");
        break;

    default:
        assert(false && "unreachable");
    }

    return unary;
}

static_assert(QBE_COUNT_BINARYS == 23, "");
QbeValue qbe_emit_binary(Qbe *q, QbeBinary op, QbeType type, QbeValue lhs, QbeValue rhs) {
    assert_local(q, true, "binary operation");

    QbeValue binary = {0};
    binary.kind = QBE_VALUE_LOCAL;
    binary.type = type;
    binary.iota = q->local_iota++;

    sb_value(q, binary);
    sb_fmt(q, " =");
    sb_type_ssa(q, type);
    sb_fmt(q, " ");

    switch (op) {
    case QBE_BINARY_ADD:
        sb_fmt(q, "add");
        break;

    case QBE_BINARY_SUB:
        sb_fmt(q, "sub");
        break;

    case QBE_BINARY_MUL:
        sb_fmt(q, "mul");
        break;

    case QBE_BINARY_SDIV:
        sb_fmt(q, "div");
        break;

    case QBE_BINARY_UDIV:
        sb_fmt(q, "udiv");
        break;

    case QBE_BINARY_SMOD:
        sb_fmt(q, "rem");
        break;

    case QBE_BINARY_UMOD:
        sb_fmt(q, "urem");
        break;

    case QBE_BINARY_OR:
        sb_fmt(q, "or");
        break;

    case QBE_BINARY_AND:
        sb_fmt(q, "and");
        break;

    case QBE_BINARY_XOR:
        sb_fmt(q, "xor");
        break;

    case QBE_BINARY_SHL:
        sb_fmt(q, "shl");
        break;

    case QBE_BINARY_SSHR:
        sb_fmt(q, "sar");
        break;

    case QBE_BINARY_USHR:
        sb_fmt(q, "shr");
        break;

    case QBE_BINARY_SGT:
        sb_fmt(q, "csgt");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_UGT:
        sb_fmt(q, "cugt");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_SGE:
        sb_fmt(q, "csge");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_UGE:
        sb_fmt(q, "cuge");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_SLT:
        sb_fmt(q, "cslt");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_ULT:
        sb_fmt(q, "cult");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_SLE:
        sb_fmt(q, "csle");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_ULE:
        sb_fmt(q, "cule");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_EQ:
        sb_fmt(q, "ceq");
        sb_type_ssa(q, lhs.type);
        break;

    case QBE_BINARY_NE:
        sb_fmt(q, "cne");
        sb_type_ssa(q, lhs.type);
        break;

    default:
        assert(false && "unreachable");
    }

    sb_fmt(q, " ");
    sb_value(q, lhs);
    sb_fmt(q, ", ");
    sb_value(q, rhs);
    sb_fmt(q, "\n");
    return binary;
}

QbeBlock qbe_new_block(Qbe *q) {
    assert_local(q, true, "block");
    return (QbeBlock) {.iota = q->block_iota++};
}

void qbe_emit_block(Qbe *q, QbeBlock block) {
    assert_local(q, true, "block");
    sb_fmt(q, "@.%zu\n", block.iota);
}

void qbe_emit_jump(Qbe *q, QbeBlock block) {
    assert_local(q, true, "jump");
    sb_fmt(q, "jmp @.%zu\n", block.iota);
}

void qbe_emit_branch(Qbe *q, QbeValue cond, QbeBlock then_block, QbeBlock else_block) {
    assert_local(q, true, "branch");
    sb_fmt(q, "jnz ");
    sb_value(q, cond);
    sb_fmt(q, ", @.%zu, @.%zu\n", then_block.iota, else_block.iota);
}

void qbe_emit_return(Qbe *q, QbeValue *value) {
    assert_local(q, true, "return");

    sb_fmt(q, "ret");
    if (value) {
        sb_fmt(q, " ");
        sb_value(q, *value);
    }

    sb_fmt(q, "\n");
}

void qbe_emit_structs(Qbe *q) {
    for (size_t i = 0; i < q->structs.count; i++) {
        QbeStruct it = q->structs.data[i];
        QbeField *fs = &q->fields.data[it.fields];

        sb_fmt(q, "type :.%zu = { ", i);
        for (size_t j = 0; j < it.count; j++) {
            if (j) {
                sb_fmt(q, ", ");
            }
            sb_type(q, fs[j].type);
        }
        sb_fmt(q, " }\n");
    }
}

void qbe_emit_func_end(Qbe *q) {
    assert_local(q, true, "end of function");

    sb_fmt(q, "}\n");
    q->local = false;

    for (size_t i = 0; i < q->local_strs.count; i++) {
        emit_str(q, q->local_strs.data[i]);
    }
    q->local_strs.count = 0;
}
