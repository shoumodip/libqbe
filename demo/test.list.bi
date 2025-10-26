:i count 10
:b shell 6
./main
:i returncode 0
:b stdout 0

:b stderr 0

:b shell 12
./example_if
:i returncode 0
:b stdout 6
First

:b stderr 0

:b shell 14
./example_cast
:i returncode 0
:b stdout 0

:b stderr 0

:b shell 15
./example_float
:i returncode 0
:b stdout 7
420.69

:b stderr 0

:b shell 13
./example_phi
:i returncode 0
:b stdout 3
69

:b stderr 0

:b shell 16
./example_struct
:i returncode 0
:b stdout 16
(69, 420, 1337)

:b stderr 0

:b shell 26
./example_while_with_debug
:i returncode 0
:b stdout 20
0
1
2
3
4
5
6
7
8
9

:b stderr 0

:b shell 15
./example_array
:i returncode 0
:b stdout 25
0
2
4
6
8
10
12
14
16
18

:b stderr 0

:b shell 20
./example_extern_var
:i returncode 0
:b stdout 0

:b stderr 14
Hello, world!

:b shell 18
./example_var_init
:i returncode 0
:b stdout 13
(69, 420, 0)

:b stderr 0

