#include "all.h"

enum {
	SecText,
	SecData,
	SecBss,
};

static uint curfile; // @shoumodip

void
qbe_emitlnk(char *n, uint linenr, Lnk *l, int s, FILE *f) // @shoumodip
{
	static char *sec[2][3] = {
		[0][SecText] = ".text",
		[0][SecData] = ".data",
		[0][SecBss] = ".bss",
		[1][SecText] = ".abort \"unreachable\"",
		[1][SecData] = ".section .tdata,\"awT\"",
		[1][SecBss] = ".section .tbss,\"awT\"",
	};
	char *pfx, *sfx;

	pfx = n[0] == '"' ? "" : qbe_T.assym;
	sfx = "";
	if (qbe_T.apple && l->thread) {
		l->sec = "__DATA";
		l->secf = "__thread_data,thread_local_regular";
		sfx = "$tlv$init";
		fputs(
			".section __DATA,__thread_vars,"
			"thread_local_variables\n",
			f
		);
		fprintf(f, "%s%s:\n", pfx, n);
		fprintf(f,
			"\t.quad __tlv_bootstrap\n"
			"\t.quad 0\n"
			"\t.quad %s%s%s\n\n",
			pfx, n, sfx
		);
	}
	if (l->sec) {
		fprintf(f, ".section %s", l->sec);
		if (l->secf)
			fprintf(f, ",%s", l->secf);
	} else
		fputs(sec[l->thread != 0][s], f);
	fputc('\n', f);
	if (l->align)
		fprintf(f, ".balign %d\n", l->align);
	if (l->export)
		fprintf(f, ".globl %s%s\n", pfx, n);
	fprintf(f, "%s%s%s:\n", pfx, n, sfx);

	// @shoumodip
	if (curfile && linenr) {
		fprintf(f, "    .loc %u %u\n", curfile, linenr);
	}
}

void
qbe_emitfnlnk(char *n, uint linenr, Lnk *l, FILE *f) // @shoumodip
{
	qbe_emitlnk(n, linenr, l, SecText, f); // @shoumodip
}

void
qbe_emitdat(Dat *d, FILE *f)
{
	static char *dtoa[] = {
		[DB] = "\t.byte",
		[DH] = "\t.short",
		[DW] = "\t.int",
		[DL] = "\t.quad"
	};
	static int64_t zero;
	char *p;

	switch (d->type) {
	case DStart:
		zero = 0;
		break;
	case DEnd:
		if (zero != -1) {
			qbe_emitlnk(d->name, 0, d->lnk, SecBss, f); // @shoumodip
			fprintf(f, "\t.fill %"PRId64",1,0\n", zero);
		}
		break;
	case DZ:
		if (zero != -1)
			zero += d->u.num;
		else
			fprintf(f, "\t.fill %"PRId64",1,0\n", d->u.num);
		break;
	default:
		if (zero != -1) {
			qbe_emitlnk(d->name, 0, d->lnk, SecData, f); // @shoumodip
			if (zero > 0)
				fprintf(f, "\t.fill %"PRId64",1,0\n", zero);
			zero = -1;
		}
		if (d->isstr) {
			if (d->type != DB)
				qbe_err("strings only supported for 'b' currently");
			fprintf(f, "\t.ascii %s\n", d->u.str);
		}
		else if (d->isref) {
			p = d->u.ref.name[0] == '"' ? "" : qbe_T.assym;
			fprintf(f, "%s %s%s%+"PRId64"\n",
				dtoa[d->type], p, d->u.ref.name,
				d->u.ref.off);
		}
		else {
			fprintf(f, "%s %"PRId64"\n",
				dtoa[d->type], d->u.num);
		}
		break;
	}
}

typedef struct Asmbits Asmbits;

struct Asmbits {
	char bits[16];
	int size;
	Asmbits *link;
};

static Asmbits *stash;

int
qbe_stashbits(void *bits, int size)
{
	Asmbits **pb, *b;
	int i;

	assert(size == 4 || size == 8 || size == 16);
	for (pb=&stash, i=0; (b=*pb); pb=&b->link, i++)
		if (size <= b->size)
		if (memcmp(bits, b->bits, size) == 0)
			return i;
	b = qbe_emalloc(sizeof *b);
	memcpy(b->bits, bits, size);
	b->size = size;
	b->link = 0;
	*pb = b;
	return i;
}

static void
emitfin(FILE *f, char *sec[3])
{
	Asmbits *b;
	char *p;
	int lg, i;
	double d;

	if (!stash)
		return;
	fprintf(f, "/* floating point constants */\n");
	for (lg=4; lg>=2; lg--)
		for (b=stash, i=0; b; b=b->link, i++) {
			if (b->size == (1<<lg)) {
				fprintf(f,
					".section %s\n"
					".p2align %d\n"
					"%sfp%d:",
					sec[lg-2], lg, qbe_T.asloc, i
				);
				for (p=b->bits; p<&b->bits[b->size]; p+=4)
					fprintf(f, "\n\t.int %"PRId32,
						*(int32_t *)p);
				if (lg <= 3) {
					if (lg == 2)
						d = *(float *)b->bits;
					else
						d = *(double *)b->bits;
					fprintf(f, " /* %f */\n\n", d);
				} else
					fprintf(f, "\n\n");
			}
		}
	while ((b=stash)) {
		stash = b->link;
		free(b);
	}
}

void
qbe_elf_emitfin(FILE *f)
{
	static char *sec[3] = { ".rodata", ".rodata", ".rodata" };

	emitfin(f ,sec);
	fprintf(f, ".section .note.GNU-stack,\"\",@progbits\n");
}

void
qbe_elf_emitfnfin(char *fn, FILE *f)
{
	fprintf(f, ".type %s, @function\n", fn);
	fprintf(f, ".size %s, .-%s\n", fn, fn);
}

void
qbe_macho_emitfin(FILE *f)
{
	static char *sec[3] = {
		"__TEXT,__literal4,4byte_literals",
		"__TEXT,__literal8,8byte_literals",
		".abort \"unreachable\"",
	};

	emitfin(f, sec);
}

static uint32_t *file;
static uint nfile;

// Modification BEGIN
// Copyright (C) 2025 Shoumodip Kar <shoumodipkar@gmail.com>
void
qbe_emit_resetall(void)
{
    if (file) {
        qbe_vfree(file);
        file = NULL;
    }
    nfile = 0;
    curfile = 0;
}
// Modification END

void
qbe_emitdbgfile(char *fn, FILE *f)
{
	uint32_t id;
	uint n;

	id = qbe_intern(fn);
	for (n=0; n<nfile; n++)
		if (file[n] == id) {
			/* gas requires positive
			 * file numbers */
			curfile = n + 1;
			return;
		}
	if (!file)
		file = qbe_vnew(0, sizeof *file, PHeap);
	qbe_vgrow(&file, ++nfile);
	file[nfile-1] = id;
	curfile = nfile;
	fprintf(f, ".file %u %s\n", curfile, fn);
}

void
qbe_emitdbgloc(uint line, uint col, FILE *f)
{
	if (col != 0)
		fprintf(f, "\t.loc %u %u %u\n", curfile, line, col);
	else
		fprintf(f, "\t.loc %u %u\n", curfile, line);
}
