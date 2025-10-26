.phony: all
all: main example_var_init_builder

main: main.c libvec3.a ../lib/libqbe.a
	cc -I../include -g -o main main.c -L../lib -lqbe

example_var_init_builder: example_var_init_builder.c libvec3.a ../lib/libqbe.a
	cc -I../include -g -o example_var_init_builder example_var_init_builder.c -L../lib -lqbe

libvec3.a: vec3.c
	cc -c vec3.c
	ar rcs libvec3.a vec3.o
	rm vec3.o
