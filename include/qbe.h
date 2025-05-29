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

#ifndef QBE_H
#define QBE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *data;
    size_t      count;
} QbeSV;

QbeSV qbe_sv_from_cstr(const char *cstr);

typedef enum {
    QBE_TYPE_I0,
    QBE_TYPE_I8,
    QBE_TYPE_I16,
    QBE_TYPE_I32,
    QBE_TYPE_I64,
    QBE_TYPE_PTR,
    QBE_TYPE_STRUCT,
    QBE_COUNT_TYPES
} QbeTypeKind;

typedef struct {
    QbeTypeKind kind;
    size_t      data;
} QbeType;

QbeType qbe_type_basic(QbeTypeKind kind);

typedef enum {
    QBE_VALUE_INT,
    QBE_VALUE_LOCAL,
    QBE_VALUE_GLOBAL,
    QBE_COUNT_VALUES,
} QbeValueKind;

typedef struct {
    QbeValueKind kind;

    QbeSV  name;
    size_t iota;

    QbeType type;
} QbeValue;

QbeValue qbe_value_int(QbeTypeKind kind, size_t n);
QbeValue qbe_value_import(QbeSV name, QbeType type);

typedef struct {
    char  *data;
    size_t count;
    size_t capacity;
} QbeSB;

typedef struct {
    QbeValue value;
    QbeSV    sv;
} QbeStr;

typedef struct {
    QbeStr *data;
    size_t  count;
    size_t  capacity;
} QbeStrs;

typedef struct {
    size_t align;
    size_t size;
} QbeTypeInfo;

typedef struct {
    QbeType type;
    size_t  offset;

    QbeTypeInfo info;
} QbeField;

typedef struct {
    QbeField *data;
    size_t    count;
    size_t    capacity;
} QbeFields;

typedef struct {
    size_t fields;
    size_t count;

    QbeTypeInfo info;
} QbeStruct;

typedef struct {
    QbeStruct *data;
    size_t     count;
    size_t     capacity;
} QbeStructs;

typedef struct {
    QbeSB      sb;
    QbeFields  fields;
    QbeStructs structs;

    size_t block_iota;
    size_t local_iota;
    size_t global_iota;

    bool    local;
    QbeStrs local_strs; // Defer string literal creation in local scope
} Qbe;

void qbe_free(Qbe *q);

QbeTypeInfo qbe_type_info(Qbe *q, QbeType type);
QbeType     qbe_type_struct(Qbe *q, QbeType *fields, size_t count);

QbeValue qbe_emit_str(Qbe *q, QbeSV sv);
QbeValue qbe_emit_var(Qbe *q, QbeSV name, QbeType type);
QbeValue qbe_emit_func(Qbe *q, QbeSV name, QbeType return_type, QbeType *arg_types, size_t arity);

QbeValue qbe_emit_load(Qbe *q, QbeValue ptr, QbeType type);
void     qbe_emit_store(Qbe *q, QbeValue ptr, QbeValue value);

QbeValue qbe_emit_call(Qbe *q, QbeValue func, QbeType return_type, QbeValue *args, size_t arity);

typedef enum {
    QBE_UNARY_NEG,
    QBE_UNARY_BNOT,
    QBE_UNARY_LNOT,
    QBE_COUNT_UNARYS
} QbeUnary;

QbeValue qbe_emit_unary(Qbe *q, QbeUnary op, QbeType type, QbeValue operand);

typedef enum {
    QBE_BINARY_ADD,
    QBE_BINARY_SUB,
    QBE_BINARY_MUL,
    QBE_BINARY_SDIV,
    QBE_BINARY_UDIV,
    QBE_BINARY_SMOD,
    QBE_BINARY_UMOD,

    QBE_BINARY_OR,
    QBE_BINARY_AND,
    QBE_BINARY_XOR,
    QBE_BINARY_SHL,
    QBE_BINARY_SSHR,
    QBE_BINARY_USHR,

    QBE_BINARY_SGT,
    QBE_BINARY_UGT,
    QBE_BINARY_SGE,
    QBE_BINARY_UGE,
    QBE_BINARY_SLT,
    QBE_BINARY_ULT,
    QBE_BINARY_SLE,
    QBE_BINARY_ULE,
    QBE_BINARY_EQ,
    QBE_BINARY_NE,

    QBE_COUNT_BINARYS
} QbeBinary;

QbeValue qbe_emit_binary(Qbe *q, QbeBinary op, QbeType type, QbeValue lhs, QbeValue rhs);

typedef struct {
    size_t iota;
} QbeBlock;

QbeBlock qbe_new_block(Qbe *q);

void qbe_emit_block(Qbe *q, QbeBlock block);
void qbe_emit_jump(Qbe *q, QbeBlock block);
void qbe_emit_branch(Qbe *q, QbeValue cond, QbeBlock then_block, QbeBlock else_block);

void qbe_emit_return(Qbe *q, QbeValue *value);
void qbe_emit_structs(Qbe *q);
void qbe_emit_func_end(Qbe *q);

typedef enum {
    QBE_TARGET_DEFAULT,
    QBE_TARGET_X86_64_SYSV,
    QBE_TARGET_X86_64_APPLE,
    QBE_TARGET_ARM64,
    QBE_TARGET_ARM64_APPLE,
    QBE_TARGET_RV64
} QbeTarget;

int qbe_compile(
    Qbe *q, QbeTarget target, const char *output, const char **flags, size_t flags_count);

#endif // QBE_H
