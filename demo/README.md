# Quick Start
```console
$ ./build.sh
$ ./main > hello.s
$ cc -o hello hello.s
$ ./hello
```

## Without Assembly Artifacts
```console
$ ./main | cc -o hello -x assembler -
$ ./hello
```

The pipe can be directly created in the "compiler" itself. See [pipe.c](pipe.c).

```console
$ ./pipe
$ ./hello
```
