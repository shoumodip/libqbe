#include <sys/wait.h>
#include <unistd.h>

#include "all.h"
#include "da.h"
#include "qbe.h"

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

static FILE *qbe_output;
static int   dbg;

static void data(Dat *d) {
    if (dbg) return;
    qbe_emitdat(d, qbe_output);
    if (d->type == DEnd) {
        fputs("/* end data */\n\n", qbe_output);
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
        qbe_T.emitfn(fn, qbe_output);
        fprintf(qbe_output, "/* end function %s */\n\n", fn->name);
    } else fprintf(stderr, "\n");
    qbe_freeall();
}

static void dbgfile(char *fn) {
    qbe_emitdbgfile(fn, qbe_output);
}

typedef struct {
    const char **data;
    size_t       count;
    size_t       capacity;
} Cmd;

int qbe_compile(
    Qbe *q, QbeTarget target, const char *output, const char **flags, size_t flags_count) {
    // Copyright (C) 2025 Shoumodip Kar <shoumodipkar@gmail.com>

    // Permission is hereby granted, free of charge, to any person obtaining a copy of
    // this software and associated documentation files (the "Software"), to deal in
    // the Software without restriction, including without limitation the rights to
    // use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
    // of the Software, and to permit persons to whom the Software is furnished to do
    // so, subject to the following conditions:

    // The above copyright notice and this permission notice shall be included in all
    // copies or substantial portions of the Software.

    // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    // SOFTWARE.

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

    {
        QbeSB program = q->sb;

        q->sb = (QbeSB) {0};
        qbe_emit_structs(q);
        da_push_many(&q->sb, program.data, program.count);

        da_free(&program);
    }

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return 1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        return 1;
    }

    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        Cmd cmd = {0};
        da_push(&cmd, "cc");
        da_push(&cmd, "-o");
        da_push(&cmd, output);
        da_push(&cmd, "-x");
        da_push(&cmd, "assembler");
        da_push(&cmd, "-");
        for (size_t i = 0; i < flags_count; i++) {
            da_push(&cmd, flags[i]);
        }
        da_push(&cmd, NULL);

        execvp(*cmd.data, (char *const *) cmd.data);
        exit(127);
    }

    qbe_output = fdopen(pipefd[1], "w");
    if (!qbe_output) {
        return 1;
    }
    close(pipefd[0]);

    FILE *qbe_input = fmemopen(q->sb.data, q->sb.count, "r");
    if (!qbe_input) {
        return 1;
    }

    qbe_parse(qbe_input, "<libqbe>", dbgfile, data, func);
    if (!dbg) {
        qbe_T.emitfin(qbe_output);
    }

    fclose(qbe_input);
    fclose(qbe_output);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return WEXITSTATUS(status);
}
