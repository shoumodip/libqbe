#!/bin/sh

set -e

CFLAGS=-std=c99
SRCDIR=$PWD/src

mkdir -p lib

cd $SRCDIR
cc $CFLAGS -c *.c

cd $SRCDIR/amd64
cc $CFLAGS -c *.c

cd $SRCDIR/arm64
cc $CFLAGS -c *.c

cd $SRCDIR/rv64
cc $CFLAGS -c *.c

cd $SRCDIR/..
ar rcs lib/libqbe.a src/*.o src/amd64/*.o src/arm64/*.o src/rv64/*.o
