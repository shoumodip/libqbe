:i count 3
:b shell 60
cc -I../include/ -o hello_world hello_world.c -L../lib -lqbe
:i returncode 0
:b stdout 0

:b stderr 0

:b shell 13
./hello_world
:i returncode 0
:b stdout 0

:b stderr 0

:b shell 17
./hello_world.exe
:i returncode 0
:b stdout 14
Hello, world!

:b stderr 0

