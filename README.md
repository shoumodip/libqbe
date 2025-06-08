# LibQBE
[QBE](https://c9x.me/compile/) as a C library.

## Disclaimer
This has only been tested on x86_64 Linux. Theoretically it should also work on
ARM64 Linux as well as on MacOS, however I cannot verify that. It seems Github
CI seems to use the same CPU irrespective of whether x86_64 or ARM64 is
selected, so the passing builds may or may not mean anything. I am thus
shutting down the CI until Github gets their act together and actually uses the
proper CPU as specified in the runner.

## Quick Start
```console
$ make
```

See the provided [demo/](demo/) for usage details.

## Why?
- The original QBE is a CLI application, which complicates distribution.
- LLVM does not generate C ABI compatible functions (It had one job :expressionless:).
