#include <stdio.h>
#include <string.h>

#include "qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

bool debug = false;

// Executor
static void generate_executable(Qbe *q, const char *name, const char **link_flags, size_t link_flags_count) {
    if (debug) {
        printf("\n%s:\n", name);
        puts("================================================================================");
    }

    const int code = qbe_generate(q, QBE_TARGET_DEFAULT, name, link_flags, link_flags_count, debug);
    if (code) {
        fprintf(stderr, "ERROR: Generation of '%s' exited abnormally with code %d\n", name, code);
    }

    if (debug) {
        puts("================================================================================");
    }
}

static void example_var_init(void) {
    Qbe *q = qbe_new();
    {
        QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

        QbeStruct *Vec3 = qbe_struct_new(q, false);
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));
        qbe_struct_add_field(q, Vec3, qbe_type_basic(QBE_TYPE_I64));

        size_t  v_data[] = {69, 420};
        QbeType v_type = qbe_type_struct(Vec3);
        QbeVar *v = qbe_var_new(q, qbe_sv_from_cstr("v"), v_type);
        qbe_var_init_add_data(q, v, v_data, sizeof(v_data));

        QbeType p_type = qbe_type_basic(QBE_TYPE_I64);
        QbeVar *p = qbe_var_new(q, qbe_sv_from_cstr("p"), p_type);
        qbe_var_init_add_node(q, p, (QbeNode *) v);

        // QbeNode *newVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("newVec3"));
        // QbeCall *newVec3_call = qbe_call_new(q, newVec3, v_type);
        // qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 69));
        // qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 420));
        // qbe_call_add_arg(q, newVec3_call, qbe_atom_int(q, QBE_TYPE_I64, 0));
        // qbe_build_call(q, main, newVec3_call);
        // qbe_build_store(q, main, (QbeNode *) v, (QbeNode *) newVec3_call);

        QbeNode *printVec3 = qbe_atom_extern_fn(q, qbe_sv_from_cstr("printVec3"));
        QbeCall *printVec3_call = qbe_call_new(q, printVec3, qbe_type_basic(QBE_TYPE_I0));
        qbe_call_add_arg(
            q,
            printVec3_call,
            qbe_build_load(q, main, qbe_build_load(q, main, (QbeNode *) p, p_type, false), v_type, false));
        // qbe_call_add_arg(q, printVec3_call, qbe_build_load(q, main, (QbeNode *) v, v_type, false));
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

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "debug")) {
        debug = true;
    }

    example_var_init();
}
