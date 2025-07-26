#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/wait.h>
#include <unistd.h>

#include "qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

#define DA_INIT_CAP 128

#define da_free(l)                                                                                                     \
    do {                                                                                                               \
        free((l)->data);                                                                                               \
        memset((l), 0, sizeof(*(l)));                                                                                  \
    } while (0)

#define da_push(l, v)                                                                                                  \
    do {                                                                                                               \
        if ((l)->count >= (l)->capacity) {                                                                             \
            (l)->capacity = (l)->capacity == 0 ? DA_INIT_CAP : (l)->capacity * 2;                                      \
            (l)->data = realloc((l)->data, (l)->capacity * sizeof(*(l)->data));                                        \
            assert((l)->data);                                                                                         \
        }                                                                                                              \
                                                                                                                       \
        (l)->data[(l)->count++] = (v);                                                                                 \
    } while (0)

// Temporary Allocator
static char   temp_data[16 * 1000 * 1000];
static size_t temp_count;

void temp_reset(const void *p) {
    assert((const char *) p >= temp_data && (const char *) p < temp_data + temp_count);
    temp_count = (const char *) p - temp_data;
}

void *temp_alloc(size_t n) {
    assert(temp_count + n <= len(temp_data));
    char *result = &temp_data[temp_count];
    temp_count += n;
    return result;
}

char *temp_sprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    assert(n >= 0);
    char *result = temp_alloc(n + 1);

    va_start(args, fmt);
    vsnprintf(result, n + 1, fmt, args);
    va_end(args);

    return result;
}

// Executor
typedef struct {
    const char **data;
    size_t       count;
    size_t       capacity;
} Cmd;

int cmd_run(Cmd *c) {
    int proc = fork();
    if (proc < 0) {
        return -1;
    }

    if (!proc) {
        da_push(c, NULL);
        execvp(*c->data, (char *const *) c->data);
        exit(127);
    }

    c->count = 0;

    int status = 0;
    if (waitpid(proc, &status, 0) < 0) {
        return 1;
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return WEXITSTATUS(status);
}

static void generate_executable(Cmd *cmd, Qbe *q, const char *name, const char **link_flags, size_t link_flags_count) {
    int code = 0;

    const char *object = temp_sprintf("%s.o", name);
    code = qbe_generate(q, QBE_TARGET_DEFAULT, object);

    if (!code) {
        da_push(cmd, "cc");
        da_push(cmd, "-o");
        da_push(cmd, name);
        da_push(cmd, object);
        for (size_t i = 0; i < link_flags_count; i++) {
            da_push(cmd, link_flags[i]);
        }

        code = cmd_run(cmd);
        remove(object);
    }

    temp_reset(object);
    if (code) {
        fprintf(stderr, "ERROR: Generation of '%s' exited abnormally with code %d\n", name, code);
    }
}

// Examples
static void example_if(Cmd *cmd) {
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
    generate_executable(cmd, q, "example_if", NULL, 0);
    qbe_free(q);
}

static void example_struct(Cmd *cmd) {
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
    generate_executable(cmd, q, "example_struct", flags, len(flags));
    qbe_free(q);
}

static void example_cast(Cmd *cmd) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeNode *i8 = qbe_atom_int(q, QBE_TYPE_I8, 0);
        qbe_build_cast(q, main, i8, QBE_TYPE_I16, false);

        qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    }

    // Compile
    generate_executable(cmd, q, "example_cast", NULL, 0);
    qbe_free(q);
}

static void example_float(Cmd *cmd) {
    Qbe *q = qbe_new();

    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeNode *x = qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_F32), NULL);
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
    generate_executable(cmd, q, "example_float", NULL, 0);
    qbe_free(q);
}

static void example_phi(Cmd *cmd) {
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
    generate_executable(cmd, q, "example_phi", NULL, 0);
    qbe_free(q);
}

static void example_while_with_debug(Cmd *cmd) {
    Qbe *q = qbe_new();

    {
        QbeNode *i = qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_I64), NULL);

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
    generate_executable(cmd, q, "example_while_with_debug", NULL, 0);
    qbe_free(q);
}

static void example_array(Cmd *cmd) {
    Qbe *q = qbe_new();

    {
        const size_t n = 10;

        QbeNode *i = qbe_var_new(q, (QbeSV) {0}, qbe_type_basic(QBE_TYPE_I64), NULL);
        QbeNode *xs = qbe_var_new(q, (QbeSV) {0}, qbe_type_array(q, qbe_type_basic(QBE_TYPE_I64), n), NULL);

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
    generate_executable(cmd, q, "example_array", NULL, 0);
    qbe_free(q);
}

void example_extern_var(Cmd *cmd) {
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

    generate_executable(cmd, q, "example_extern_var", NULL, 0);
    qbe_free(q);
}

int main(void) {
    Cmd cmd = {0};
    example_if(&cmd);
    example_struct(&cmd);
    example_cast(&cmd);
    example_float(&cmd);
    example_phi(&cmd);
    example_while_with_debug(&cmd);
    example_array(&cmd);
    example_extern_var(&cmd);
    da_free(&cmd);
}
