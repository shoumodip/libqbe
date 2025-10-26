#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

#include "qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

// Executor
static void generate_executable(Qbe *q, const char *name, const char **link_flags, size_t link_flags_count) {
    const int code = qbe_generate(q, QBE_TARGET_DEFAULT, name, link_flags, link_flags_count);
    if (code) {
        fprintf(stderr, "ERROR: Generation of '%s' exited abnormally with code %d\n", name, code);
    }
}

// Examples
static void example_if(void) {
    Qbe *q = qbe_new();

    {
        QbeFn   *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));
        QbeNode *puts = qbe_atom_extern_fn(q, qbe_sv_from_cstr("puts"));

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
        QbeCall *first = qbe_call_new(q, puts, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, first, qbe_str_new(q, qbe_sv_from_cstr("First")));
        qbe_build_call(q, main, first);
        qbe_build_jump(q, main, merge_block);

        // Else
        qbe_build_block(q, main, else_block);
        QbeCall *second = qbe_call_new(q, puts, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, second, qbe_str_new(q, qbe_sv_from_cstr("Second")));
        qbe_build_call(q, main, second);
        qbe_build_jump(q, main, merge_block);

        // Merge
        qbe_build_block(q, main, merge_block);
        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(q, "example_if", NULL, 0);
    qbe_free(q);
}

static void example_struct(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeStruct *Vec3 = qbe_struct_new(q, false);
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));

        QbeStruct *Vec3_duplicate = qbe_struct_new(q, false);
        qbe_struct_add_field(q, Vec3_duplicate, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3_duplicate, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3_duplicate, qbe_type_basic(QBE_TYPE_I64));

        QbeNode *v = qbe_fn_add_var(q, main, qbe_type_struct(Vec3));
        QbeNode *newVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("newVec3"));
        QbeNode *printVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printVec3"));

        QbeCall *newVec3_call = qbe_call_new(q, newVec3, qbe_type_struct(Vec3));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 69));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 420));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 1337));
        qbe_build_call(q, main, newVec3_call);
        qbe_build_store(q, main, v, (QbeNode *) newVec3_call);

        QbeCall *printVec3_call = qbe_call_new(q, printVec3, qbe_type_basic(QBE_TYPE_I0));
        qbe_call_add_arg(q, printVec3_call, qbe_build_load(q, main, v, qbe_type_struct(Vec3_duplicate), true));
        qbe_build_call(q, main, printVec3_call);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    const char *flags[] = {
        "-L.",
        "-lvec3",
    };
    generate_executable(q, "example_struct", flags, len(flags));
    qbe_free(q);
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
    generate_executable(q, "example_cast", NULL, 0);
    qbe_free(q);
}

static void example_float(void) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeNode *x = (QbeNode *) qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_F32));
        qbe_build_store(q, main, x, qbe_atom_float(q, QBE_TYPE_F32, 420.69));

        QbeNode *printf = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printf"));
        QbeCall *call = qbe_call_new(q, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%g\n")));
        qbe_call_start_variadic(q, call);
        qbe_call_add_arg(
            q,
            call,
            qbe_build_cast(
                q, main, qbe_build_load(q, main, x, qbe_type_basic(QBE_TYPE_F32), true), QBE_TYPE_F64, true));

        qbe_build_call(q, main, call);
        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(q, "example_float", NULL, 0);
    qbe_free(q);
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
        QbeNode *printf = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printf"));

        QbeCall *call = qbe_call_new(q, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%ld\n")));
        qbe_call_start_variadic(q, call);
        qbe_call_add_arg(q, call, x);
        qbe_build_call(q, main, call);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(q, "example_phi", NULL, 0);
    qbe_free(q);
}

static void example_while_with_debug(void) {
    Qbe *q = qbe_new();

    {
        QbeNode *i = (QbeNode *) qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_I64));

        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));
        qbe_fn_set_debug(q, main, qbe_sv_from_cstr("while_with_debug.c"), 5);

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
            qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
            qbe_atom_int(q, QBE_TYPE_I64, 10));

        qbe_build_branch(q, main, cond, body_block, over_block);

        // Body
        qbe_build_block(q, main, body_block);
        qbe_build_debug_line(q, main, 7);

        QbeNode *printf = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printf"));
        QbeCall *call = qbe_call_new(q, printf, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%ld\n")));
        qbe_call_start_variadic(q, call);
        qbe_call_add_arg(q, call, qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true));
        qbe_build_call(q, main, call);

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
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, 1)));

        // Loop
        qbe_build_jump(q, main, cond_block);

        // Over
        qbe_build_block(q, main, over_block);
        qbe_build_debug_line(q, main, 11);
        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(q, "example_while_with_debug", NULL, 0);
    qbe_free(q);
}

static void example_array(void) {
    Qbe *q = qbe_new();

    {
        const size_t n = 10;

        QbeNode *i = (QbeNode *) qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_I64));
        QbeNode *xs = (QbeNode *) qbe_var_new(q, (QbeSV) {0}, qbe_type_array(q, qbe_type_basic(QBE_TYPE_I64), n));

        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        {
            QbeBlock *cond_block = qbe_block_new(q);
            QbeBlock *body_block = qbe_block_new(q);
            QbeBlock *over_block = qbe_block_new(q);

            // Condition
            qbe_build_block(q, main, cond_block);

            QbeNode *cond = qbe_build_binary(
                q,
                main,
                QBE_BINARY_SLT,
                qbe_type_basic(QBE_TYPE_I32),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, n));

            qbe_build_branch(q, main, cond, body_block, over_block);

            // Body
            qbe_build_block(q, main, body_block);

            QbeNode *offset = qbe_build_binary(
                q,
                main,
                QBE_BINARY_MUL,
                qbe_type_basic(QBE_TYPE_I64),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, qbe_sizeof(qbe_type_basic(QBE_TYPE_I64))));

            QbeNode *value = qbe_build_binary(
                q,
                main,
                QBE_BINARY_MUL,
                qbe_type_basic(QBE_TYPE_I64),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, 2));

            qbe_build_store(
                q, main, qbe_build_binary(q, main, QBE_BINARY_ADD, qbe_type_basic(QBE_TYPE_I64), xs, offset), value);

            qbe_build_store(
                q,
                main,
                i,
                qbe_build_binary(
                    q,
                    main,
                    QBE_BINARY_ADD,
                    qbe_type_basic(QBE_TYPE_I64),
                    qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                    qbe_atom_int(q, QBE_TYPE_I64, 1)));

            // Loop
            qbe_build_jump(q, main, cond_block);

            // Over
            qbe_build_block(q, main, over_block);
            qbe_build_store(q, main, i, qbe_atom_int(q, QBE_TYPE_I64, 0));
        }

        {
            QbeBlock *cond_block = qbe_block_new(q);
            QbeBlock *body_block = qbe_block_new(q);
            QbeBlock *over_block = qbe_block_new(q);

            // Condition
            qbe_build_block(q, main, cond_block);

            QbeNode *cond = qbe_build_binary(
                q,
                main,
                QBE_BINARY_SLT,
                qbe_type_basic(QBE_TYPE_I32),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, n));

            qbe_build_branch(q, main, cond, body_block, over_block);

            // Body
            qbe_build_block(q, main, body_block);

            QbeNode *printf = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printf"));
            QbeCall *call = qbe_call_new(q, printf, qbe_type_basic(QBE_TYPE_I32));
            qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("%ld\n")));
            qbe_call_start_variadic(q, call);

            QbeNode *offset = qbe_build_binary(
                q,
                main,
                QBE_BINARY_MUL,
                qbe_type_basic(QBE_TYPE_I64),
                qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                qbe_atom_int(q, QBE_TYPE_I64, qbe_sizeof(qbe_type_basic(QBE_TYPE_I64))));

            qbe_call_add_arg(
                q,
                call,
                qbe_build_load(
                    q,
                    main,
                    qbe_build_binary(q, main, QBE_BINARY_ADD, qbe_type_basic(QBE_TYPE_I64), xs, offset),
                    qbe_type_basic(QBE_TYPE_I64),
                    true));

            qbe_build_call(q, main, call);

            qbe_build_store(
                q,
                main,
                i,
                qbe_build_binary(
                    q,
                    main,
                    QBE_BINARY_ADD,
                    qbe_type_basic(QBE_TYPE_I64),
                    qbe_build_load(q, main, i, qbe_type_basic(QBE_TYPE_I64), true),
                    qbe_atom_int(q, QBE_TYPE_I64, 1)));

            // Loop
            qbe_build_jump(q, main, cond_block);

            // Over
            qbe_build_block(q, main, over_block);
        }

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(q, "example_array", NULL, 0);
    qbe_free(q);
}

static void example_extern_var(void) {
    Qbe *q = qbe_new();
    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        const char *stderr_name = "stderr";
        if (qbe_target_default() == QBE_TARGET_ARM64_MACOS || qbe_target_default() == QBE_TARGET_X86_64_MACOS) {
            stderr_name = "__stderrp";
        }

        QbeNode *fputs_symbol = qbe_atom_extern_fn(q, qbe_sv_from_cstr("fputs"));
        QbeNode *stderr_symbol = qbe_atom_extern(q, qbe_sv_from_cstr(stderr_name), qbe_type_basic(QBE_TYPE_I64));

        QbeCall *call = qbe_call_new(q, fputs_symbol, qbe_type_basic(QBE_TYPE_I32));
        qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("Hello, world!\n")));
        qbe_call_add_arg(q, call, qbe_build_load(q, main, stderr_symbol, qbe_type_basic(QBE_TYPE_I64), false));
        qbe_build_call(q, main, call);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    generate_executable(q, "example_extern_var", NULL, 0);
    qbe_free(q);
}

static void example_var_init(void) {
    Qbe *q = qbe_new();
    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeStruct *Vec3 = qbe_struct_new(q, false);
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));

        // size_t  v_data[] = {69, 420};
        QbeType v_type = qbe_type_struct(Vec3);
        QbeVar *v = qbe_var_new(q, qbe_sv_from_cstr("v"), v_type);
        // qbe_var_init_add_data(q, v, &v_data, sizeof(v_data));

        // QbeType p_type = qbe_type_basic(QBE_TYPE_I64);
        // QbeVar *p = qbe_var_new(q, qbe_sv_from_cstr("p"), p_type);
        // qbe_var_init_add_node(q, p, (QbeNode *) v);

        QbeNode *newVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("newVec3"));
        QbeNode *printVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printVec3"));

        QbeCall *newVec3_call = qbe_call_new(q, newVec3, v_type);
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 69));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 420));
        qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 0));
        qbe_build_call(q, main, newVec3_call);
        qbe_build_store(q, main, (QbeNode *) v, (QbeNode *) newVec3_call);

        QbeCall *printVec3_call = qbe_call_new(q, printVec3, qbe_type_basic(QBE_TYPE_I0));
        // qbe_call_add_arg(
        //     q, call, qbe_build_load(q, main, qbe_build_load(q, main, (QbeNode *) p, p_type, false), v_type, false));
        qbe_call_add_arg(q, printVec3_call, qbe_build_load(q, main, (QbeNode *) v, v_type, false));
        qbe_build_call(q, main, printVec3_call);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // qbe_compile(q);
    // QbeSV sv = qbe_get_compiled_program(q);
    // fwrite(sv.data, sv.count, 1, stdout);

    const char *flags[] = {
        "-L.",
        "-lvec3",
    };
    generate_executable(q, "example_var_init", flags, len(flags));
    qbe_free(q);
}

int main(void) {
    example_if();
    example_struct();
    example_cast();
    example_float();
    example_phi();
    example_while_with_debug();
    example_array();
    example_extern_var();
    example_var_init();
}
