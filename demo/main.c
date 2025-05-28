#include "../include/qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

int main(void) {
    Qbe qbe = {0};

    qbe_emit_func(&qbe, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32), NULL, 0);

    QbeValue func = qbe_value_import(qbe_sv_from_cstr("puts"), qbe_type_basic(QBE_TYPE_PTR));
    QbeBlock then_block = qbe_new_block(&qbe);
    QbeBlock else_block = qbe_new_block(&qbe);
    QbeBlock merge_block = qbe_new_block(&qbe);

    // Condition
    QbeValue cond = qbe_emit_binary(
        &qbe,
        QBE_BINARY_SGE,
        qbe_type_basic(QBE_TYPE_I32),
        qbe_value_int(QBE_TYPE_I64, 2),
        qbe_value_int(QBE_TYPE_I64, 2));

    qbe_emit_branch(&qbe, cond, then_block, else_block);

    // Then
    qbe_emit_block(&qbe, then_block);
    QbeValue first = qbe_emit_str(&qbe, qbe_sv_from_cstr("First"));
    qbe_emit_call(&qbe, func, qbe_type_basic(QBE_TYPE_I32), &first, 1);
    qbe_emit_jump(&qbe, merge_block);

    // Else
    qbe_emit_block(&qbe, else_block);
    QbeValue second = qbe_emit_str(&qbe, qbe_sv_from_cstr("Second"));
    qbe_emit_call(&qbe, func, qbe_type_basic(QBE_TYPE_I32), &second, 1);
    qbe_emit_jump(&qbe, merge_block);

    // Merge
    qbe_emit_block(&qbe, merge_block);

    QbeValue zero = qbe_value_int(QBE_TYPE_I32, 0);
    qbe_emit_return(&qbe, &zero);
    qbe_emit_func_end(&qbe);

    // #include <stdio.h>
    //     fwrite(qbe.sb.data, qbe.sb.count, 1, stdout);

    return qbe_compile(&qbe, QBE_TARGET_DEFAULT, "hello", NULL, 0);
}
