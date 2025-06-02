#include <stdio.h>

typedef struct {
    long x;
    long y;
    long z;
} Vec3;

Vec3 newVec3(long x, long y, long z) {
    return (Vec3) {
        .x = x,
        .y = y,
        .z = z,
    };
}

Vec3 addVec3(Vec3 a, Vec3 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

void printVec3(Vec3 v) {
    printf("(%ld, %ld, %ld)\n", v.x, v.y, v.z);
}
