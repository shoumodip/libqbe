#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

static void debug_program(Qbe *q) {
    if (0) {
        QbeSV program = qbe_get_compiled_program(q);
        fwrite(program.data, program.count, 1, stdout);
    }
}

static void example_if(void) {
    Qbe *q = qbe_new();

    {
        QbeFn   *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));
        QbeNode *puts = qbe_atom_symbol(q, qbe_sv_from_cstr("puts"), qbe_type_basic(QBE_TYPE_PTR));

        QbeBlock *then_block = qbe_block_new(q);
        QbeBlock *else_block = qbe_block_new(q);
        QbeBlock *merge_block = qbe_block_new(q);

        // Condition
        QbeNode *cond = qbe_build_binary(
            q,
            main,
            QBE_BINARY_SLT,
            qbe_type_basic(QBE_TYPE_I32),
            qbe_atom_int(q, QBE_TYPE_I64, 1),
            qbe_atom_int(q, QBE_TYPE_I64, 2));

        qbe_build_branch(q, main, cond, then_block, else_block);

        // Then
        qbe_build_block(q, main, then_block);
        QbeCall *first = qbe_build_call(q, main, puts, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, first, qbe_str_new(q, qbe_sv_from_cstr("First")));
        qbe_build_jump(q, main, merge_block);

        // Else
        qbe_build_block(q, main, else_block);
        QbeCall *second = qbe_build_call(q, main, puts, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, second, qbe_str_new(q, qbe_sv_from_cstr("Second")));
        qbe_build_jump(q, main, merge_block);

        // Merge
        qbe_build_block(q, main, merge_block);
        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", NULL, 0);
    qbe_free(q);
    exit(result);
}

static void example_while(void) {
    Qbe *q = qbe_new();

    {
        QbeNode *i = qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_I64));

        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));
        qbe_fn_set_debug_file(q, main, qbe_sv_from_cstr("hello.c"));

        QbeBlock *cond_block = qbe_block_new(q);
        QbeBlock *body_block = qbe_block_new(q);
        QbeBlock *over_block = qbe_block_new(q);

        // Condition
        qbe_build_block(q, main, cond_block);
        qbe_build_debug_line(q, main, 6);

        QbeNode *cond = qbe_build_binary(
            q,
            main,
            QBE_BINARY_SLT,
            qbe_type_basic(QBE_TYPE_I32),
            qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64)),
            qbe_atom_int(q, QBE_TYPE_I64, 10));

        qbe_build_branch(q, main, cond, body_block, over_block);

        // Body
        qbe_build_block(q, main, body_block);
        qbe_build_debug_line(q, main, 7);

        QbeNode *printf = qbe_atom_symbol(q, qbe_sv_from_cstr("printf"), qbe_type_basic(QBE_TYPE_PTR));
        QbeCall *call = qbe_build_call(q, main, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%ld\n")));
        qbe_call_add_arg(q, call, qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64)));

        qbe_build_debug_line(q, main, 8);
        qbe_build_store(
            q,
            main,
            i,
            qbe_build_binary(
                q,
                main,
                QBE_BINARY_ADD,
                qbe_type_basic(QBE_TYPE_I64),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64)),
                qbe_atom_int(q, QBE_TYPE_I64, 1)));

        // Loop
        qbe_build_jump(q, main, cond_block);

        // Over
        qbe_build_block(q, main, over_block);
        qbe_build_debug_line(q, main, 11);
        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", NULL, 0);
    qbe_free(q);
    exit(result);
}

static void example_struct(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeStruct *Vec3 = qbe_struct_new(q, false);
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));

        QbeNode *v = qbe_fn_add_var(q, main, qbe_type_struct(Vec3));
        QbeNode *newVec3 = qbe_atom_symbol(q, qbe_sv_from_cstr("newVec3"), qbe_type_basic(QBE_TYPE_PTR));
        QbeNode *printVec3 = qbe_atom_symbol(q, qbe_sv_from_cstr("printVec3"), qbe_type_basic(QBE_TYPE_PTR));

        QbeCall *newVec3_call = qbe_build_call(q, main, newVec3, qbe_type_struct(Vec3));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 69));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 420));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 1337));
        qbe_build_store(q, main, v, (QbeNode *) newVec3_call);

        QbeCall *printVec3_call = qbe_build_call(q, main, printVec3, qbe_type_basic(QBE_TYPE_I0));
        qbe_call_add_arg(q, printVec3_call, qbe_build_load(q, main, v, qbe_type_struct(Vec3)));

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    const char *flags[] = {
        "-L.",
        "-lvec3",
    };
    const int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", flags, len(flags));
    qbe_free(q);
    exit(result);
}

static void example_cast(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeNode *i8 = qbe_atom_int(q, QBE_TYPE_I8, 0);
        qbe_build_cast(q, main, i8, QBE_TYPE_I16, false);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    const int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", NULL, 0);
    qbe_free(q);
    exit(result);
}

static void example_float(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeNode *x = qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_F32));
        qbe_build_store(q, main, x, qbe_atom_float(q, QBE_TYPE_F32, 420.69));

        QbeNode *printf = qbe_atom_symbol(q, qbe_sv_from_cstr("printf"), qbe_type_basic(QBE_TYPE_PTR));
        QbeCall *call = qbe_build_call(q, main, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%g\n")));
        qbe_call_add_arg(
            q,
            call,
            qbe_build_cast(q, main, qbe_build_load(q, main, x, qbe_type_basic(QBE_TYPE_F32)), QBE_TYPE_F64, true));

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    const int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", NULL, 0);
    qbe_free(q);
    exit(result);
}

static void example_phi(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeBlock *is_true = qbe_block_new(q);
        QbeBlock *is_false = qbe_block_new(q);
        QbeBlock *phi = qbe_block_new(q);

        qbe_build_branch(q, main, qbe_atom_int(q, QBE_TYPE_I32, true), is_true, is_false);

        // True
        qbe_build_block(q, main, is_true);
        qbe_build_jump(q, main, phi);

        // False
        qbe_build_block(q, main, is_false);
        qbe_build_jump(q, main, phi);

        // Phi
        qbe_build_block(q, main, phi);
        QbePhiBranch phi_is_true = {
            .block = is_true,
            .value = qbe_atom_int(q, QBE_TYPE_I64, 69),
        };

        QbePhiBranch phi_is_false = {
            .block = is_false,
            .value = qbe_atom_int(q, QBE_TYPE_I64, 420),
        };

        QbeNode *x = qbe_build_phi(q, main, phi_is_true, phi_is_false);
        QbeNode *printf = qbe_atom_symbol(q, qbe_sv_from_cstr("printf"), qbe_type_basic(QBE_TYPE_PTR));

        QbeCall *call = qbe_build_call(q, main, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%ld\n")));
        qbe_call_add_arg(q, call, x);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    qbe_compile(q);
    debug_program(q);
    const int result = qbe_generate(q, QBE_TARGET_DEFAULT, "hello", NULL, 0);
    qbe_free(q);
    exit(result);
}

int main(void) {
    example_while();
}
