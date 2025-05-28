#include "all.h"

enum Imm {
	Iother,
	Iplo12,
	Iphi12,
	Iplo24,
	Inlo12,
	Inhi12,
	Inlo24
};

static enum Imm
imm(Con *c, int k, int64_t *pn)
{
	int64_t n;
	int i;

	if (c->type != CBits)
		return Iother;
	n = c->bits.i;
	if (k == Kw)
		n = (int32_t)n;
	i = Iplo12;
	if (n < 0) {
		i = Inlo12;
		n = -n;
	}
	*pn = n;
	if ((n & 0x000fff) == n)
		return i;
	if ((n & 0xfff000) == n)
		return i + 1;
	if ((n & 0xffffff) == n)
		return i + 2;
	return Iother;
}

int
qbe_arm64_logimm(uint64_t x, int k)
{
	uint64_t n;

	if (k == Kw)
		x = (x & 0xffffffff) | x << 32;
	if (x & 1)
		x = ~x;
	if (x == 0)
		return 0;
	if (x == 0xaaaaaaaaaaaaaaaa)
		return 1;
	n = x & 0xf;
	if (0x1111111111111111 * n == x)
		goto Check;
	n = x & 0xff;
	if (0x0101010101010101 * n == x)
		goto Check;
	n = x & 0xffff;
	if (0x0001000100010001 * n == x)
		goto Check;
	n = x & 0xffffffff;
	if (0x0000000100000001 * n == x)
		goto Check;
	n = x;
Check:
	return (n & (n + (n & -n))) == 0;
}

static void
fixarg(Ref *pr, int k, int phi, Fn *fn)
{
	char buf[32];
	Con *c, cc;
	Ref r0, r1, r2, r3;
	int s, n;

	r0 = *pr;
	switch (rtype(r0)) {
	case RCon:
		c = &fn->con[r0.val];
		if (qbe_T.apple
		&& c->type == CAddr
		&& c->sym.type == SThr) {
			r1 = qbe_newtmp("isel", Kl, fn);
			*pr = r1;
			if (c->bits.i) {
				r2 = qbe_newtmp("isel", Kl, fn);
				cc = (Con){.type = CBits};
				cc.bits.i = c->bits.i;
				r3 = qbe_newcon(&cc, fn);
				qbe_emit(Oadd, Kl, r1, r2, r3);
				r1 = r2;
			}
			qbe_emit(Ocopy, Kl, r1, TMP(R0), R);
			r1 = qbe_newtmp("isel", Kl, fn);
			r2 = qbe_newtmp("isel", Kl, fn);
			qbe_emit(Ocall, 0, R, r1, CALL(33));
			qbe_emit(Ocopy, Kl, TMP(R0), r2, R);
			qbe_emit(Oload, Kl, r1, r2, R);
			cc = *c;
			cc.bits.i = 0;
			r3 = qbe_newcon(&cc, fn);
			qbe_emit(Ocopy, Kl, r2, r3, R);
			break;
		}
		if (KBASE(k) == 0 && phi)
			return;
		r1 = qbe_newtmp("isel", k, fn);
		if (KBASE(k) == 0) {
			qbe_emit(Ocopy, k, r1, r0, R);
		} else {
			n = qbe_stashbits(&c->bits, KWIDE(k) ? 8 : 4);
			qbe_vgrow(&fn->con, ++fn->ncon);
			c = &fn->con[fn->ncon-1];
			sprintf(buf, "\"%sfp%d\"", qbe_T.asloc, n);
			*c = (Con){.type = CAddr};
			c->sym.id = qbe_intern(buf);
			r2 = qbe_newtmp("isel", Kl, fn);
			qbe_emit(Oload, k, r1, r2, R);
			qbe_emit(Ocopy, Kl, r2, CON(c-fn->con), R);
		}
		*pr = r1;
		break;
	case RTmp:
		s = fn->tmp[r0.val].slot;
		if (s == -1)
			break;
		r1 = qbe_newtmp("isel", Kl, fn);
		qbe_emit(Oaddr, Kl, r1, SLOT(s), R);
		*pr = r1;
		break;
	}
}

static int
selcmp(Ref arg[2], int k, Fn *fn)
{
	Ref r, *iarg;
	Con *c;
	int swap, cmp, fix;
	int64_t n;

	if (KBASE(k) == 1) {
		qbe_emit(Oafcmp, k, R, arg[0], arg[1]);
		iarg = qbe_curi->arg;
		fixarg(&iarg[0], k, 0, fn);
		fixarg(&iarg[1], k, 0, fn);
		return 0;
	}
	swap = rtype(arg[0]) == RCon;
	if (swap) {
		r = arg[1];
		arg[1] = arg[0];
		arg[0] = r;
	}
	fix = 1;
	cmp = Oacmp;
	r = arg[1];
	if (rtype(r) == RCon) {
		c = &fn->con[r.val];
		switch (imm(c, k, &n)) {
		default:
			break;
		case Iplo12:
		case Iphi12:
			fix = 0;
			break;
		case Inlo12:
		case Inhi12:
			cmp = Oacmn;
			r = qbe_getcon(n, fn);
			fix = 0;
			break;
		}
	}
	qbe_emit(cmp, k, R, arg[0], r);
	iarg = qbe_curi->arg;
	fixarg(&iarg[0], k, 0, fn);
	if (fix)
		fixarg(&iarg[1], k, 0, fn);
	return swap;
}

static int
callable(Ref r, Fn *fn)
{
	Con *c;

	if (rtype(r) == RTmp)
		return 1;
	if (rtype(r) == RCon) {
		c = &fn->con[r.val];
		if (c->type == CAddr)
		if (c->bits.i == 0)
			return 1;
	}
	return 0;
}

static void
sel(Ins i, Fn *fn)
{
	Ref *iarg;
	Ins *i0;
	int ck, cc;

	if (INRANGE(i.op, Oalloc, Oalloc1)) {
		i0 = qbe_curi - 1;
		qbe_salloc(i.to, i.arg[0], fn);
		fixarg(&i0->arg[0], Kl, 0, fn);
		return;
	}
	if (qbe_iscmp(i.op, &ck, &cc)) {
		qbe_emit(Oflag, i.cls, i.to, R, R);
		i0 = qbe_curi;
		if (selcmp(i.arg, ck, fn))
			i0->op += qbe_cmpop(cc);
		else
			i0->op += cc;
		return;
	}
	if (i.op == Ocall)
	if (callable(i.arg[0], fn)) {
		qbe_emiti(i);
		return;
	}
	if (i.op != Onop) {
		qbe_emiti(i);
		iarg = qbe_curi->arg; /* fixarg() can change curi */
		fixarg(&iarg[0], qbe_argcls(&i, 0), 0, fn);
		fixarg(&iarg[1], qbe_argcls(&i, 1), 0, fn);
	}
}

static void
seljmp(Blk *b, Fn *fn)
{
	Ref r;
	Ins *i, *ir;
	int ck, cc, use;

	if (b->jmp.type == Jret0
	|| b->jmp.type == Jjmp
	|| b->jmp.type == Jhlt)
		return;
	assert(b->jmp.type == Jjnz);
	r = b->jmp.arg;
	use = -1;
	b->jmp.arg = R;
	ir = 0;
	i = &b->ins[b->nins];
	while (i > b->ins)
		if (req((--i)->to, r)) {
			use = fn->tmp[r.val].nuse;
			ir = i;
			break;
		}
	if (ir && use == 1
	&& qbe_iscmp(ir->op, &ck, &cc)) {
		if (selcmp(ir->arg, ck, fn))
			cc = qbe_cmpop(cc);
		b->jmp.type = Jjf + cc;
		*ir = (Ins){.op = Onop};
	}
	else {
		selcmp((Ref[]){r, CON_Z}, Kw, fn);
		b->jmp.type = Jjfine;
	}
}

void
qbe_arm64_isel(Fn *fn)
{
	Blk *b, **sb;
	Ins *i;
	Phi *p;
	uint n, al;
	int64_t sz;

	/* assign slots to fast allocs */
	b = fn->start;
	/* specific to NAlign == 3 */ /* or change n=4 and sz /= 4 below */
	for (al=Oalloc, n=4; al<=Oalloc1; al++, n*=2)
		for (i=b->ins; i<&b->ins[b->nins]; i++)
			if (i->op == al) {
				if (rtype(i->arg[0]) != RCon)
					break;
				sz = fn->con[i->arg[0].val].bits.i;
				if (sz < 0 || sz >= INT_MAX-15)
					qbe_err("invalid alloc size %"PRId64, sz);
				sz = (sz + n-1) & -n;
				sz /= 4;
				fn->tmp[i->to.val].slot = fn->slot;
				fn->slot += sz;
				*i = (Ins){.op = Onop};
			}

	for (b=fn->start; b; b=b->link) {
		qbe_curi = &qbe_insb[NIns];
		for (sb=(Blk*[3]){b->s1, b->s2, 0}; *sb; sb++)
			for (p=(*sb)->phi; p; p=p->link) {
				for (n=0; p->blk[n] != b; n++)
					assert(n+1 < p->narg);
				fixarg(&p->arg[n], p->cls, 1, fn);
			}
		seljmp(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;)
			sel(*--i, fn);
		b->nins = &qbe_insb[NIns] - qbe_curi;
		qbe_idup(&b->ins, qbe_curi, b->nins);
	}

	if (qbe_debug['I']) {
		fprintf(stderr, "\n> After instruction selection:\n");
		qbe_printfn(fn, stderr);
	}
}
