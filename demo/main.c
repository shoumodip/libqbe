#include "../include/qbe.h"

#define len(a) (sizeof(a) / sizeof(*(a)))

int main(void) {
    Qbe qbe = {0};

    QbeValue message = qbe_emit_str(&qbe, qbe_sv_from_cstr("%ld\n"));
    qbe_emit_func(&qbe, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32), NULL, 0);

    QbeValue func = qbe_value_import(qbe_sv_from_cstr("printf"), qbe_type_basic(QBE_TYPE_PTR));
    QbeValue args[] = {
        message,
        qbe_emit_binary(
            &qbe,
            QBE_BINARY_SGE,
            qbe_type_basic(QBE_TYPE_I64),
            qbe_value_int(QBE_TYPE_I64, 2),
            qbe_value_int(QBE_TYPE_I64, 3)),
    };
    qbe_emit_call(&qbe, func, qbe_type_basic(QBE_TYPE_I32), args, len(args));

    QbeValue zero = qbe_value_int(QBE_TYPE_I32, 0);
    qbe_emit_return(&qbe, &zero);
    qbe_emit_func_end(&qbe);

    // #include <stdio.h>
    //     fwrite(qbe.sb.data, qbe.sb.count, 1, stdout);

    return qbe_compile(&qbe, QBE_TARGET_DEFAULT, "hello", NULL, 0);
}
