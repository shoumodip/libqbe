#include "qbe.h"
#include "all.h"

#include <getopt.h>

Target qbe_T;

char qbe_debug['Z' + 1] = {
    ['P'] = 0, /* parsing */
    ['M'] = 0, /* memory optimization */
    ['N'] = 0, /* ssa construction */
    ['C'] = 0, /* copy elimination */
    ['F'] = 0, /* constant folding */
    ['A'] = 0, /* abi lowering */
    ['I'] = 0, /* instruction selection */
    ['L'] = 0, /* liveness */
    ['S'] = 0, /* spilling */
    ['R'] = 0, /* reg. allocation */
};

extern Target qbe_T_amd64_sysv;
extern Target qbe_T_amd64_apple;
extern Target qbe_T_arm64;
extern Target qbe_T_arm64_apple;
extern Target qbe_T_rv64;

static FILE *outf;
static int   dbg;

static void data(Dat *d) {
    if (dbg) return;
    qbe_emitdat(d, outf);
    if (d->type == DEnd) {
        fputs("/* end data */\n\n", outf);
        qbe_freeall();
    }
}

static void func(Fn *fn) {
    uint n;

    if (dbg) fprintf(stderr, "**** Function %s ****", fn->name);
    if (qbe_debug['P']) {
        fprintf(stderr, "\n> After parsing:\n");
        qbe_printfn(fn, stderr);
    }
    qbe_T.abi0(fn);
    qbe_fillrpo(fn);
    qbe_fillpreds(fn);
    qbe_filluse(fn);
    qbe_promote(fn);
    qbe_filluse(fn);
    qbe_ssa(fn);
    qbe_filluse(fn);
    qbe_ssacheck(fn);
    qbe_fillalias(fn);
    qbe_loadopt(fn);
    qbe_filluse(fn);
    qbe_fillalias(fn);
    qbe_coalesce(fn);
    qbe_filluse(fn);
    qbe_ssacheck(fn);
    qbe_copy(fn);
    qbe_filluse(fn);
    qbe_fold(fn);
    qbe_T.abi1(fn);
    qbe_simpl(fn);
    qbe_fillpreds(fn);
    qbe_filluse(fn);
    qbe_T.isel(fn);
    qbe_fillrpo(fn);
    qbe_filllive(fn);
    qbe_fillloop(fn);
    qbe_fillcost(fn);
    qbe_spill(fn);
    qbe_rega(fn);
    qbe_fillrpo(fn);
    qbe_simpljmp(fn);
    qbe_fillpreds(fn);
    qbe_fillrpo(fn);
    assert(fn->rpo[0] == fn->start);
    for (n = 0;; n++)
        if (n == fn->nblk - 1) {
            fn->rpo[n]->link = 0;
            break;
        } else fn->rpo[n]->link = fn->rpo[n + 1];
    if (!dbg) {
        qbe_T.emitfn(fn, outf);
        fprintf(outf, "/* end function %s */\n\n", fn->name);
    } else fprintf(stderr, "\n");
    qbe_freeall();
}

static void dbgfile(char *fn) {
    qbe_emitdbgfile(fn, outf);
}

void qbe_compile(QbeTarget target, FILE *input, FILE *output) {
    if (target == QBE_TARGET_DEFAULT) {
#if defined(__APPLE__) && defined(__x86_64__)
        target = QBE_TARGET_X86_64_APPLE;
#elif defined(__APPLE__) && defined(__aarch64__)
        target = QBE_TARGET_ARM64_APPLE;
#elif defined(__x86_64__)
        target = QBE_TARGET_X86_64_SYSV;
#elif defined(__aarch64__)
        target = QBE_TARGET_ARM64;
#elif defined(__riscv) && __riscv_xlen == 64
        target = QBE_TARGET_RV64;
#else
#    error "Unknown or unsupported architecture"
#endif
    }

    switch (target) {
    case QBE_TARGET_X86_64_SYSV:
        qbe_T = qbe_T_amd64_sysv;
        break;

    case QBE_TARGET_X86_64_APPLE:
        qbe_T = qbe_T_amd64_apple;
        break;

    case QBE_TARGET_ARM64:
        qbe_T = qbe_T_arm64;
        break;

    case QBE_TARGET_ARM64_APPLE:
        qbe_T = qbe_T_arm64_apple;
        break;

    case QBE_TARGET_RV64:
        qbe_T = qbe_T_rv64;
        break;

    default:
        assert(0 && "unreachable");
        break;
    }

    outf = output;
    qbe_parse(input, "<libqbe>", dbgfile, data, func);
    if (!dbg) qbe_T.emitfin(outf);
}
