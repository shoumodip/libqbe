#include "all.h"

typedef struct AClass AClass;
typedef struct RAlloc RAlloc;

struct AClass {
	Typ *type;
	int inmem;
	int align;
	uint size;
	int cls[2];
	Ref ref[2];
};

struct RAlloc {
	Ins i;
	RAlloc *link;
};

static void
classify(AClass *a, Typ *t, uint s)
{
	Field *f;
	int *cls;
	uint n, s1;

	for (n=0, s1=s; n<t->nunion; n++, s=s1)
		for (f=t->fields[n]; f->type!=FEnd; f++) {
			assert(s <= 16);
			cls = &a->cls[s/8];
			switch (f->type) {
			case FEnd:
				die("unreachable");
			case FPad:
				/* don't change anything */
				s += f->len;
				break;
			case Fs:
			case Fd:
				if (*cls == Kx)
					*cls = Kd;
				s += f->len;
				break;
			case Fb:
			case Fh:
			case Fw:
			case Fl:
				*cls = Kl;
				s += f->len;
				break;
			case FTyp:
				classify(a, &qbe_typ[f->len], s);
				s += qbe_typ[f->len].size;
				break;
			}
		}
}

static void
typclass(AClass *a, Typ *t)
{
	uint sz, al;

	sz = t->size;
	al = 1u << t->align;

	/* the ABI requires sizes to be rounded
	 * up to the nearest multiple of 8, moreover
	 * it makes it easy load and store structures
	 * in registers
	 */
	if (al < 8)
		al = 8;
	sz = (sz + al-1) & -al;

	a->type = t;
	a->size = sz;
	a->align = t->align;

	if (t->isdark || sz > 16 || sz == 0) {
		/* large or unaligned structures are
		 * required to be passed in memory
		 */
		a->inmem = 1;
		return;
	}

	a->cls[0] = Kx;
	a->cls[1] = Kx;
	a->inmem = 0;
	classify(a, t, 0);
}

static int
retr(Ref reg[2], AClass *aret)
{
	static int retreg[2][2] = {{RAX, RDX}, {XMM0, XMM0+1}};
	int n, k, ca, nr[2];

	nr[0] = nr[1] = 0;
	ca = 0;
	for (n=0; (uint)n*8<aret->size; n++) {
		k = KBASE(aret->cls[n]);
		reg[n] = TMP(retreg[k][nr[k]++]);
		ca += 1 << (2 * k);
	}
	return ca;
}

static void
selret(Blk *b, Fn *fn)
{
	int j, k, ca;
	Ref r, r0, reg[2];
	AClass aret;

	j = b->jmp.type;

	if (!isret(j) || j == Jret0)
		return;

	r0 = b->jmp.arg;
	b->jmp.type = Jret0;

	if (j == Jretc) {
		typclass(&aret, &qbe_typ[fn->retty]);
		if (aret.inmem) {
			assert(rtype(fn->retr) == RTmp);
			qbe_emit(Ocopy, Kl, TMP(RAX), fn->retr, R);
			qbe_emit(Oblit1, 0, R, INT(aret.type->size), R);
			qbe_emit(Oblit0, 0, R, r0, fn->retr);
			ca = 1;
		} else {
			ca = retr(reg, &aret);
			if (aret.size > 8) {
				r = qbe_newtmp("abi", Kl, fn);
				qbe_emit(Oload, Kl, reg[1], r, R);
				qbe_emit(Oadd, Kl, r, r0, qbe_getcon(8, fn));
			}
			qbe_emit(Oload, Kl, reg[0], r0, R);
		}
	} else {
		k = j - Jretw;
		if (KBASE(k) == 0) {
			qbe_emit(Ocopy, k, TMP(RAX), r0, R);
			ca = 1;
		} else {
			qbe_emit(Ocopy, k, TMP(XMM0), r0, R);
			ca = 1 << 2;
		}
	}

	b->jmp.arg = CALL(ca);
}

static int
argsclass(Ins *i0, Ins *i1, AClass *ac, int op, AClass *aret, Ref *env)
{
	int varc, envc, nint, ni, nsse, ns, n, *pn;
	AClass *a;
	Ins *i;

	if (aret && aret->inmem)
		nint = 5; /* hidden argument */
	else
		nint = 6;
	nsse = 8;
	varc = 0;
	envc = 0;
	for (i=i0, a=ac; i<i1; i++, a++)
		switch (i->op - op + Oarg) {
		case Oarg:
			if (KBASE(i->cls) == 0)
				pn = &nint;
			else
				pn = &nsse;
			if (*pn > 0) {
				--*pn;
				a->inmem = 0;
			} else
				a->inmem = 2;
			a->align = 3;
			a->size = 8;
			a->cls[0] = i->cls;
			break;
		case Oargc:
			n = i->arg[0].val;
			typclass(a, &qbe_typ[n]);
			if (a->inmem)
				continue;
			ni = ns = 0;
			for (n=0; (uint)n*8<a->size; n++)
				if (KBASE(a->cls[n]) == 0)
					ni++;
				else
					ns++;
			if (nint >= ni && nsse >= ns) {
				nint -= ni;
				nsse -= ns;
			} else
				a->inmem = 1;
			break;
		case Oarge:
			envc = 1;
			if (op == Opar)
				*env = i->to;
			else
				*env = i->arg[0];
			break;
		case Oargv:
			varc = 1;
			break;
		default:
			die("unreachable");
		}

	if (varc && envc)
		qbe_err("sysv abi does not support variadic env calls");

	return ((varc|envc) << 12) | ((6-nint) << 4) | ((8-nsse) << 8);
}

int qbe_amd64_sysv_rsave[] = {
	RDI, RSI, RDX, RCX, R8, R9, R10, R11, RAX,
	XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
	XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, -1
};
int qbe_amd64_sysv_rclob[] = {RBX, R12, R13, R14, R15, -1};

MAKESURE(sysv_arrays_ok,
	sizeof qbe_amd64_sysv_rsave == (NGPS+NFPS+1) * sizeof(int) &&
	sizeof qbe_amd64_sysv_rclob == (NCLR+1) * sizeof(int)
);

/* layout of call's second argument (RCall)
 *
 *  29     12    8    4  3  0
 *  |0...00|x|xxxx|xxxx|xx|xx|                  range
 *          |    |    |  |  ` gp regs returned (0..2)
 *          |    |    |  ` sse regs returned   (0..2)
 *          |    |    ` gp regs passed         (0..6)
 *          |    ` sse regs passed             (0..8)
 *          ` 1 if rax is used to pass data    (0..1)
 */

bits
qbe_amd64_sysv_retregs(Ref r, int p[2])
{
	bits b;
	int ni, nf;

	assert(rtype(r) == RCall);
	b = 0;
	ni = r.val & 3;
	nf = (r.val >> 2) & 3;
	if (ni >= 1)
		b |= BIT(RAX);
	if (ni >= 2)
		b |= BIT(RDX);
	if (nf >= 1)
		b |= BIT(XMM0);
	if (nf >= 2)
		b |= BIT(XMM1);
	if (p) {
		p[0] = ni;
		p[1] = nf;
	}
	return b;
}

bits
qbe_amd64_sysv_argregs(Ref r, int p[2])
{
	bits b;
	int j, ni, nf, ra;

	assert(rtype(r) == RCall);
	b = 0;
	ni = (r.val >> 4) & 15;
	nf = (r.val >> 8) & 15;
	ra = (r.val >> 12) & 1;
	for (j=0; j<ni; j++)
		b |= BIT(qbe_amd64_sysv_rsave[j]);
	for (j=0; j<nf; j++)
		b |= BIT(XMM0+j);
	if (p) {
		p[0] = ni + ra;
		p[1] = nf;
	}
	return b | (ra ? BIT(RAX) : 0);
}

static Ref
rarg(int ty, int *ni, int *ns)
{
	if (KBASE(ty) == 0)
		return TMP(qbe_amd64_sysv_rsave[(*ni)++]);
	else
		return TMP(XMM0 + (*ns)++);
}

static void
selcall(Fn *fn, Ins *i0, Ins *i1, RAlloc **rap)
{
	Ins *i;
	AClass *ac, *a, aret;
	int ca, ni, ns, al;
	uint stk, off;
	Ref r, r1, r2, reg[2], env;
	RAlloc *ra;

	env = R;
	ac = qbe_alloc((i1-i0) * sizeof ac[0]);

	if (!req(i1->arg[1], R)) {
		assert(rtype(i1->arg[1]) == RType);
		typclass(&aret, &qbe_typ[i1->arg[1].val]);
		ca = argsclass(i0, i1, ac, Oarg, &aret, &env);
	} else
		ca = argsclass(i0, i1, ac, Oarg, 0, &env);

	for (stk=0, a=&ac[i1-i0]; a>ac;)
		if ((--a)->inmem) {
			if (a->align > 4)
				qbe_err("sysv abi requires alignments of 16 or less");
			stk += a->size;
			if (a->align == 4)
				stk += stk & 15;
		}
	stk += stk & 15;
	if (stk) {
		r = qbe_getcon(-(int64_t)stk, fn);
		qbe_emit(Osalloc, Kl, R, r, R);
	}

	if (!req(i1->arg[1], R)) {
		if (aret.inmem) {
			/* get the return location from eax
			 * it saves one callee-save reg */
			r1 = qbe_newtmp("abi", Kl, fn);
			qbe_emit(Ocopy, Kl, i1->to, TMP(RAX), R);
			ca += 1;
		} else {
			/* todo, may read out of bounds.
			 * gcc did this up until 5.2, but
			 * this should still be fixed.
			 */
			if (aret.size > 8) {
				r = qbe_newtmp("abi", Kl, fn);
				aret.ref[1] = qbe_newtmp("abi", aret.cls[1], fn);
				qbe_emit(Ostorel, 0, R, aret.ref[1], r);
				qbe_emit(Oadd, Kl, r, i1->to, qbe_getcon(8, fn));
			}
			aret.ref[0] = qbe_newtmp("abi", aret.cls[0], fn);
			qbe_emit(Ostorel, 0, R, aret.ref[0], i1->to);
			ca += retr(reg, &aret);
			if (aret.size > 8)
				qbe_emit(Ocopy, aret.cls[1], aret.ref[1], reg[1], R);
			qbe_emit(Ocopy, aret.cls[0], aret.ref[0], reg[0], R);
			r1 = i1->to;
		}
		/* allocate return pad */
		ra = qbe_alloc(sizeof *ra);
		/* specific to NAlign == 3 */
		al = aret.align >= 2 ? aret.align - 2 : 0;
		ra->i = (Ins){Oalloc+al, Kl, r1, {qbe_getcon(aret.size, fn)}};
		ra->link = (*rap);
		*rap = ra;
	} else {
		ra = 0;
		if (KBASE(i1->cls) == 0) {
			qbe_emit(Ocopy, i1->cls, i1->to, TMP(RAX), R);
			ca += 1;
		} else {
			qbe_emit(Ocopy, i1->cls, i1->to, TMP(XMM0), R);
			ca += 1 << 2;
		}
	}

	qbe_emit(Ocall, i1->cls, R, i1->arg[0], CALL(ca));

	if (!req(R, env))
		qbe_emit(Ocopy, Kl, TMP(RAX), env, R);
	else if ((ca >> 12) & 1) /* vararg call */
		qbe_emit(Ocopy, Kw, TMP(RAX), qbe_getcon((ca >> 8) & 15, fn), R);

	ni = ns = 0;
	if (ra && aret.inmem)
		qbe_emit(Ocopy, Kl, rarg(Kl, &ni, &ns), ra->i.to, R); /* pass hidden argument */

	for (i=i0, a=ac; i<i1; i++, a++) {
		if (i->op >= Oarge || a->inmem)
			continue;
		r1 = rarg(a->cls[0], &ni, &ns);
		if (i->op == Oargc) {
			if (a->size > 8) {
				r2 = rarg(a->cls[1], &ni, &ns);
				r = qbe_newtmp("abi", Kl, fn);
				qbe_emit(Oload, a->cls[1], r2, r, R);
				qbe_emit(Oadd, Kl, r, i->arg[1], qbe_getcon(8, fn));
			}
			qbe_emit(Oload, a->cls[0], r1, i->arg[1], R);
		} else
			qbe_emit(Ocopy, i->cls, r1, i->arg[0], R);
	}

	if (!stk)
		return;

	r = qbe_newtmp("abi", Kl, fn);
	for (i=i0, a=ac, off=0; i<i1; i++, a++) {
		if (i->op >= Oarge || !a->inmem)
			continue;
		r1 = qbe_newtmp("abi", Kl, fn);
		if (i->op == Oargc) {
			if (a->align == 4)
				off += off & 15;
			qbe_emit(Oblit1, 0, R, INT(a->type->size), R);
			qbe_emit(Oblit0, 0, R, i->arg[1], r1);
		} else
			qbe_emit(Ostorel, 0, R, i->arg[0], r1);
		qbe_emit(Oadd, Kl, r1, r, qbe_getcon(off, fn));
		off += a->size;
	}
	qbe_emit(Osalloc, Kl, r, qbe_getcon(stk, fn), R);
}

static int
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	AClass *ac, *a, aret;
	Ins *i;
	int ni, ns, s, al, fa;
	Ref r, env;

	env = R;
	ac = qbe_alloc((i1-i0) * sizeof ac[0]);
	qbe_curi = &qbe_insb[NIns];
	ni = ns = 0;

	if (fn->retty >= 0) {
		typclass(&aret, &qbe_typ[fn->retty]);
		fa = argsclass(i0, i1, ac, Opar, &aret, &env);
	} else
		fa = argsclass(i0, i1, ac, Opar, 0, &env);
	fn->reg = qbe_amd64_sysv_argregs(CALL(fa), 0);

	for (i=i0, a=ac; i<i1; i++, a++) {
		if (i->op != Oparc || a->inmem)
			continue;
		if (a->size > 8) {
			r = qbe_newtmp("abi", Kl, fn);
			a->ref[1] = qbe_newtmp("abi", Kl, fn);
			qbe_emit(Ostorel, 0, R, a->ref[1], r);
			qbe_emit(Oadd, Kl, r, i->to, qbe_getcon(8, fn));
		}
		a->ref[0] = qbe_newtmp("abi", Kl, fn);
		qbe_emit(Ostorel, 0, R, a->ref[0], i->to);
		/* specific to NAlign == 3 */
		al = a->align >= 2 ? a->align - 2 : 0;
		qbe_emit(Oalloc+al, Kl, i->to, qbe_getcon(a->size, fn), R);
	}

	if (fn->retty >= 0 && aret.inmem) {
		r = qbe_newtmp("abi", Kl, fn);
		qbe_emit(Ocopy, Kl, r, rarg(Kl, &ni, &ns), R);
		fn->retr = r;
	}

	for (i=i0, a=ac, s=4; i<i1; i++, a++) {
		switch (a->inmem) {
		case 1:
			if (a->align > 4)
				qbe_err("sysv abi requires alignments of 16 or less");
			if (a->align == 4)
				s = (s+3) & -4;
			fn->tmp[i->to.val].slot = -s;
			s += a->size / 4;
			continue;
		case 2:
			qbe_emit(Oload, i->cls, i->to, SLOT(-s), R);
			s += 2;
			continue;
		}
		if (i->op == Opare)
			continue;
		r = rarg(a->cls[0], &ni, &ns);
		if (i->op == Oparc) {
			qbe_emit(Ocopy, a->cls[0], a->ref[0], r, R);
			if (a->size > 8) {
				r = rarg(a->cls[1], &ni, &ns);
				qbe_emit(Ocopy, a->cls[1], a->ref[1], r, R);
			}
		} else
			qbe_emit(Ocopy, i->cls, i->to, r, R);
	}

	if (!req(R, env))
		qbe_emit(Ocopy, Kl, env, TMP(RAX), R);

	return fa | (s*4)<<12;
}

static Blk *
split(Fn *fn, Blk *b)
{
	Blk *bn;

	++fn->nblk;
	bn = qbe_newblk();
	bn->nins = &qbe_insb[NIns] - qbe_curi;
	qbe_idup(&bn->ins, qbe_curi, bn->nins);
	qbe_curi = &qbe_insb[NIns];
	bn->visit = ++b->visit;
	qbe_strf(bn->name, "%s.%d", b->name, b->visit);
	bn->loop = b->loop;
	bn->link = b->link;
	b->link = bn;
	return bn;
}

static void
chpred(Blk *b, Blk *bp, Blk *bp1)
{
	Phi *p;
	uint a;

	for (p=b->phi; p; p=p->link) {
		for (a=0; p->blk[a]!=bp; a++)
			assert(a+1<p->narg);
		p->blk[a] = bp1;
	}
}

static void
selvaarg(Fn *fn, Blk *b, Ins *i)
{
	Ref loc, lreg, lstk, nr, r0, r1, c4, c8, c16, c, ap;
	Blk *b0, *bstk, *breg;
	int isint;

	c4 = qbe_getcon(4, fn);
	c8 = qbe_getcon(8, fn);
	c16 = qbe_getcon(16, fn);
	ap = i->arg[0];
	isint = KBASE(i->cls) == 0;

	/* @b [...]
	       r0 =l add ap, (0 or 4)
	       nr =l loadsw r0
	       r1 =w cultw nr, (48 or 176)
	       jnz r1, @breg, @bstk
	   @breg
	       r0 =l add ap, 16
	       r1 =l loadl r0
	       lreg =l add r1, nr
	       r0 =w add nr, (8 or 16)
	       r1 =l add ap, (0 or 4)
	       storew r0, r1
	   @bstk
	       r0 =l add ap, 8
	       lstk =l loadl r0
	       r1 =l add lstk, 8
	       storel r1, r0
	   @b0
	       %loc =l phi @breg %lreg, @bstk %lstk
	       i->to =(i->cls) load %loc
	*/

	loc = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Oload, i->cls, i->to, loc, R);
	b0 = split(fn, b);
	b0->jmp = b->jmp;
	b0->s1 = b->s1;
	b0->s2 = b->s2;
	if (b->s1)
		chpred(b->s1, b, b0);
	if (b->s2 && b->s2 != b->s1)
		chpred(b->s2, b, b0);

	lreg = qbe_newtmp("abi", Kl, fn);
	nr = qbe_newtmp("abi", Kl, fn);
	r0 = qbe_newtmp("abi", Kw, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorew, Kw, R, r0, r1);
	qbe_emit(Oadd, Kl, r1, ap, isint ? CON_Z : c4);
	qbe_emit(Oadd, Kw, r0, nr, isint ? c8 : c16);
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Oadd, Kl, lreg, r1, nr);
	qbe_emit(Oload, Kl, r1, r0, R);
	qbe_emit(Oadd, Kl, r0, ap, c16);
	breg = split(fn, b);
	breg->jmp.type = Jjmp;
	breg->s1 = b0;

	lstk = qbe_newtmp("abi", Kl, fn);
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r1, r0);
	qbe_emit(Oadd, Kl, r1, lstk, c8);
	qbe_emit(Oload, Kl, lstk, r0, R);
	qbe_emit(Oadd, Kl, r0, ap, c8);
	bstk = split(fn, b);
	bstk->jmp.type = Jjmp;
	bstk->s1 = b0;

	b0->phi = qbe_alloc(sizeof *b0->phi);
	*b0->phi = (Phi){
		.cls = Kl, .to = loc,
		.narg = 2,
		.blk = qbe_vnew(2, sizeof b0->phi->blk[0], PFn),
		.arg = qbe_vnew(2, sizeof b0->phi->arg[0], PFn),
	};
	b0->phi->blk[0] = bstk;
	b0->phi->blk[1] = breg;
	b0->phi->arg[0] = lstk;
	b0->phi->arg[1] = lreg;
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kw, fn);
	b->jmp.type = Jjnz;
	b->jmp.arg = r1;
	b->s1 = breg;
	b->s2 = bstk;
	c = qbe_getcon(isint ? 48 : 176, fn);
	qbe_emit(Ocmpw+Ciult, Kw, r1, nr, c);
	qbe_emit(Oloadsw, Kl, nr, r0, R);
	qbe_emit(Oadd, Kl, r0, ap, isint ? CON_Z : c4);
}

static void
selvastart(Fn *fn, int fa, Ref ap)
{
	Ref r0, r1;
	int gp, fp, sp;

	gp = ((fa >> 4) & 15) * 8;
	fp = 48 + ((fa >> 8) & 15) * 16;
	sp = fa >> 12;
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r1, r0);
	qbe_emit(Oadd, Kl, r1, TMP(RBP), qbe_getcon(-176, fn));
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(16, fn));
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r1, r0);
	qbe_emit(Oadd, Kl, r1, TMP(RBP), qbe_getcon(sp, fn));
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(8, fn));
	r0 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorew, Kw, R, qbe_getcon(fp, fn), r0);
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(4, fn));
	qbe_emit(Ostorew, Kw, R, qbe_getcon(gp, fn), ap);
}

void
qbe_amd64_sysv_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0, *ip;
	RAlloc *ral;
	int n, fa;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	fa = selpar(fn, b->ins, i);
	n = b->nins - (i - b->ins) + (&qbe_insb[NIns] - qbe_curi);
	i0 = qbe_alloc(n * sizeof(Ins));
	ip = qbe_icpy(ip = i0, qbe_curi, &qbe_insb[NIns] - qbe_curi);
	ip = qbe_icpy(ip, i, &b->ins[b->nins] - i);
	b->nins = n;
	b->ins = i0;

	/* lower calls, returns, and vararg instructions */
	ral = 0;
	b = fn->start;
	do {
		if (!(b = b->link))
			b = fn->start; /* do it last */
		if (b->visit)
			continue;
		qbe_curi = &qbe_insb[NIns];
		selret(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;)
			switch ((--i)->op) {
			default:
				qbe_emiti(*i);
				break;
			case Ocall:
				for (i0=i; i0>b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;
				selcall(fn, i0, i, &ral);
				i = i0;
				break;
			case Ovastart:
				selvastart(fn, fa, i->arg[0]);
				break;
			case Ovaarg:
				selvaarg(fn, b, i);
				break;
			case Oarg:
			case Oargc:
				die("unreachable");
			}
		if (b == fn->start)
			for (; ral; ral=ral->link)
				qbe_emiti(ral->i);
		b->nins = &qbe_insb[NIns] - qbe_curi;
		qbe_idup(&b->ins, qbe_curi, b->nins);
	} while (b != fn->start);

	if (qbe_debug['A']) {
		fprintf(stderr, "\n> After ABI lowering:\n");
		qbe_printfn(fn, stderr);
	}
}
