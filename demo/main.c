#include <stdio.h>
#include <string.h>

#include "../include/qbe.h"

char *input = //
    "data $msg = align 1 { b \"Hello, world!\", b 0 }\n"
    "export function w $main() {\n"
    "@start\n"
    "	%.1 =w call $puts(l $msg)\n"
    "	ret 0\n"
    "}\n";

int main(void) {
    FILE *f = fmemopen(input, strlen(input), "r");
    if (!f) {
        perror("fmemopen");
    }

    qbe_compile(QBE_TARGET_DEFAULT, f, stdout);
    fclose(f);
}
