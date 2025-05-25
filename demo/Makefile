LIBS = -L../lib -lqbe

.phony: all
all: main pipe

main: main.c
	cc -o $@ $< $(LIBS)

pipe: pipe.c
	cc -o $@ $< $(LIBS)
