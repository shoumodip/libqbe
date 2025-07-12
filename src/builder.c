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

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARENA_API static
#define ARENA_IMPLEMENTATION
#include "arena.h"
#include "qbe.h"

typedef enum {
    QBE_SSA_NIL,
    QBE_SSA_INT,
    QBE_SSA_FLOAT,
    QBE_SSA_LOCAL,
    QBE_SSA_GLOBAL
} QbeSSA;

typedef enum {
    QBE_NODE_ATOM,
    QBE_NODE_UNARY,
    QBE_NODE_BINARY,

    QBE_NODE_ARG,
    QBE_NODE_PHI,
    QBE_NODE_CALL,
    QBE_NODE_CAST,
    QBE_NODE_LOAD,
    QBE_NODE_STORE,

    QBE_NODE_JUMP,
    QBE_NODE_BRANCH,
    QBE_NODE_RETURN,

    QBE_NODE_FN,
    QBE_NODE_VAR,
    QBE_NODE_BLOCK,
    QBE_NODE_FIELD,
    QBE_NODE_STRUCT,

    QBE_NODE_DEBUG,
    QBE_COUNT_NODES,
} QbeNodeKind;

struct QbeNode {
    QbeNodeKind kind;
    QbeType     type;

    QbeSSA ssa;
    QbeSV  sv;
    union {
        size_t iota;
        double real;
    };

    QbeNode *next;
};

typedef struct {
    QbeNode *head;
    QbeNode *tail;
} QbeNodes;

typedef struct {
    QbeNode node;

    QbeUnaryOp op;
    QbeNode   *operand;
} QbeUnary;

typedef struct {
    QbeNode node;

    QbeBinaryOp op;
    QbeNode    *lhs;
    QbeNode    *rhs;
} QbeBinary;

typedef struct {
    QbeNode  node;
    QbeNode *value;
    bool     start_variadic;
} QbeArg;

typedef struct {
    QbeNode      node;
    QbePhiBranch a;
    QbePhiBranch b;
} QbePhi;

struct QbeCall {
    QbeNode  node;
    QbeNode *fn;
    QbeNodes args;

    bool started_variadic;
};

typedef struct {
    QbeNode  node;
    QbeNode *value;
    bool     is_signed;
} QbeCast;

typedef struct {
    QbeNode  node;
    QbeNode *src;
    bool     is_signed;
} QbeLoad;

typedef struct {
    QbeNode     node;
    QbeNode    *dst;
    QbeNode    *src;
    const void *data;
} QbeStore;

typedef struct {
    QbeNode   node;
    QbeBlock *block;
} QbeJump;

typedef struct {
    QbeNode   node;
    QbeNode  *cond;
    QbeBlock *then_block;
    QbeBlock *else_block;
} QbeBranch;

typedef struct {
    QbeNode  node;
    QbeNode *value;
} QbeReturn;

struct QbeFn {
    QbeNode node;

    QbeNodes args;
    QbeNodes vars;
    QbeNodes body;

    QbeType return_type;
    QbeSV   debug_file;
    size_t  debug_line;

    QbeBlock *current_block;
};

typedef struct {
    QbeNode node;

    bool    local;
    QbeSV   str;
    QbeType type;

    const void *data;
} QbeVar;

struct QbeBlock {
    QbeNode node;
};

typedef struct {
    size_t align;
    size_t size;
} QbeTypeInfo;

struct QbeField {
    QbeNode node;

    size_t repeat;
    size_t offset;

    QbeTypeInfo info;
    QbeStruct  *parent;
};

struct QbeStruct {
    QbeNode node;

    QbeNodes fields;
    size_t   fields_count;

    QbeTypeInfo info;
    bool        info_ready;

    bool packed;
    bool compiled;
};

typedef struct {
    QbeNode node;
    size_t  line;
} QbeDebug;

typedef struct {
    char  *data;
    size_t count;
    size_t capacity;
} QbeSB;

typedef struct {
    uint64_t   hash;
    QbeStruct *st;
} QbeStructHashed;

typedef struct {
    QbeStructHashed *data;
    size_t           count;
    size_t           capacity;
    size_t           iota;
} QbeStructCache;

typedef struct {
    QbeType type;
    size_t  count;
} QbeArrayKey;

typedef struct {
    QbeArrayKey key;
    QbeType     type;
    bool        used;
} QbeArrayEntry;

typedef struct {
    QbeArrayEntry *data;
    size_t         count;
    size_t         capacity;
} QbeArrayCache;

struct Qbe {
    Arena arena;

    QbeNodes fns;
    QbeNodes vars;
    QbeNodes structs;

    size_t blocks;
    size_t locals;

    QbeArrayCache  array_cache;
    QbeStructCache struct_cache;

    bool  compiled;
    QbeSB sb;
};

static bool qbe_type_kind_is_float(QbeTypeKind k) {
    return k == QBE_TYPE_F32 || k == QBE_TYPE_F64;
}

static_assert(QBE_COUNT_TYPES == 8, "");
static QbeTypeInfo qbe_type_info(QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I0:
        return (QbeTypeInfo) {.size = 0, .align = 0};

    case QBE_TYPE_I8:
        return (QbeTypeInfo) {.size = 1, .align = 1};

    case QBE_TYPE_I16:
        return (QbeTypeInfo) {.size = 2, .align = 2};

    case QBE_TYPE_I32:
    case QBE_TYPE_F32:
        return (QbeTypeInfo) {.size = 4, .align = 4};

    case QBE_TYPE_I64:
    case QBE_TYPE_F64:
        return (QbeTypeInfo) {.size = 8, .align = 8};

    case QBE_TYPE_STRUCT:
        assert(type.spec->info_ready);
        return type.spec->info;

    default:
        assert(false && "unreachable");
    }
}

static void qbe_nodes_push(QbeNodes *ns, QbeNode *node) {
    if (ns->tail) {
        ns->tail->next = node;
        ns->tail = node;
    } else {
        ns->head = node;
        ns->tail = node;
    }
}

static QbeNode *qbe_node_alloc(Qbe *q, QbeNodeKind kind, QbeType type) {
    assert(!q->compiled && "This QBE context is already compiled");

    static_assert(QBE_COUNT_NODES == 18, "");
    static const size_t sizes[QBE_COUNT_NODES] = {
        [QBE_NODE_ATOM] = sizeof(QbeNode),
        [QBE_NODE_UNARY] = sizeof(QbeUnary),
        [QBE_NODE_BINARY] = sizeof(QbeBinary),

        [QBE_NODE_ARG] = sizeof(QbeArg),
        [QBE_NODE_PHI] = sizeof(QbePhi),
        [QBE_NODE_CALL] = sizeof(QbeCall),
        [QBE_NODE_CAST] = sizeof(QbeCast),
        [QBE_NODE_LOAD] = sizeof(QbeLoad),
        [QBE_NODE_STORE] = sizeof(QbeStore),

        [QBE_NODE_JUMP] = sizeof(QbeJump),
        [QBE_NODE_BRANCH] = sizeof(QbeBranch),
        [QBE_NODE_RETURN] = sizeof(QbeReturn),

        [QBE_NODE_FN] = sizeof(QbeFn),
        [QBE_NODE_VAR] = sizeof(QbeVar),
        [QBE_NODE_BLOCK] = sizeof(QbeBlock),
        [QBE_NODE_FIELD] = sizeof(QbeField),
        [QBE_NODE_STRUCT] = sizeof(QbeStruct),

        [QBE_NODE_DEBUG] = sizeof(QbeDebug),
    };

    assert(kind >= QBE_NODE_ATOM && kind < QBE_COUNT_NODES);
    size_t size = sizes[kind];

    QbeNode *node = arena_alloc(&q->arena, size);
    memset(node, 0, size);
    node->kind = kind;
    node->type = type;
    return node;
}

static QbeNode *qbe_node_build(Qbe *q, QbeFn *fn, QbeNodeKind kind, QbeType type) {
    QbeNode *node = qbe_node_alloc(q, kind, type);
    qbe_nodes_push(&fn->body, node);
    return node;
}

__attribute__((format(printf, 2, 3))) static void qbe_sb_fmt(Qbe *q, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    assert(n >= 0);

    if (q->sb.count + n + 1 > q->sb.capacity) {
        if (q->sb.capacity == 0) {
            q->sb.capacity = 128;
        }

        while (q->sb.count + n + 1 > q->sb.capacity) {
            q->sb.capacity *= 2;
        }

        q->sb.data = realloc(q->sb.data, q->sb.capacity * sizeof(*q->sb.data));
        assert(q->sb.data);
    }

    va_start(args, fmt);
    vsnprintf(q->sb.data + q->sb.count, n + 1, fmt, args);
    q->sb.count += n;
    va_end(args);
}

static_assert(QBE_COUNT_TYPES == 8, "");
static void qbe_sb_type(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
        qbe_sb_fmt(q, "b");
        break;

    case QBE_TYPE_I16:
        qbe_sb_fmt(q, "h");
        break;

    case QBE_TYPE_I32:
        qbe_sb_fmt(q, "w");
        break;

    case QBE_TYPE_I64:
        qbe_sb_fmt(q, "l");
        break;

    case QBE_TYPE_F32:
        qbe_sb_fmt(q, "s");
        break;

    case QBE_TYPE_F64:
        qbe_sb_fmt(q, "d");
        break;

    case QBE_TYPE_STRUCT:
        qbe_sb_fmt(q, ":.%zu", type.spec->node.iota);
        break;

    default:
        assert(false && "unreachable");
    }
}

static_assert(QBE_COUNT_TYPES == 8, "");
static void qbe_sb_type_ssa(Qbe *q, QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
    case QBE_TYPE_I16:
    case QBE_TYPE_I32:
        qbe_sb_fmt(q, "w");
        break;

    case QBE_TYPE_I64:
        qbe_sb_fmt(q, "l");
        break;

    case QBE_TYPE_F32:
        qbe_sb_fmt(q, "s");
        break;

    case QBE_TYPE_F64:
        qbe_sb_fmt(q, "d");
        break;

    case QBE_TYPE_STRUCT:
        qbe_sb_fmt(q, ":.%zu", type.spec->node.iota);
        break;

    default:
        assert(false && "unreachable");
    }
}

static void qbe_sb_node_ssa(Qbe *q, QbeNode *node) {
    char prefix = 0;
    switch (node->ssa) {
    case QBE_SSA_INT:
        qbe_sb_fmt(q, "%zu", node->iota);
        return;

    case QBE_SSA_FLOAT:
        if (node->type.kind == QBE_TYPE_F32) {
            qbe_sb_fmt(q, "s_%lf", node->real);
        } else {
            qbe_sb_fmt(q, "d_%lf", node->real);
        }
        return;

    case QBE_SSA_LOCAL:
        prefix = '%';
        break;

    case QBE_SSA_GLOBAL:
        prefix = '$';
        break;

    default:
        assert(false && "unreachable");
    }

    if (node->sv.data) {
        qbe_sb_fmt(q, "%c" QbeSVFmt, prefix, QbeSVArg(node->sv));
    } else {
        qbe_sb_fmt(q, "%c.%zu", prefix, node->iota);
    }
}

static inline void qbe_sb_indent(Qbe *q) {
    qbe_sb_fmt(q, "\t");
}

static inline size_t qbe_block_iota(Qbe *q, QbeBlock *block) {
    if (!block->node.iota) {
        block->node.iota = q->blocks++;
    }

    return block->node.iota;
}

static inline void qbe_sb_quote_sv(Qbe *q, QbeSV sv) {
    qbe_sb_fmt(q, "\"");
    for (size_t i = 0; i < sv.count; i++) {
        const char it = sv.data[i];
        if (it == '"') {
            qbe_sb_fmt(q, "\\\"");
        } else if (isprint(it)) {
            qbe_sb_fmt(q, "%c", it);
        } else {
            qbe_sb_fmt(q, "\\x%x", it);
        }
    }
    qbe_sb_fmt(q, "\"");
}

static_assert(QBE_COUNT_NODES == 18, "");
static void qbe_compile_node(Qbe *q, QbeNode *n) {
    if (!n || n->ssa) {
        return;
    }

    switch (n->kind) {
    case QBE_NODE_ATOM:
        assert(false && "unreachable");
        break;

    case QBE_NODE_UNARY: {
        QbeUnary *unary = (QbeUnary *) n;
        qbe_compile_node(q, unary->operand);

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

        qbe_sb_indent(q);
        qbe_sb_node_ssa(q, n);
        qbe_sb_fmt(q, " =");
        qbe_sb_type_ssa(q, n->type);
        qbe_sb_fmt(q, " ");

        static_assert(QBE_COUNT_UNARYS == 4, "");
        switch (unary->op) {
        case QBE_UNARY_NOP:
            assert(false && "NOP");
            break;

        case QBE_UNARY_NEG:
            qbe_sb_fmt(q, "neg ");
            qbe_sb_node_ssa(q, unary->operand);
            qbe_sb_fmt(q, "\n");
            break;

        case QBE_UNARY_BNOT:
            // QBE doesn't support binary NOT smh
            qbe_sb_fmt(q, "xor ");
            qbe_sb_node_ssa(q, unary->operand);
            qbe_sb_fmt(q, ", 18446744073709551615\n");
            break;

        case QBE_UNARY_LNOT:
            qbe_sb_fmt(q, "ceqw ");
            qbe_sb_node_ssa(q, unary->operand);
            qbe_sb_fmt(q, ", 0\n");
            break;

        default:
            assert(false && "unreachable");
        }
    } break;

    case QBE_NODE_BINARY: {
        QbeBinary *binary = (QbeBinary *) n;
        qbe_compile_node(q, binary->lhs);
        qbe_compile_node(q, binary->rhs);

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

        qbe_sb_indent(q);
        qbe_sb_node_ssa(q, n);
        qbe_sb_fmt(q, " =");
        qbe_sb_type_ssa(q, n->type);
        qbe_sb_fmt(q, " ");

        static_assert(QBE_COUNT_BINARYS == 24, "");
        switch (binary->op) {
        case QBE_BINARY_NOP:
            assert(false && "NOP");
            break;

        case QBE_BINARY_ADD:
            qbe_sb_fmt(q, "add");
            break;

        case QBE_BINARY_SUB:
            qbe_sb_fmt(q, "sub");
            break;

        case QBE_BINARY_MUL:
            qbe_sb_fmt(q, "mul");
            break;

        case QBE_BINARY_SDIV:
            qbe_sb_fmt(q, "div");
            break;

        case QBE_BINARY_UDIV:
            qbe_sb_fmt(q, "udiv");
            break;

        case QBE_BINARY_SMOD:
            qbe_sb_fmt(q, "rem");
            break;

        case QBE_BINARY_UMOD:
            qbe_sb_fmt(q, "urem");
            break;

        case QBE_BINARY_OR:
            qbe_sb_fmt(q, "or");
            break;

        case QBE_BINARY_AND:
            qbe_sb_fmt(q, "and");
            break;

        case QBE_BINARY_XOR:
            qbe_sb_fmt(q, "xor");
            break;

        case QBE_BINARY_SHL:
            qbe_sb_fmt(q, "shl");
            break;

        case QBE_BINARY_SSHR:
            qbe_sb_fmt(q, "sar");
            break;

        case QBE_BINARY_USHR:
            qbe_sb_fmt(q, "shr");
            break;

        case QBE_BINARY_SGT:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cgt");
            } else {
                qbe_sb_fmt(q, "csgt");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_UGT:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cgt");
            } else {
                qbe_sb_fmt(q, "cugt");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_SGE:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cge");
            } else {
                qbe_sb_fmt(q, "csge");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_UGE:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cge");
            } else {
                qbe_sb_fmt(q, "cuge");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_SLT:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "clt");
            } else {
                qbe_sb_fmt(q, "cslt");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_ULT:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "clt");
            } else {
                qbe_sb_fmt(q, "cult");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_SLE:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cle");
            } else {
                qbe_sb_fmt(q, "csle");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_ULE:
            if (qbe_type_kind_is_float(binary->lhs->type.kind)) {
                qbe_sb_fmt(q, "cle");
            } else {
                qbe_sb_fmt(q, "cule");
                qbe_sb_type_ssa(q, binary->lhs->type);
            }
            break;

        case QBE_BINARY_EQ:
            qbe_sb_fmt(q, "ceq");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_NE:
            qbe_sb_fmt(q, "cne");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        default:
            assert(false && "unreachable");
        }

        qbe_sb_fmt(q, " ");
        qbe_sb_node_ssa(q, binary->lhs);
        qbe_sb_fmt(q, ", ");
        qbe_sb_node_ssa(q, binary->rhs);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_ARG:
        assert(false && "unreachable");
        break;

    case QBE_NODE_PHI: {
        QbePhi *phi = (QbePhi *) n;
        qbe_compile_node(q, phi->a.value);
        qbe_compile_node(q, phi->b.value);

        size_t a_block = qbe_block_iota(q, phi->a.block);
        size_t b_block = qbe_block_iota(q, phi->b.block);

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

        qbe_sb_indent(q);
        qbe_sb_node_ssa(q, n);
        qbe_sb_fmt(q, " =");
        qbe_sb_type_ssa(q, n->type);
        qbe_sb_fmt(q, " phi @.%zu ", a_block);
        qbe_sb_node_ssa(q, phi->a.value);
        qbe_sb_fmt(q, ", @.%zu ", b_block);
        qbe_sb_node_ssa(q, phi->b.value);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_CALL: {
        QbeCall *call = (QbeCall *) n;
        qbe_compile_node(q, call->fn);
        for (QbeArg *it = (QbeArg *) call->args.head; it; it = (QbeArg *) it->node.next) {
            if (!it->start_variadic) {
                qbe_compile_node(q, it->value);
            }
        }
        qbe_sb_indent(q);

        n->ssa = QBE_SSA_LOCAL;
        if (n->type.kind != QBE_TYPE_I0) {
            n->iota = q->locals++;

            qbe_sb_node_ssa(q, n);
            qbe_sb_fmt(q, " =");
            qbe_sb_type_ssa(q, n->type);
            qbe_sb_fmt(q, " ");
        }

        qbe_sb_fmt(q, "call ");
        qbe_sb_node_ssa(q, call->fn);

        qbe_sb_fmt(q, "(");
        for (QbeArg *it = (QbeArg *) call->args.head; it; it = (QbeArg *) it->node.next) {
            if (it->start_variadic) {
                if (it->node.next) {
                    qbe_sb_fmt(q, "...");
                }
            } else {
                qbe_sb_type_ssa(q, it->value->type);
                qbe_sb_fmt(q, " ");
                qbe_sb_node_ssa(q, it->value);
            }

            if (it->node.next) {
                qbe_sb_fmt(q, ", ");
            }
        }
        qbe_sb_fmt(q, ")\n");
    } break;

    case QBE_NODE_CAST: {
        QbeCast *cast = (QbeCast *) n;
        qbe_compile_node(q, cast->value);

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

        qbe_sb_indent(q);
        qbe_sb_node_ssa(q, n);
        qbe_sb_fmt(q, " =");
        qbe_sb_type_ssa(q, n->type);
        qbe_sb_fmt(q, " ");

        if (qbe_type_kind_is_float(cast->value->type.kind)) {
            static_assert(QBE_TYPE_F32 < QBE_TYPE_F64, "");

            if (n->type.kind == QBE_TYPE_F32) {
                // Float -> Lower Float
                qbe_sb_fmt(q, "truncd");
            } else if (n->type.kind == QBE_TYPE_F64) {
                // Float -> Higher Float
                qbe_sb_fmt(q, "exts");
            } else {
                // Float -> Int
                qbe_sb_type_ssa(q, cast->value->type);
                qbe_sb_fmt(q, "to%ci", cast->is_signed ? 's' : 'u');
            }
        } else if (qbe_type_kind_is_float(n->type.kind)) {
            // Int -> Float
            qbe_sb_fmt(q, "%c", cast->is_signed ? 's' : 'u');
            qbe_sb_type_ssa(q, cast->value->type);
            qbe_sb_fmt(q, "tof");
        } else {
            // Int -> Higher Int
            qbe_sb_fmt(q, "ext%c", cast->is_signed ? 's' : 'u');
            qbe_sb_type(q, cast->value->type);
        }

        qbe_sb_fmt(q, " ");
        qbe_sb_node_ssa(q, cast->value);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_LOAD: {
        QbeLoad *load = (QbeLoad *) n;
        qbe_compile_node(q, load->src);

        if (n->type.kind == QBE_TYPE_STRUCT) {
            n->ssa = load->src->ssa;
            n->iota = load->src->iota;
            n->sv = load->src->sv;
            return;
        }

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

        qbe_sb_indent(q);
        qbe_sb_node_ssa(q, n);
        qbe_sb_fmt(q, " =");
        qbe_sb_type_ssa(q, n->type);
        qbe_sb_fmt(q, " ");

        qbe_sb_fmt(q, "load");
        if (n->type.kind == QBE_TYPE_I8 || n->type.kind == QBE_TYPE_I16) {
            qbe_sb_fmt(q, "%c", load->is_signed ? 's' : 'u');
        }

        qbe_sb_type(q, n->type);
        qbe_sb_fmt(q, " ");
        qbe_sb_node_ssa(q, load->src);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_STORE: {
        QbeStore *store = (QbeStore *) n;
        qbe_compile_node(q, store->dst);
        n->ssa = QBE_SSA_LOCAL;

        if (store->data) {
            assert(!store->src);

            const size_t size = qbe_sizeof(n->type);

            size_t stored = 0;
            size_t remaining = size;

            const int8_t *data = store->data;
            for (size_t i = 0; remaining; i++) {
                if (i) {
                    const size_t prev = n->iota;
                    n->iota = q->locals++;

                    qbe_sb_indent(q);
                    qbe_sb_node_ssa(q, n);
                    qbe_sb_fmt(q, " =");
                    qbe_sb_type_ssa(q, store->dst->type);
                    qbe_sb_fmt(q, " add ");

                    if (i == 1) {
                        qbe_sb_node_ssa(q, store->dst);
                    } else {
                        qbe_sb_fmt(q, "%%.%zu", prev);
                    }

                    qbe_sb_fmt(q, ", %zu\n", stored);
                }

                qbe_sb_indent(q);
                if (remaining >= 8) {
                    qbe_sb_fmt(q, "storel %ld, ", *(int64_t *) data);
                    stored = 8;
                } else if (remaining >= 4) {
                    qbe_sb_fmt(q, "storew %d, ", *(int32_t *) data);
                    stored = 4;
                } else if (remaining >= 2) {
                    qbe_sb_fmt(q, "storeh %d, ", *(int16_t *) data);
                    stored = 2;
                } else {
                    qbe_sb_fmt(q, "storeb %d, ", *(int8_t *) data);
                    stored = 1;
                }

                data += stored;
                remaining -= stored;

                qbe_sb_node_ssa(q, i ? n : store->dst);
                qbe_sb_fmt(q, "\n");
            }

            return;
        }

        if (!store->src) {
            const size_t size = qbe_sizeof(n->type);
            if (size > 128) {
                qbe_sb_indent(q);
                qbe_sb_fmt(q, "call $memset(");
                qbe_sb_type_ssa(q, store->dst->type);
                qbe_sb_fmt(q, " ");
                qbe_sb_node_ssa(q, store->dst);
                qbe_sb_fmt(q, ", w 0, l %zu)\n", size);
                return;
            }

            size_t stored = 0;
            size_t remaining = size;
            for (size_t i = 0; remaining; i++) {
                if (i) {
                    const size_t prev = n->iota;
                    n->iota = q->locals++;

                    qbe_sb_indent(q);
                    qbe_sb_node_ssa(q, n);
                    qbe_sb_fmt(q, " =");
                    qbe_sb_type_ssa(q, store->dst->type);
                    qbe_sb_fmt(q, " add ");

                    if (i == 1) {
                        qbe_sb_node_ssa(q, store->dst);
                    } else {
                        qbe_sb_fmt(q, "%%.%zu", prev);
                    }

                    qbe_sb_fmt(q, ", %zu\n", stored);
                }

                qbe_sb_indent(q);
                if (remaining >= 8) {
                    qbe_sb_fmt(q, "storel 0, ");
                    stored = 8;
                } else if (remaining >= 4) {
                    qbe_sb_fmt(q, "storew 0, ");
                    stored = 4;
                } else if (remaining >= 2) {
                    qbe_sb_fmt(q, "storeh 0, ");
                    stored = 2;
                } else {
                    qbe_sb_fmt(q, "storeb 0, ");
                    stored = 1;
                }
                remaining -= stored;

                qbe_sb_node_ssa(q, i ? n : store->dst);
                qbe_sb_fmt(q, "\n");
            }

            return;
        }

        qbe_compile_node(q, store->src);
        if (store->src->type.kind == QBE_TYPE_STRUCT) {
            qbe_sb_indent(q);
            qbe_sb_fmt(q, "blit ");
            qbe_sb_node_ssa(q, store->src);
            qbe_sb_fmt(q, ", ");
            qbe_sb_node_ssa(q, store->dst);

            QbeTypeInfo info = qbe_type_info(store->src->type);
            qbe_sb_fmt(q, ", %zu\n", info.size);
            return;
        }

        qbe_sb_indent(q);
        qbe_sb_fmt(q, "store");
        qbe_sb_type(q, store->src->type);
        qbe_sb_fmt(q, " ");

        qbe_sb_node_ssa(q, store->src);
        qbe_sb_fmt(q, ", ");
        qbe_sb_node_ssa(q, store->dst);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_JUMP: {
        QbeJump *jump = (QbeJump *) n;
        qbe_sb_indent(q);
        qbe_sb_fmt(q, "jmp @.%zu\n", qbe_block_iota(q, jump->block));
    } break;

    case QBE_NODE_BRANCH: {
        QbeBranch *branch = (QbeBranch *) n;
        qbe_compile_node(q, branch->cond);

        size_t then_block = qbe_block_iota(q, branch->then_block);
        size_t else_block = qbe_block_iota(q, branch->else_block);

        qbe_sb_indent(q);
        qbe_sb_fmt(q, "jnz ");
        qbe_sb_node_ssa(q, branch->cond);
        qbe_sb_fmt(q, ", @.%zu, @.%zu\n", then_block, else_block);
    } break;

    case QBE_NODE_RETURN: {
        QbeReturn *ret = (QbeReturn *) n;
        if (ret->value) {
            qbe_compile_node(q, ret->value);
            qbe_sb_indent(q);
            qbe_sb_fmt(q, "ret ");
            qbe_sb_node_ssa(q, ret->value);
            qbe_sb_fmt(q, "\n");
        } else {
            qbe_sb_indent(q);
            qbe_sb_fmt(q, "ret\n");
        }
    } break;

    case QBE_NODE_FN:
        n->ssa = QBE_SSA_GLOBAL;
        break;

    case QBE_NODE_VAR: {
        QbeVar *var = (QbeVar *) n;
        n->ssa = var->local ? QBE_SSA_LOCAL : QBE_SSA_GLOBAL;
    } break;

    case QBE_NODE_BLOCK:
        qbe_sb_fmt(q, "@.%zu\n", qbe_block_iota(q, (QbeBlock *) n));
        break;

    case QBE_NODE_FIELD:
        assert(false && "unreachable");
        break;

    case QBE_NODE_STRUCT:
        assert(false && "unreachable");
        break;

    case QBE_NODE_DEBUG: {
        QbeDebug *debug = (QbeDebug *) n;
        qbe_sb_indent(q);
        qbe_sb_fmt(q, "dbgloc %zu\n", debug->line);
    } break;

    default:
        assert(false && "unreachable");
    }
}

static uint64_t qbe_struct_hash(const QbeStruct *st) {
    uint64_t hash = 14695981039346656037ULL;
    for (QbeNode *it = st->fields.head; it; it = it->next) {
        hash ^= (uint64_t) it->type.kind;
        hash *= 1099511628211ULL;

        if (it->type.kind == QBE_TYPE_STRUCT && it->type.spec) {
            hash ^= (uint64_t) ((uintptr_t) it->type.spec >> 3);
            hash *= 1099511628211ULL;
        }

        QbeField *field = (QbeField *) it;
        hash ^= (uint64_t) field->repeat;
        hash *= 1099511628211ULL;
    }

    hash ^= (uint64_t) st->packed;
    hash *= 1099511628211ULL;
    return hash;
}

static bool qbe_struct_equal(const QbeStruct *a, const QbeStruct *b) {
    if (a->fields_count != b->fields_count || a->packed != b->packed) {
        return false;
    }

    for (QbeNode *na = a->fields.head, *nb = b->fields.head; na && nb; na = na->next, nb = nb->next) {
        if (na->type.kind != nb->type.kind) {
            return false;
        }

        if (na->type.kind == QBE_TYPE_STRUCT && na->type.spec != nb->type.spec) {
            return false;
        }

        QbeField *fa = (QbeField *) na;
        QbeField *fb = (QbeField *) nb;
        if (fa->repeat != fb->repeat) {
            return false;
        }
    }

    return true;
}

static bool qbe_struct_cache_lookup(QbeStructCache *cache, QbeStruct *st) {
    if (!cache->data) {
        return false;
    }

    uint64_t hash = qbe_struct_hash(st);
    size_t   index = hash & (cache->capacity - 1);
    while (cache->data[index].st) {
        QbeStruct *existing = cache->data[index].st;
        if (cache->data[index].hash == hash && qbe_struct_equal(st, existing)) {
            st->node.iota = existing->node.iota;
            return true;
        }
        index = (index + 1) & (cache->capacity - 1);
    }

    return false;
}

static void qbe_struct_cache_insert(QbeStructCache *cache, QbeStruct *st) {
    if (!cache->capacity || (double) (cache->count + 1) / cache->capacity > 0.8) {
        size_t           new_capacity = cache->capacity ? cache->capacity * 2 : 128;
        QbeStructHashed *new_data = calloc(new_capacity, sizeof(QbeStructHashed));

        for (size_t i = 0; i < cache->capacity; i++) {
            if (!cache->data[i].st) {
                continue;
            }

            size_t index = cache->data[i].hash & (new_capacity - 1);
            while (new_data[index].st) {
                index = (index + 1) & (new_capacity - 1);
            }

            new_data[index] = cache->data[i];
        }

        free(cache->data);
        cache->data = new_data;
        cache->capacity = new_capacity;
    }

    uint64_t hash = qbe_struct_hash(st);
    size_t   index = hash & (cache->capacity - 1);
    while (cache->data[index].st) {
        index = (index + 1) & (cache->capacity - 1);
    }

    st->node.iota = cache->iota++;
    cache->data[index].st = st;
    cache->data[index].hash = hash;
    cache->count++;
}

static inline size_t qbe_array_hash(QbeArrayKey key) {
    const size_t h1 = key.type.kind;
    const size_t h2 = (size_t) key.type.spec;
    const size_t h3 = key.count;
    return h1 * 31 + h2 * 17 + h3;
}

static inline bool qbe_array_key_equal(QbeArrayKey a, QbeArrayKey b) {
    return a.count == b.count && a.type.kind == b.type.kind && a.type.spec == b.type.spec;
}

static bool qbe_array_cache_lookup(QbeArrayCache *cache, QbeArrayKey key, QbeType *out) {
    if (cache->capacity == 0) {
        return false;
    }

    size_t mask = cache->capacity - 1;
    size_t index = qbe_array_hash(key) & mask;

    for (size_t i = 0; i < cache->capacity; ++i) {
        QbeArrayEntry *entry = &cache->data[index];
        if (!entry->used) {
            return false;
        }

        if (qbe_array_key_equal(entry->key, key)) {
            *out = entry->type;
            return true;
        }
        index = (index + 1) & mask;
    }

    return false;
}

static void qbe_array_cache_insert(QbeArrayCache *cache, QbeArrayKey key, QbeType type) {
    if (!cache->capacity || (double) (cache->count + 1) / cache->capacity > 0.8) {
        size_t         new_capacity = cache->capacity ? cache->capacity * 2 : 128;
        QbeArrayEntry *new_entries = calloc(new_capacity, sizeof(QbeArrayEntry));

        for (size_t i = 0; i < cache->capacity; ++i) {
            QbeArrayEntry *old = &cache->data[i];
            if (!old->used) {
                continue;
            }

            size_t mask = new_capacity - 1;
            size_t index = qbe_array_hash(old->key) & mask;
            while (new_entries[index].used) {
                index = (index + 1) & mask;
            }

            new_entries[index] = *old;
        }

        free(cache->data);
        cache->data = new_entries;
        cache->capacity = new_capacity;
    }

    size_t mask = cache->capacity - 1;
    size_t index = qbe_array_hash(key) & mask;
    while (cache->data[index].used) {
        index = (index + 1) & mask;
    }

    cache->data[index] = (QbeArrayEntry) {
        .key = key,
        .type = type,
        .used = true,
    };
    cache->count++;
}

// Public API
QbeSV qbe_sv_from_cstr(const char *cstr) {
    return (QbeSV) {.data = (cstr), .count = strlen(cstr)};
}

QbeType qbe_type_basic(QbeTypeKind kind) {
    return (QbeType) {.kind = kind};
}

QbeType qbe_type_struct(QbeStruct *spec) {
    if (!spec->info_ready) {
        spec->info.size = 0;
        spec->info.align = 1;

        for (QbeNode *it = spec->fields.head; it; it = it->next) {
            QbeField *field = (QbeField *) it;
            field->info = qbe_type_info(it->type);
            if (!spec->packed && spec->info.align < field->info.align) {
                spec->info.align = field->info.align;
            }
        }

        size_t offset = 0;
        for (QbeNode *it = spec->fields.head; it; it = it->next) {
            QbeField   *field = (QbeField *) it;
            QbeTypeInfo info = field->info;

            if (!spec->packed) {
                offset += (info.align - (offset % info.align)) % info.align;
            }

            field->offset = offset;
            offset += info.size * field->repeat;
        }

        if (!spec->packed) {
            offset += (spec->info.align - (offset % spec->info.align)) % spec->info.align;
        }

        spec->info.size = offset;
        spec->info_ready = true;
    }

    return (QbeType) {.kind = QBE_TYPE_STRUCT, .spec = spec};
}

QbeType qbe_type_array(Qbe *q, QbeType element_type, size_t count) {
    assert(count);
    const QbeArrayKey key = {
        .type = element_type,
        .count = count,
    };

    QbeType cached = {0};
    if (qbe_array_cache_lookup(&q->array_cache, key, &cached)) {
        return cached;
    }

    QbeStruct *st = qbe_struct_new(q, false);
    QbeField  *element = qbe_struct_add_field(q, st, element_type);
    element->repeat = count;

    const QbeType array_type = qbe_type_struct(st);
    qbe_array_cache_insert(&q->array_cache, key, array_type);
    return array_type;
}

QbeType qbe_typeof(QbeNode *node) {
    return node->type;
}

size_t qbe_sizeof(QbeType type) {
    return qbe_type_info(type).size;
}

size_t qbe_offsetof(QbeField *field) {
    assert(field->parent->info_ready);
    return field->offset;
}

QbeNode *qbe_atom_int(Qbe *q, QbeTypeKind kind, size_t n) {
    QbeNode *atom = qbe_node_alloc(q, QBE_NODE_ATOM, qbe_type_basic(kind));
    atom->ssa = QBE_SSA_INT;
    atom->iota = n;
    return atom;
}

QbeNode *qbe_atom_float(Qbe *q, QbeTypeKind kind, double n) {
    QbeNode *atom = qbe_node_alloc(q, QBE_NODE_ATOM, qbe_type_basic(kind));
    atom->ssa = QBE_SSA_FLOAT;
    atom->real = n;
    return atom;
}

QbeNode *qbe_atom_symbol(Qbe *q, QbeSV name, QbeType type) {
    QbeNode *atom = qbe_node_alloc(q, QBE_NODE_ATOM, type);
    atom->ssa = QBE_SSA_GLOBAL;
    atom->sv = name;
    return atom;
}

QbeFn *qbe_fn_new(Qbe *q, QbeSV name, QbeType return_type) {
    if (return_type.kind == QBE_TYPE_STRUCT && return_type.spec->packed) {
        assert(false && "Returning packed structures directly is not implemented");
    }

    QbeFn *fn = (QbeFn *) qbe_node_alloc(q, QBE_NODE_FN, qbe_type_basic(QBE_TYPE_I64));
    fn->node.sv = name;
    fn->return_type = return_type;
    qbe_build_block(q, fn, qbe_block_new(q));

    qbe_nodes_push(&q->fns, (QbeNode *) fn);
    return fn;
}

QbeNode *qbe_str_new(Qbe *q, QbeSV sv) {
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_I64));
    var->node.ssa = QBE_SSA_GLOBAL;
    var->str = sv;
    var->type = qbe_type_basic(QBE_TYPE_I64);
    qbe_nodes_push(&q->vars, (QbeNode *) var);
    return (QbeNode *) var;
}

QbeBlock *qbe_block_new(Qbe *q) {
    return (QbeBlock *) qbe_node_alloc(q, QBE_NODE_BLOCK, qbe_type_basic(QBE_TYPE_I0));
}

QbeStruct *qbe_struct_new(Qbe *q, bool packed) {
    QbeStruct *st = (QbeStruct *) qbe_node_alloc(q, QBE_NODE_STRUCT, qbe_type_basic(QBE_TYPE_I0));
    st->packed = packed;
    qbe_nodes_push(&q->structs, (QbeNode *) st);
    return st;
}

QbeNode *qbe_var_new(Qbe *q, QbeSV name, QbeType type, const void *data) {
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_I64));
    var->node.sv = name;
    var->type = type;
    var->data = data;
    qbe_nodes_push(&q->vars, (QbeNode *) var);
    return (QbeNode *) var;
}

QbeCall *qbe_call_new(Qbe *q, QbeNode *value, QbeType return_type) {
    QbeCall *call = (QbeCall *) qbe_node_alloc(q, QBE_NODE_CALL, return_type);
    call->fn = value;
    return call;
}

void qbe_build_call(Qbe *q, QbeFn *fn, QbeCall *call) {
    (void) q; // Symmetry
    qbe_nodes_push(&fn->body, (QbeNode *) call);
}

void qbe_call_add_arg(Qbe *q, QbeCall *call, QbeNode *arg) {
    if (arg->type.kind == QBE_TYPE_STRUCT && arg->type.spec->packed) {
        assert(false && "Passing packed structures directly to a function is not implemented");
    }

    QbeArg *container = (QbeArg *) qbe_node_alloc(q, QBE_NODE_ARG, arg->type);
    container->value = arg;
    qbe_nodes_push(&call->args, (QbeNode *) container);
}

void qbe_call_start_variadic(Qbe *q, QbeCall *call) {
    assert(!call->started_variadic);
    call->started_variadic = true;

    QbeArg *container = (QbeArg *) qbe_node_alloc(q, QBE_NODE_ARG, qbe_type_basic(QBE_TYPE_I0));
    container->start_variadic = true;
    qbe_nodes_push(&call->args, (QbeNode *) container);
}

QbeNode *qbe_fn_add_arg(Qbe *q, QbeFn *fn, QbeType arg_type) {
    QbeVar *arg = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, arg_type);
    arg->type = arg_type;
    arg->local = true;
    qbe_nodes_push(&fn->args, (QbeNode *) arg);
    return (QbeNode *) arg;
}

QbeNode *qbe_fn_add_var(Qbe *q, QbeFn *fn, QbeType var_type) {
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_I64));
    var->type = var_type;
    var->local = true;
    qbe_nodes_push(&fn->vars, (QbeNode *) var);
    return (QbeNode *) var;
}

QbeField *qbe_struct_add_field(Qbe *q, QbeStruct *st, QbeType field_type) {
    st->info_ready = false;
    st->fields_count++;

    QbeField *field = (QbeField *) qbe_node_alloc(q, QBE_NODE_FIELD, field_type);
    field->parent = st;
    field->repeat = 1;
    qbe_nodes_push(&st->fields, (QbeNode *) field);
    return field;
}

QbeNode *qbe_build_phi(Qbe *q, QbeFn *fn, QbePhiBranch a, QbePhiBranch b) {
    QbePhi *phi = (QbePhi *) qbe_node_build(q, fn, QBE_NODE_PHI, a.value->type);
    phi->a = a;
    phi->b = b;
    return (QbeNode *) phi;
}

QbeNode *qbe_build_unary(Qbe *q, QbeFn *fn, QbeUnaryOp op, QbeType type, QbeNode *operand) {
    QbeUnary *unary = (QbeUnary *) qbe_node_build(q, fn, QBE_NODE_UNARY, type);
    unary->op = op;
    unary->operand = operand;
    return (QbeNode *) unary;
}

QbeNode *qbe_build_binary(Qbe *q, QbeFn *fn, QbeBinaryOp op, QbeType type, QbeNode *lhs, QbeNode *rhs) {
    QbeBinary *binary = (QbeBinary *) qbe_node_build(q, fn, QBE_NODE_BINARY, type);
    binary->op = op;
    binary->lhs = lhs;
    binary->rhs = rhs;
    return (QbeNode *) binary;
}

QbeNode *qbe_build_load(Qbe *q, QbeFn *fn, QbeNode *ptr, QbeType type, bool is_signed) {
    QbeLoad *load = (QbeLoad *) qbe_node_build(q, fn, QBE_NODE_LOAD, type);
    load->src = ptr;
    load->is_signed = is_signed;
    return (QbeNode *) load;
}

QbeNode *qbe_build_cast(Qbe *q, QbeFn *fn, QbeNode *value, QbeTypeKind type_kind, bool is_signed) {
    if (value->type.kind == type_kind) {
        return value;
    }

    if (!qbe_type_kind_is_float(value->type.kind) && !qbe_type_kind_is_float(type_kind)) {
        static const size_t sizes[QBE_COUNT_TYPES] = {
            [QBE_TYPE_I8] = 8,
            [QBE_TYPE_I16] = 16,
            [QBE_TYPE_I32] = 32,
            [QBE_TYPE_I64] = 64,
        };

        size_t from_size = sizes[value->type.kind];
        assert(from_size);

        assert(type_kind >= 0 && type_kind < QBE_COUNT_TYPES);
        size_t to_size = sizes[type_kind];
        assert(to_size);

        if (to_size <= from_size) {
            // QBE does not need explicit integer truncation
            return value;
        }
    }

    QbeCast *cast = (QbeCast *) qbe_node_build(q, fn, QBE_NODE_CAST, qbe_type_basic(type_kind));
    cast->value = value;
    cast->is_signed = is_signed;
    return (QbeNode *) cast;
}

void qbe_build_store(Qbe *q, QbeFn *fn, QbeNode *ptr, QbeNode *value) {
    QbeStore *store = (QbeStore *) qbe_node_build(q, fn, QBE_NODE_STORE, qbe_type_basic(QBE_TYPE_I0));
    store->dst = ptr;
    store->src = value;
}

void qbe_build_store_zero(Qbe *q, QbeFn *fn, QbeNode *ptr, QbeType type) {
    QbeStore *store = (QbeStore *) qbe_node_build(q, fn, QBE_NODE_STORE, type);
    store->dst = ptr;
}

void qbe_build_store_data(Qbe *q, QbeFn *fn, QbeNode *ptr, QbeType type, const void *data) {
    QbeStore *store = (QbeStore *) qbe_node_build(q, fn, QBE_NODE_STORE, type);
    store->dst = ptr;
    store->data = data;
}

void qbe_build_block(Qbe *q, QbeFn *fn, QbeBlock *block) {
    assert(!q->compiled && "This QBE context is already compiled");
    qbe_nodes_push(&fn->body, (QbeNode *) block);
    fn->current_block = block;
}

void qbe_build_jump(Qbe *q, QbeFn *fn, QbeBlock *block) {
    QbeJump *jump = (QbeJump *) qbe_node_build(q, fn, QBE_NODE_JUMP, qbe_type_basic(QBE_TYPE_I0));
    jump->block = block;
}

void qbe_build_branch(Qbe *q, QbeFn *fn, QbeNode *cond, QbeBlock *then_block, QbeBlock *else_block) {
    QbeBranch *branch = (QbeBranch *) qbe_node_build(q, fn, QBE_NODE_BRANCH, qbe_type_basic(QBE_TYPE_I0));
    branch->cond = cond;
    branch->then_block = then_block;
    branch->else_block = else_block;
}

void qbe_build_return(Qbe *q, QbeFn *fn, QbeNode *value) {
    QbeReturn *ret = (QbeReturn *) qbe_node_build(q, fn, QBE_NODE_RETURN, qbe_type_basic(QBE_TYPE_I0));
    ret->value = value;
}

QbeBlock *qbe_fn_get_current_block(QbeFn *fn) {
    return fn->current_block;
}

void qbe_fn_set_debug(Qbe *q, QbeFn *fn, QbeSV path, size_t line) {
    assert(!q->compiled && "This QBE context is already compiled");
    fn->debug_file = path;
    fn->debug_line = line;
}

void qbe_build_debug_line(Qbe *q, QbeFn *fn, size_t line) {
    QbeDebug *debug = (QbeDebug *) qbe_node_build(q, fn, QBE_NODE_DEBUG, qbe_type_basic(QBE_TYPE_I0));
    debug->line = line;
}

Qbe *qbe_new(void) {
    return calloc(1, sizeof(Qbe));
}

void qbe_free(Qbe *q) {
    arena_free(&q->arena);
    free(q->sb.data);
    free(q->array_cache.data);
    free(q->struct_cache.data);
    free(q);
}

static void qbe_compile_struct(Qbe *q, QbeStruct *st) {
    if (!st->info_ready || st->compiled) {
        return;
    }
    st->compiled = true;

    if (qbe_struct_cache_lookup(&q->struct_cache, st)) {
        return;
    }

    if (!st->packed) {
        for (QbeNode *it = st->fields.head; it; it = it->next) {
            if (it->type.kind == QBE_TYPE_STRUCT) {
                qbe_compile_struct(q, it->type.spec);
            }
        }
    }

    qbe_struct_cache_insert(&q->struct_cache, st);
    qbe_sb_fmt(q, "type :.%zu = align %zu { ", st->node.iota, st->info.align);
    if (st->packed) {
        qbe_sb_fmt(q, "%zu", st->info.size);
    } else {
        for (QbeNode *it = st->fields.head; it; it = it->next) {
            qbe_sb_type(q, it->type);

            QbeField *field = (QbeField *) it;
            if (field->repeat != 1) {
                qbe_sb_fmt(q, " %zu", field->repeat);
            }

            if (it->next) {
                qbe_sb_fmt(q, ", ");
            }
        }
    }
    qbe_sb_fmt(q, " }\n");
}

void qbe_compile(Qbe *q) {
    assert(!q->compiled && "This QBE context is already compiled");
    q->compiled = true;

    {
        size_t iota = 0;

        for (QbeNode *it = q->fns.head; it; it = it->next) {
            if (!it->sv.data) {
                it->iota = iota++;
            }
        }

        for (QbeNode *it = q->vars.head; it; it = it->next) {
            if (!it->sv.data) {
                it->iota = iota++;
            }
        }
    }

    for (QbeNode *it = q->structs.head; it; it = it->next) {
        qbe_compile_struct(q, (QbeStruct *) it);
    }

    for (QbeNode *it = q->vars.head; it; it = it->next) {
        QbeVar *var = (QbeVar *) it;

        if (it->sv.data) {
            qbe_sb_fmt(q, "export ");
        }

        qbe_sb_fmt(q, "data ");
        qbe_compile_node(q, it);
        qbe_sb_node_ssa(q, it);

        if (var->str.data) {
            qbe_sb_fmt(q, " = align 1 { b ");
            qbe_sb_quote_sv(q, var->str);
            qbe_sb_fmt(q, ", b 0 }\n");
        } else {
            QbeTypeInfo info = qbe_type_info(var->type);
            if (var->data) {
                qbe_sb_fmt(q, " = align %zu { ", info.align);

                size_t        remaining = info.size;
                const int8_t *data = var->data;
                while (remaining) {
                    if (*data == 0) {
                        size_t count = 0;
                        for (size_t i = 0; i < remaining && !data[i]; i++) {
                            count++;
                        }

                        if (count >= 8 || remaining < 8) {
                            qbe_sb_fmt(q, "z %zu", count);
                            data += count;
                            remaining -= count;

                            if (remaining) {
                                qbe_sb_fmt(q, ", ");
                            } else {
                                break;
                            }
                        }
                    }

                    if (remaining >= 8) {
                        qbe_sb_fmt(q, "l %ld", *(int64_t *) data);
                        data += 8;
                        remaining -= 8;
                    } else if (remaining >= 4) {
                        qbe_sb_fmt(q, "w %d", *(int32_t *) data);
                        data += 4;
                        remaining -= 4;
                    } else if (remaining >= 2) {
                        qbe_sb_fmt(q, "h %d", *(int16_t *) data);
                        data += 2;
                        remaining -= 2;
                    } else if (remaining >= 1) {
                        qbe_sb_fmt(q, "b %d", *data);
                        data += 1;
                        remaining -= 1;
                    }

                    if (remaining) {
                        qbe_sb_fmt(q, ", ");
                    }
                }

                qbe_sb_fmt(q, " }\n");
            } else {
                qbe_sb_fmt(q, " = align %zu { z %zu }\n", info.align, info.size);
            }
        }
    }

    for (QbeNode *it = q->fns.head; it; it = it->next) {
        QbeFn *fn = (QbeFn *) it;

        if (fn->debug_file.data) {
            qbe_sb_fmt(q, "dbgfile ");
            qbe_sb_quote_sv(q, fn->debug_file);
            qbe_sb_fmt(q, ", %zu\n", fn->debug_line);
        }

        if (it->sv.data) {
            qbe_sb_fmt(q, "export ");
        }

        qbe_sb_fmt(q, "function ");

        if (fn->return_type.kind != QBE_TYPE_I0) {
            qbe_sb_type_ssa(q, fn->return_type);
            qbe_sb_fmt(q, " ");
        }

        qbe_compile_node(q, it);
        qbe_sb_node_ssa(q, it);
        qbe_sb_fmt(q, "(");

        q->locals = 0;
        q->blocks = 1;
        for (QbeNode *arg = fn->args.head; arg; arg = arg->next) {
            arg->iota = q->locals++;

            qbe_sb_type_ssa(q, arg->type);
            qbe_sb_fmt(q, " ");
            qbe_compile_node(q, arg);
            qbe_sb_node_ssa(q, arg);
            if (arg->next) {
                qbe_sb_fmt(q, ", ");
            }
        }

        qbe_sb_fmt(q, ") {\n");
        assert(fn->body.head && fn->body.head->kind == QBE_NODE_BLOCK);
        qbe_compile_node(q, fn->body.head);

        for (QbeNode *var = fn->vars.head; var; var = var->next) {
            var->iota = q->locals++;
            qbe_compile_node(q, var);

            qbe_sb_indent(q);
            qbe_sb_node_ssa(q, var);

            QbeTypeInfo info = qbe_type_info(((QbeVar *) var)->type);
            if (info.align < 4) {
                // Typical C compilers usually align stack variables by 4
                info.align = 4;
            }

            qbe_sb_fmt(q, " =l alloc%zu %zu\n", info.align, info.size);
        }

        for (QbeNode *stmt = fn->body.head->next; stmt; stmt = stmt->next) {
            qbe_compile_node(q, stmt);
        }

        qbe_sb_fmt(q, "}\n");
    }
}

bool qbe_has_been_compiled(Qbe *q) {
    return q->compiled;
}

QbeSV qbe_get_compiled_program(Qbe *q) {
    assert(q->compiled);
    return (QbeSV) {.data = q->sb.data, .count = q->sb.count};
}
