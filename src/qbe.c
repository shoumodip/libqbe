#include "../include/qbe.h"
#include "all.h"

#include <getopt.h>

Target T;

char debug['Z' + 1] = {
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

extern Target T_amd64_sysv;
extern Target T_amd64_apple;
extern Target T_arm64;
extern Target T_arm64_apple;
extern Target T_rv64;

static FILE *outf;
static int   dbg;

static void data(Dat *d) {
    if (dbg) return;
    emitdat(d, outf);
    if (d->type == DEnd) {
        fputs("/* end data */\n\n", outf);
        freeall();
    }
}

static void func(Fn *fn) {
    uint n;

    if (dbg) fprintf(stderr, "**** Function %s ****", fn->name);
    if (debug['P']) {
        fprintf(stderr, "\n> After parsing:\n");
        printfn(fn, stderr);
    }
    T.abi0(fn);
    fillrpo(fn);
    fillpreds(fn);
    filluse(fn);
    promote(fn);
    filluse(fn);
    ssa(fn);
    filluse(fn);
    ssacheck(fn);
    fillalias(fn);
    loadopt(fn);
    filluse(fn);
    fillalias(fn);
    coalesce(fn);
    filluse(fn);
    ssacheck(fn);
    copy(fn);
    filluse(fn);
    fold(fn);
    T.abi1(fn);
    simpl(fn);
    fillpreds(fn);
    filluse(fn);
    T.isel(fn);
    fillrpo(fn);
    filllive(fn);
    fillloop(fn);
    fillcost(fn);
    spill(fn);
    rega(fn);
    fillrpo(fn);
    simpljmp(fn);
    fillpreds(fn);
    fillrpo(fn);
    assert(fn->rpo[0] == fn->start);
    for (n = 0;; n++)
        if (n == fn->nblk - 1) {
            fn->rpo[n]->link = 0;
            break;
        } else fn->rpo[n]->link = fn->rpo[n + 1];
    if (!dbg) {
        T.emitfn(fn, outf);
        fprintf(outf, "/* end function %s */\n\n", fn->name);
    } else fprintf(stderr, "\n");
    freeall();
}

static void dbgfile(char *fn) {
    emitdbgfile(fn, outf);
}

void qbeCompile(QbeTarget target, FILE *input, FILE *output) {
    switch (target) {
    case QBE_TARGET_DEFAULT:
#if defined(__APPLE__) && defined(__x86_64__)
        T = T_amd64_apple;
#elif defined(__APPLE__) && defined(__aarch64__)
        T = T_arm64_apple;
#elif defined(__x86_64__)
        T = T_amd64_sysv;
#elif defined(__aarch64__)
        T = T_arm64;
#elif defined(__riscv) && __riscv_xlen == 64
        T = T_rv64;
#else
#    error "Unknown or unsupported architecture"
#endif
        break;

    case QBE_TARGET_AMD64_SYSV:
        T = T_amd64_sysv;
        break;

    case QBE_TARGET_AMD64_APPLE:
        T = T_amd64_apple;
        break;

    case QBE_TARGET_ARM64:
        T = T_arm64;
        break;

    case QBE_TARGET_ARM64_APPLE:
        T = T_arm64_apple;
        break;

    case QBE_TARGET_RV64:
        T = T_rv64;
        break;

    default:
        assert(0 && "unreachable");
        break;
    }

    outf = output;
    parse(input, "<libqbe>", dbgfile, data, func);
    if (!dbg) T.emitfin(outf);
}
