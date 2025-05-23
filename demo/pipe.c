#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../include/qbe.h"

char *program = //
    "data $msg = align 1 { b \"Hello, world!\", b 0 }\n"
    "export function w $main() {\n"
    "@start\n"
    "	%.1 =w call $puts(l $msg)\n"
    "	ret 0\n"
    "}\n";

int main(void) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        execlp("cc", "cc", "-o", "hello", "-x", "assembler", "-", NULL);
        perror("exec");
        exit(1);
    }

    close(pipefd[0]);
    FILE *ccInput = fdopen(pipefd[1], "w");
    if (!ccInput) {
        perror("fdopen");
        exit(1);
    }

    FILE *qbeInput = fmemopen(program, strlen(program), "r");
    if (!qbeInput) {
        perror("fmemopen");
        exit(1);
    }

    qbeCompile(QBE_TARGET_DEFAULT, qbeInput, ccInput);
    fclose(qbeInput);
    fclose(ccInput);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        exit(1);
    }

    if (WIFSIGNALED(status)) {
        exit(128 + WTERMSIG(status));
    }

    exit(WEXITSTATUS(status));
}
