#include "../include/qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

int main(void) {
    Qbe qbe = {0};

    QbeValue i = qbe_emit_var(&qbe, qbe_sv_from_cstr(""), qbe_type_basic(QBE_TYPE_I64));

    qbe_emit_func(&qbe, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32), NULL, 0);

    QbeBlock cond_block = qbe_new_block(&qbe);
    QbeBlock body_block = qbe_new_block(&qbe);
    QbeBlock break_block = qbe_new_block(&qbe);

    // Condition
    qbe_emit_block(&qbe, cond_block);

    QbeValue cond = qbe_emit_binary(
        &qbe,
        QBE_BINARY_SLT,
        qbe_type_basic(QBE_TYPE_I32),
        qbe_emit_load(&qbe, i, qbe_type_basic(QBE_TYPE_I64)),
        qbe_value_int(QBE_TYPE_I64, 10));

    qbe_emit_branch(&qbe, cond, body_block, break_block);

    // Body
    qbe_emit_block(&qbe, body_block);

    QbeValue args[] = {
        qbe_emit_str(&qbe, qbe_sv_from_cstr("%ld\n")),
        qbe_emit_load(&qbe, i, qbe_type_basic(QBE_TYPE_I64)),
    };

    QbeValue log = qbe_value_import(qbe_sv_from_cstr("printf"), qbe_type_basic(QBE_TYPE_PTR));
    qbe_emit_call(&qbe, log, qbe_type_basic(QBE_TYPE_I32), args, len(args));

    QbeValue i1 = qbe_emit_load(&qbe, i, qbe_type_basic(QBE_TYPE_I64));
    QbeValue i2 = qbe_emit_binary(
        &qbe, QBE_BINARY_ADD, qbe_type_basic(QBE_TYPE_I64), i1, qbe_value_int(QBE_TYPE_I64, 1));

    qbe_emit_store(&qbe, i, i2);
    qbe_emit_jump(&qbe, cond_block);

    // Break
    qbe_emit_block(&qbe, break_block);

    QbeValue zero = qbe_value_int(QBE_TYPE_I32, 0);
    qbe_emit_return(&qbe, &zero);
    qbe_emit_func_end(&qbe);

    return qbe_compile(&qbe, QBE_TARGET_DEFAULT, "hello", NULL, 0);
}
