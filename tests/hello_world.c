#include "qbe.h"

int main(void) {
    Qbe   *q = qbe_new();
    QbeFn *main = qbe_fn_new(q, qbe_sv_from_cstr("main"), qbe_type_basic(QBE_TYPE_I32));

    QbeNode *puts = qbe_atom_symbol(q, qbe_sv_from_cstr("puts"), qbe_type_basic(QBE_TYPE_I64));
    QbeCall *call = qbe_build_call(q, main, puts, qbe_type_basic(QBE_TYPE_I32));
    qbe_call_add_arg(q, call, qbe_str_new(q, qbe_sv_from_cstr("Hello, world!")));

    qbe_build_return(q, main, qbe_atom_int(q, QBE_TYPE_I32, 0));
    return qbe_generate(q, QBE_TARGET_DEFAULT, "hello_world.exe", NULL, 0);
}
