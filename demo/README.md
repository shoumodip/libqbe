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
```
