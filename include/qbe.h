#ifndef QBE_H
#define QBE_H

#include <stdio.h>

typedef enum {
    QBE_TARGET_DEFAULT,
    QBE_TARGET_X86_64_SYSV,
    QBE_TARGET_X86_64_APPLE,
    QBE_TARGET_ARM64,
    QBE_TARGET_ARM64_APPLE,
    QBE_TARGET_RV64
} QbeTarget;

void qbe_compile(QbeTarget target, FILE *input, FILE *output);

#endif // QBE_H
