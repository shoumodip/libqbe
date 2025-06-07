#include <stdio.h>

long i = 0;

int main(void) {
    while (i < 10) {
        printf("%ld", i);
        i = i + 1;
    }

    return 0;
}

// This file is strictly here to demonstrate the (limited) debug capabilities of QBE
