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
#include <stddef.h>
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
};

typedef struct {
    QbeNode  node;
    QbeNode *value;
    bool     is_signed;
} QbeCast;

typedef struct {
    QbeNode  node;
    QbeNode *src;
} QbeLoad;

typedef struct {
    QbeNode  node;
    QbeNode *dst;
    QbeNode *src;
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
};

typedef struct {
    QbeNode node;

    bool    local;
    QbeSV   str;
    QbeType type;
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

    size_t      offset;
    QbeTypeInfo info;

    QbeStruct *parent;
};

struct QbeStruct {
    QbeNode node;

    QbeNodes fields;
    size_t   fields_count;

    QbeTypeInfo info;
    bool        info_ready;

    bool packed;
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
} QbeHashedStruct;

typedef struct {
    QbeHashedStruct *data;
    size_t           count;
    size_t           capacity;
    size_t           iota;
} QbeHashedStructTable;

struct Qbe {
    Arena arena;

    QbeNodes fns;
    QbeNodes vars;
    QbeNodes structs;

    size_t blocks;
    size_t locals;

    QbeHashedStructTable hashed_struct_table;

    bool  compiled;
    QbeSB sb;
};

static bool qbe_type_kind_is_float(QbeTypeKind k) {
    return k == QBE_TYPE_F32 || k == QBE_TYPE_F64;
}

static_assert(QBE_COUNT_TYPES == 9, "");
static QbeTypeInfo qbe_type_info(QbeType type) {
    switch (type.kind) {
    case QBE_TYPE_I8:
        return (QbeTypeInfo) {.size = 1, .align = 1};

    case QBE_TYPE_I16:
        return (QbeTypeInfo) {.size = 2, .align = 2};

    case QBE_TYPE_I32:
    case QBE_TYPE_F32:
        return (QbeTypeInfo) {.size = 4, .align = 4};

    case QBE_TYPE_I64:
    case QBE_TYPE_F64:
    case QBE_TYPE_PTR:
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

static_assert(QBE_COUNT_TYPES == 9, "");
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

    case QBE_TYPE_PTR:
        qbe_sb_fmt(q, "l");
        break;

    case QBE_TYPE_STRUCT:
        qbe_sb_fmt(q, ":.%zu", type.spec->node.iota);
        break;

    default:
        assert(false && "unreachable");
    }
}

static_assert(QBE_COUNT_TYPES == 9, "");
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

    case QBE_TYPE_PTR:
        qbe_sb_fmt(q, "l");
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

        switch (unary->op) {
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

        switch (binary->op) {
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
            qbe_sb_fmt(q, "csgt");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_UGT:
            qbe_sb_fmt(q, "cugt");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_SGE:
            qbe_sb_fmt(q, "csge");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_UGE:
            qbe_sb_fmt(q, "cuge");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_SLT:
            qbe_sb_fmt(q, "cslt");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_ULT:
            qbe_sb_fmt(q, "cult");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_SLE:
            qbe_sb_fmt(q, "csle");
            qbe_sb_type_ssa(q, binary->lhs->type);
            break;

        case QBE_BINARY_ULE:
            qbe_sb_fmt(q, "cule");
            qbe_sb_type_ssa(q, binary->lhs->type);
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
            qbe_compile_node(q, it->value);
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
            qbe_sb_type(q, it->value->type);
            qbe_sb_fmt(q, " ");
            qbe_sb_node_ssa(q, it->value);

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
        qbe_sb_type(q, n->type);
        qbe_sb_fmt(q, " ");
        qbe_sb_node_ssa(q, load->src);
        qbe_sb_fmt(q, "\n");
    } break;

    case QBE_NODE_STORE: {
        QbeStore *store = (QbeStore *) n;
        qbe_compile_node(q, store->dst);
        qbe_compile_node(q, store->src);

        if (store->src->type.kind == QBE_TYPE_STRUCT) {
            n->ssa = QBE_SSA_LOCAL;

            qbe_sb_indent(q);
            qbe_sb_fmt(q, "blit ");
            qbe_sb_node_ssa(q, store->src);
            qbe_sb_fmt(q, ", ");
            qbe_sb_node_ssa(q, store->dst);

            QbeTypeInfo info = qbe_type_info(store->src->type);
            qbe_sb_fmt(q, ", %zu\n", info.size);
            return;
        }

        n->ssa = QBE_SSA_LOCAL;
        n->iota = q->locals++;

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
    }

    return true;
}

static void qbe_hashed_struct_table_resize(QbeHashedStructTable *table, size_t new_capacity) {
    QbeHashedStruct *old_data = table->data;
    size_t           old_capacity = table->capacity;

    table->data = calloc(new_capacity, sizeof(QbeHashedStruct));
    table->capacity = new_capacity;
    table->count = 0;

    for (size_t i = 0; i < old_capacity; ++i) {
        if (!old_data[i].st) {
            continue;
        }

        size_t index = old_data[i].hash & (new_capacity - 1);
        while (table->data[index].st) {
            index = (index + 1) & (new_capacity - 1);
        }

        table->data[index] = old_data[i];
        table->count++;
    }

    free(old_data);
}

static bool qbe_hashed_struct_table_new(QbeHashedStructTable *table, QbeStruct *st) {
    if (!table->data) {
        table->capacity = 128;
        table->data = calloc(table->capacity, sizeof(QbeHashedStruct));
    }

    uint64_t hash = qbe_struct_hash(st);
    size_t   index = hash & (table->capacity - 1);

    while (table->data[index].st) {
        QbeStruct *existing = table->data[index].st;
        if (table->data[index].hash == hash && qbe_struct_equal(st, existing)) {
            st->node.iota = existing->node.iota;
            return false; // A struct like this already exists
        }

        index = (index + 1) & (table->capacity - 1);
    }

    if ((double) (table->count + 1) / table->capacity > 0.8) {
        qbe_hashed_struct_table_resize(table, table->capacity * 2);
        return qbe_hashed_struct_table_new(table, st);
    }

    st->node.iota = table->iota++;
    table->data[index].st = st;
    table->data[index].hash = hash;
    table->count++;
    return true; // A new struct
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
            offset += info.size;
        }

        if (!spec->packed) {
            offset += (spec->info.align - (offset % spec->info.align)) % spec->info.align;
        }

        spec->info.size = offset;
        spec->info_ready = true;
    }

    return (QbeType) {.kind = QBE_TYPE_STRUCT, .spec = spec};
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
    QbeFn *fn = (QbeFn *) qbe_node_alloc(q, QBE_NODE_FN, qbe_type_basic(QBE_TYPE_PTR));
    fn->node.sv = name;
    fn->return_type = return_type;
    qbe_nodes_push(&q->fns, (QbeNode *) fn);
    return fn;
}

QbeNode *qbe_var_new(Qbe *q, QbeSV name, QbeType type) {
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_PTR));
    var->node.sv = name;
    var->type = type;
    qbe_nodes_push(&q->vars, (QbeNode *) var);
    return (QbeNode *) var;
}

QbeNode *qbe_str_new(Qbe *q, QbeSV sv) {
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_PTR));
    var->node.ssa = QBE_SSA_GLOBAL;
    var->str = sv;
    var->type = qbe_type_basic(QBE_TYPE_PTR);
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

void qbe_call_add_arg(Qbe *q, QbeCall *call, QbeNode *arg) {
    QbeArg *container = (QbeArg *) qbe_node_alloc(q, QBE_NODE_ARG, arg->type);
    container->value = arg;
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
    QbeVar *var = (QbeVar *) qbe_node_alloc(q, QBE_NODE_VAR, qbe_type_basic(QBE_TYPE_PTR));
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
    qbe_nodes_push(&st->fields, (QbeNode *) field);
    return field;
}

QbeNode *qbe_build_phi(Qbe *q, QbeFn *fn, QbePhiBranch a, QbePhiBranch b) {
    QbePhi *phi = (QbePhi *) qbe_node_build(q, fn, QBE_NODE_PHI, a.value->type);
    phi->a = a;
    phi->b = b;
    return (QbeNode *) phi;
}

QbeCall *qbe_build_call(Qbe *q, QbeFn *fn, QbeNode *value, QbeType return_type) {
    QbeCall *call = (QbeCall *) qbe_node_build(q, fn, QBE_NODE_CALL, return_type);
    call->fn = value;
    return call;
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

QbeNode *qbe_build_load(Qbe *q, QbeFn *fn, QbeNode *ptr, QbeType type) {
    QbeLoad *load = (QbeLoad *) qbe_node_build(q, fn, QBE_NODE_LOAD, type);
    load->src = ptr;
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
            [QBE_TYPE_PTR] = 64,
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

void qbe_build_block(Qbe *q, QbeFn *fn, QbeBlock *block) {
    assert(!q->compiled && "This QBE context is already compiled");
    qbe_nodes_push(&fn->body, (QbeNode *) block);
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

void qbe_fn_set_debug_file(Qbe *q, QbeFn *fn, QbeSV path) {
    assert(!q->compiled && "This QBE context is already compiled");
    fn->debug_file = path;
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
    free(q->hashed_struct_table.data);
    free(q);
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
        QbeStruct *st = (QbeStruct *) it;
        if (!qbe_hashed_struct_table_new(&q->hashed_struct_table, st)) {
            continue;
        }

        qbe_sb_fmt(q, "type :.%zu = { ", it->iota);
        for (QbeNode *field = st->fields.head; field; field = field->next) {
            qbe_sb_type(q, field->type);
            if (field->next) {
                qbe_sb_fmt(q, ", ");
            }
        }
        qbe_sb_fmt(q, " }\n");
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
            qbe_sb_fmt(q, " = align %zu { z %zu }\n", info.align, info.size);
        }
    }

    for (QbeNode *it = q->fns.head; it; it = it->next) {
        QbeFn *fn = (QbeFn *) it;

        if (fn->debug_file.data) {
            qbe_sb_fmt(q, "dbgfile ");
            qbe_sb_quote_sv(q, fn->debug_file);
            qbe_sb_fmt(q, "\n");
        }

        if (it->sv.data) {
            qbe_sb_fmt(q, "export ");
        }

        qbe_sb_fmt(q, "function ");

        if (fn->return_type.kind != QBE_TYPE_I0) {
            qbe_sb_type(q, fn->return_type);
            qbe_sb_fmt(q, " ");
        }

        qbe_compile_node(q, it);
        qbe_sb_node_ssa(q, it);
        qbe_sb_fmt(q, "(");

        q->locals = 0;
        q->blocks = 0;
        for (QbeNode *arg = fn->args.head; arg; arg = arg->next) {
            arg->iota = q->locals++;

            qbe_sb_type(q, arg->type);
            qbe_sb_fmt(q, " ");
            qbe_compile_node(q, arg);
            qbe_sb_node_ssa(q, arg);
            if (arg->next) {
                qbe_sb_fmt(q, ", ");
            }
        }

        qbe_sb_fmt(q, ") {\n@.%zu\n", q->blocks++);

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

        for (QbeNode *stmt = fn->body.head; stmt; stmt = stmt->next) {
            qbe_compile_node(q, stmt);
        }

        qbe_sb_fmt(q, "}\n");
    }
}

bool qbe_has_been_compiled(Qbe *q) {
    return q->compiled;
}

QbeSV qbe_get_compiled_program(Qbe *q) {
    return (QbeSV) {.data = q->sb.data, .count = q->sb.count};
}
