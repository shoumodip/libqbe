#ifndef QBE_H
#define QBE_H

#include <stdio.h>

typedef enum {
    QBE_TARGET_DEFAULT,
    QBE_TARGET_AMD64_SYSV,
    QBE_TARGET_AMD64_APPLE,
    QBE_TARGET_ARM64,
    QBE_TARGET_ARM64_APPLE,
    QBE_TARGET_RV64
} QbeTarget;

void qbeCompile(QbeTarget target, FILE *input, FILE *output);

#endif // QBE_H
