#include "all.h"

typedef struct Abi Abi;
typedef struct Class Class;
typedef struct Insl Insl;
typedef struct Params Params;

enum {
	Cstk = 1, /* pass on the stack */
	Cptr = 2, /* replaced by a pointer */
};

struct Class {
	char class;
	char ishfa;
	struct {
		char base;
		uchar size;
	} hfa;
	uint size;
	uint align;
	Typ *t;
	uchar nreg;
	uchar ngp;
	uchar nfp;
	int reg[4];
	int cls[4];
};

struct Insl {
	Ins i;
	Insl *link;
};

struct Params {
	uint ngp;
	uint nfp;
	uint stk;
};

static int gpreg[12] = {R0, R1, R2, R3, R4, R5, R6, R7};
static int fpreg[12] = {V0, V1, V2, V3, V4, V5, V6, V7};
static int store[] = {
	[Kw] = Ostorew, [Kl] = Ostorel,
	[Ks] = Ostores, [Kd] = Ostored
};

/* layout of call's second argument (RCall)
 *
 *         13
 *  29   14 |    9    5   2  0
 *  |0.00|x|x|xxxx|xxxx|xxx|xx|                  range
 *        | |    |    |   |  ` gp regs returned (0..2)
 *        | |    |    |   ` fp regs returned    (0..4)
 *        | |    |    ` gp regs passed          (0..8)
 *        | |     ` fp regs passed              (0..8)
 *        | ` indirect result register x8 used  (0..1)
 *        ` env pointer passed in x9            (0..1)
 */

static int
isfloatv(Typ *t, char *cls)
{
	Field *f;
	uint n;

	for (n=0; n<t->nunion; n++)
		for (f=t->fields[n]; f->type != FEnd; f++)
			switch (f->type) {
			case Fs:
				if (*cls == Kd)
					return 0;
				*cls = Ks;
				break;
			case Fd:
				if (*cls == Ks)
					return 0;
				*cls = Kd;
				break;
			case FTyp:
				if (isfloatv(&qbe_typ[f->len], cls))
					break;
				/* fall through */
			default:
				return 0;
			}
	return 1;
}

static void
typclass(Class *c, Typ *t, int *gp, int *fp)
{
	uint64_t sz;
	uint n;

	sz = (t->size + 7) & -8;
	c->t = t;
	c->class = 0;
	c->ngp = 0;
	c->nfp = 0;
	c->align = 8;

	if (t->align > 3)
		qbe_err("alignments larger than 8 are not supported");

	if (t->isdark || sz > 16 || sz == 0) {
		/* large structs are replaced by a
		 * pointer to some caller-allocated
		 * memory */
		c->class |= Cptr;
		c->size = 8;
		c->ngp = 1;
		*c->reg = *gp;
		*c->cls = Kl;
		return;
	}

	c->size = sz;
	c->hfa.base = Kx;
	c->ishfa = isfloatv(t, &c->hfa.base);
	c->hfa.size = t->size/(KWIDE(c->hfa.base) ? 8 : 4);

	if (c->ishfa)
		for (n=0; n<c->hfa.size; n++, c->nfp++) {
			c->reg[n] = *fp++;
			c->cls[n] = c->hfa.base;
		}
	else
		for (n=0; n<sz/8; n++, c->ngp++) {
			c->reg[n] = *gp++;
			c->cls[n] = Kl;
		}

	c->nreg = n;
}

static void
sttmps(Ref tmp[], int cls[], uint nreg, Ref mem, Fn *fn)
{
	uint n;
	uint64_t off;
	Ref r;

	assert(nreg <= 4);
	off = 0;
	for (n=0; n<nreg; n++) {
		tmp[n] = qbe_newtmp("abi", cls[n], fn);
		r = qbe_newtmp("abi", Kl, fn);
		qbe_emit(store[cls[n]], 0, R, tmp[n], r);
		qbe_emit(Oadd, Kl, r, mem, qbe_getcon(off, fn));
		off += KWIDE(cls[n]) ? 8 : 4;
	}
}

/* todo, may read out of bounds */
static void
ldregs(int reg[], int cls[], int n, Ref mem, Fn *fn)
{
	int i;
	uint64_t off;
	Ref r;

	off = 0;
	for (i=0; i<n; i++) {
		r = qbe_newtmp("abi", Kl, fn);
		qbe_emit(Oload, cls[i], TMP(reg[i]), r, R);
		qbe_emit(Oadd, Kl, r, mem, qbe_getcon(off, fn));
		off += KWIDE(cls[i]) ? 8 : 4;
	}
}

static void
selret(Blk *b, Fn *fn)
{
	int j, k, cty;
	Ref r;
	Class cr;

	j = b->jmp.type;

	if (!isret(j) || j == Jret0)
		return;

	r = b->jmp.arg;
	b->jmp.type = Jret0;

	if (j == Jretc) {
		typclass(&cr, &qbe_typ[fn->retty], gpreg, fpreg);
		if (cr.class & Cptr) {
			assert(rtype(fn->retr) == RTmp);
			qbe_emit(Oblit1, 0, R, INT(cr.t->size), R);
			qbe_emit(Oblit0, 0, R, r, fn->retr);
			cty = 0;
		} else {
			ldregs(cr.reg, cr.cls, cr.nreg, r, fn);
			cty = (cr.nfp << 2) | cr.ngp;
		}
	} else {
		k = j - Jretw;
		if (KBASE(k) == 0) {
			qbe_emit(Ocopy, k, TMP(R0), r, R);
			cty = 1;
		} else {
			qbe_emit(Ocopy, k, TMP(V0), r, R);
			cty = 1 << 2;
		}
	}

	b->jmp.arg = CALL(cty);
}

static int
argsclass(Ins *i0, Ins *i1, Class *carg)
{
	int va, envc, ngp, nfp, *gp, *fp;
	Class *c;
	Ins *i;

	va = 0;
	envc = 0;
	gp = gpreg;
	fp = fpreg;
	ngp = 8;
	nfp = 8;
	for (i=i0, c=carg; i<i1; i++, c++)
		switch (i->op) {
		case Oargsb:
		case Oargub:
		case Oparsb:
		case Oparub:
			c->size = 1;
			goto Scalar;
		case Oargsh:
		case Oarguh:
		case Oparsh:
		case Oparuh:
			c->size = 2;
			goto Scalar;
		case Opar:
		case Oarg:
			c->size = 8;
			if (qbe_T.apple && !KWIDE(i->cls))
				c->size = 4;
		Scalar:
			c->align = c->size;
			*c->cls = i->cls;
			if (va) {
				c->class |= Cstk;
				c->size = 8;
				c->align = 8;
				break;
			}
			if (KBASE(i->cls) == 0 && ngp > 0) {
				ngp--;
				*c->reg = *gp++;
				break;
			}
			if (KBASE(i->cls) == 1 && nfp > 0) {
				nfp--;
				*c->reg = *fp++;
				break;
			}
			c->class |= Cstk;
			break;
		case Oparc:
		case Oargc:
			typclass(c, &qbe_typ[i->arg[0].val], gp, fp);
			if (c->ngp <= ngp) {
				if (c->nfp <= nfp) {
					ngp -= c->ngp;
					nfp -= c->nfp;
					gp += c->ngp;
					fp += c->nfp;
					break;
				} else
					nfp = 0;
			} else
				ngp = 0;
			c->class |= Cstk;
			break;
		case Opare:
		case Oarge:
			*c->reg = R9;
			*c->cls = Kl;
			envc = 1;
			break;
		case Oargv:
			va = qbe_T.apple != 0;
			break;
		default:
			die("unreachable");
		}

	return envc << 14 | (gp-gpreg) << 5 | (fp-fpreg) << 9;
}

bits
qbe_arm64_retregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp;

	assert(rtype(r) == RCall);
	ngp = r.val & 3;
	nfp = (r.val >> 2) & 7;
	if (p) {
		p[0] = ngp;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(R0+ngp);
	while (nfp--)
		b |= BIT(V0+nfp);
	return b;
}

bits
qbe_arm64_argregs(Ref r, int p[2])
{
	bits b;
	int ngp, nfp, x8, x9;

	assert(rtype(r) == RCall);
	ngp = (r.val >> 5) & 15;
	nfp = (r.val >> 9) & 15;
	x8 = (r.val >> 13) & 1;
	x9 = (r.val >> 14) & 1;
	if (p) {
		p[0] = ngp + x8 + x9;
		p[1] = nfp;
	}
	b = 0;
	while (ngp--)
		b |= BIT(R0+ngp);
	while (nfp--)
		b |= BIT(V0+nfp);
	return b | ((bits)x8 << R8) | ((bits)x9 << R9);
}

static void
stkblob(Ref r, Class *c, Fn *fn, Insl **ilp)
{
	Insl *il;
	int al;
	uint64_t sz;

	il = qbe_alloc(sizeof *il);
	al = c->t->align - 2; /* NAlign == 3 */
	if (al < 0)
		al = 0;
	sz = c->class & Cptr ? c->t->size : c->size;
	il->i = (Ins){Oalloc+al, Kl, r, {qbe_getcon(sz, fn)}};
	il->link = *ilp;
	*ilp = il;
}

static uint
align(uint x, uint al)
{
	return (x + al-1) & -al;
}

static void
selcall(Fn *fn, Ins *i0, Ins *i1, Insl **ilp)
{
	Ins *i;
	Class *ca, *c, cr;
	int op, cty;
	uint n, stk, off;;
	Ref r, rstk, tmp[4];

	ca = qbe_alloc((i1-i0) * sizeof ca[0]);
	cty = argsclass(i0, i1, ca);

	stk = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (c->class & Cptr) {
			i->arg[0] = qbe_newtmp("abi", Kl, fn);
			stkblob(i->arg[0], c, fn, ilp);
			i->op = Oarg;
		}
		if (c->class & Cstk) {
			stk = align(stk, c->align);
			stk += c->size;
		}
	}
	stk = align(stk, 16);
	rstk = qbe_getcon(stk, fn);
	if (stk)
		qbe_emit(Oadd, Kl, TMP(SP), TMP(SP), rstk);

	if (!req(i1->arg[1], R)) {
		typclass(&cr, &qbe_typ[i1->arg[1].val], gpreg, fpreg);
		stkblob(i1->to, &cr, fn, ilp);
		cty |= (cr.nfp << 2) | cr.ngp;
		if (cr.class & Cptr) {
			/* spill & rega expect calls to be
			 * followed by copies from regs,
			 * so we emit a dummy
			 */
			cty |= 1 << 13 | 1;
			qbe_emit(Ocopy, Kw, R, TMP(R0), R);
		} else {
			sttmps(tmp, cr.cls, cr.nreg, i1->to, fn);
			for (n=0; n<cr.nreg; n++) {
				r = TMP(cr.reg[n]);
				qbe_emit(Ocopy, cr.cls[n], tmp[n], r, R);
			}
		}
	} else {
		if (KBASE(i1->cls) == 0) {
			qbe_emit(Ocopy, i1->cls, i1->to, TMP(R0), R);
			cty |= 1;
		} else {
			qbe_emit(Ocopy, i1->cls, i1->to, TMP(V0), R);
			cty |= 1 << 2;
		}
	}

	qbe_emit(Ocall, 0, R, i1->arg[0], CALL(cty));

	if (cty & (1 << 13))
		/* struct return argument */
		qbe_emit(Ocopy, Kl, TMP(R8), i1->to, R);

	for (i=i0, c=ca; i<i1; i++, c++) {
		if ((c->class & Cstk) != 0)
			continue;
		if (i->op == Oarg || i->op == Oarge)
			qbe_emit(Ocopy, *c->cls, TMP(*c->reg), i->arg[0], R);
		if (i->op == Oargc)
			ldregs(c->reg, c->cls, c->nreg, i->arg[1], fn);
	}

	/* populate the stack */
	off = 0;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if ((c->class & Cstk) == 0)
			continue;
		off = align(off, c->align);
		r = qbe_newtmp("abi", Kl, fn);
		if (i->op == Oarg || isargbh(i->op)) {
			switch (c->size) {
			case 1: op = Ostoreb; break;
			case 2: op = Ostoreh; break;
			case 4:
			case 8: op = store[*c->cls]; break;
			default: die("unreachable");
			}
			qbe_emit(op, 0, R, i->arg[0], r);
		} else {
			assert(i->op == Oargc);
			qbe_emit(Oblit1, 0, R, INT(c->size), R);
			qbe_emit(Oblit0, 0, R, i->arg[1], r);
		}
		qbe_emit(Oadd, Kl, r, TMP(SP), qbe_getcon(off, fn));
		off += c->size;
	}
	if (stk)
		qbe_emit(Osub, Kl, TMP(SP), TMP(SP), rstk);

	for (i=i0, c=ca; i<i1; i++, c++)
		if (c->class & Cptr) {
			qbe_emit(Oblit1, 0, R, INT(c->t->size), R);
			qbe_emit(Oblit0, 0, R, i->arg[1], i->arg[0]);
		}
}

static Params
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Class *ca, *c, cr;
	Insl *il;
	Ins *i;
	int op, n, cty;
	uint off;
	Ref r, tmp[16], *t;

	ca = qbe_alloc((i1-i0) * sizeof ca[0]);
	qbe_curi = &qbe_insb[NIns];

	cty = argsclass(i0, i1, ca);
	fn->reg = qbe_arm64_argregs(CALL(cty), 0);

	il = 0;
	t = tmp;
	for (i=i0, c=ca; i<i1; i++, c++) {
		if (i->op != Oparc || (c->class & (Cptr|Cstk)))
			continue;
		sttmps(t, c->cls, c->nreg, i->to, fn);
		stkblob(i->to, c, fn, &il);
		t += c->nreg;
	}
	for (; il; il=il->link)
		qbe_emiti(il->i);

	if (fn->retty >= 0) {
		typclass(&cr, &qbe_typ[fn->retty], gpreg, fpreg);
		if (cr.class & Cptr) {
			fn->retr = qbe_newtmp("abi", Kl, fn);
			qbe_emit(Ocopy, Kl, fn->retr, TMP(R8), R);
			fn->reg |= BIT(R8);
		}
	}

	t = tmp;
	off = 0;
	for (i=i0, c=ca; i<i1; i++, c++)
		if (i->op == Oparc && !(c->class & Cptr)) {
			if (c->class & Cstk) {
				off = align(off, c->align);
				fn->tmp[i->to.val].slot = -(off+2);
				off += c->size;
			} else
				for (n=0; n<c->nreg; n++) {
					r = TMP(c->reg[n]);
					qbe_emit(Ocopy, c->cls[n], *t++, r, R);
				}
		} else if (c->class & Cstk) {
			off = align(off, c->align);
			if (isparbh(i->op))
				op = Oloadsb + (i->op - Oparsb);
			else
				op = Oload;
			qbe_emit(op, *c->cls, i->to, SLOT(-(off+2)), R);
			off += c->size;
		} else {
			qbe_emit(Ocopy, *c->cls, i->to, TMP(*c->reg), R);
		}

	return (Params){
		.stk = align(off, 8),
		.ngp = (cty >> 5) & 15,
		.nfp = (cty >> 9) & 15
	};
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
apple_selvaarg(Fn *fn, Blk *b, Ins *i)
{
	Ref ap, stk, stk8, c8;

	(void)b;
	c8 = qbe_getcon(8, fn);
	ap = i->arg[0];
	stk8 = qbe_newtmp("abi", Kl, fn);
	stk = qbe_newtmp("abi", Kl, fn);

	qbe_emit(Ostorel, 0, R, stk8, ap);
	qbe_emit(Oadd, Kl, stk8, stk, c8);
	qbe_emit(Oload, i->cls, i->to, stk, R);
	qbe_emit(Oload, Kl, stk, ap, R);
}

static void
arm64_selvaarg(Fn *fn, Blk *b, Ins *i)
{
	Ref loc, lreg, lstk, nr, r0, r1, c8, c16, c24, c28, ap;
	Blk *b0, *bstk, *breg;
	int isgp;

	c8 = qbe_getcon(8, fn);
	c16 = qbe_getcon(16, fn);
	c24 = qbe_getcon(24, fn);
	c28 = qbe_getcon(28, fn);
	ap = i->arg[0];
	isgp = KBASE(i->cls) == 0;

	/* @b [...]
	       r0 =l add ap, (24 or 28)
	       nr =l loadsw r0
	       r1 =w csltw nr, 0
	       jnz r1, @breg, @bstk
	   @breg
	       r0 =l add ap, (8 or 16)
	       r1 =l loadl r0
	       lreg =l add r1, nr
	       r0 =w add nr, (8 or 16)
	       r1 =l add ap, (24 or 28)
	       storew r0, r1
	   @bstk
	       lstk =l loadl ap
	       r0 =l add lstk, 8
	       storel r0, ap
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
	qbe_emit(Oadd, Kl, r1, ap, isgp ? c24 : c28);
	qbe_emit(Oadd, Kw, r0, nr, isgp ? c8 : c16);
	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Oadd, Kl, lreg, r1, nr);
	qbe_emit(Oload, Kl, r1, r0, R);
	qbe_emit(Oadd, Kl, r0, ap, isgp ? c8 : c16);
	breg = split(fn, b);
	breg->jmp.type = Jjmp;
	breg->s1 = b0;

	lstk = qbe_newtmp("abi", Kl, fn);
	r0 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r0, ap);
	qbe_emit(Oadd, Kl, r0, lstk, c8);
	qbe_emit(Oload, Kl, lstk, ap, R);
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
	qbe_emit(Ocmpw+Cislt, Kw, r1, nr, CON_Z);
	qbe_emit(Oloadsw, Kl, nr, r0, R);
	qbe_emit(Oadd, Kl, r0, ap, isgp ? c24 : c28);
}

static void
apple_selvastart(Fn *fn, Params p, Ref ap)
{
	Ref off, stk, arg;

	off = qbe_getcon(p.stk, fn);
	stk = qbe_newtmp("abi", Kl, fn);
	arg = qbe_newtmp("abi", Kl, fn);

	qbe_emit(Ostorel, 0, R, arg, ap);
	qbe_emit(Oadd, Kl, arg, stk, off);
	qbe_emit(Oaddr, Kl, stk, SLOT(-1), R);
}

static void
arm64_selvastart(Fn *fn, Params p, Ref ap)
{
	Ref r0, r1, rsave;

	rsave = qbe_newtmp("abi", Kl, fn);

	r0 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r0, ap);
	qbe_emit(Oadd, Kl, r0, rsave, qbe_getcon(p.stk + 192, fn));

	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r1, r0);
	qbe_emit(Oadd, Kl, r1, rsave, qbe_getcon(64, fn));
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(8, fn));

	r0 = qbe_newtmp("abi", Kl, fn);
	r1 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorel, Kw, R, r1, r0);
	qbe_emit(Oadd, Kl, r1, rsave, qbe_getcon(192, fn));
	qbe_emit(Oaddr, Kl, rsave, SLOT(-1), R);
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(16, fn));

	r0 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorew, Kw, R, qbe_getcon((p.ngp-8)*8, fn), r0);
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(24, fn));

	r0 = qbe_newtmp("abi", Kl, fn);
	qbe_emit(Ostorew, Kw, R, qbe_getcon((p.nfp-8)*16, fn), r0);
	qbe_emit(Oadd, Kl, r0, ap, qbe_getcon(28, fn));
}

void
qbe_arm64_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0, *ip;
	Insl *il;
	int n;
	Params p;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	p = selpar(fn, b->ins, i);
	n = b->nins - (i - b->ins) + (&qbe_insb[NIns] - qbe_curi);
	i0 = qbe_alloc(n * sizeof(Ins));
	ip = qbe_icpy(ip = i0, qbe_curi, &qbe_insb[NIns] - qbe_curi);
	ip = qbe_icpy(ip, i, &b->ins[b->nins] - i);
	b->nins = n;
	b->ins = i0;

	/* lower calls, returns, and vararg instructions */
	il = 0;
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
				selcall(fn, i0, i, &il);
				i = i0;
				break;
			case Ovastart:
				if (qbe_T.apple)
					apple_selvastart(fn, p, i->arg[0]);
				else
					arm64_selvastart(fn, p, i->arg[0]);
				break;
			case Ovaarg:
				if (qbe_T.apple)
					apple_selvaarg(fn, b, i);
				else
					arm64_selvaarg(fn, b, i);
				break;
			case Oarg:
			case Oargc:
				die("unreachable");
			}
		if (b == fn->start)
			for (; il; il=il->link)
				qbe_emiti(il->i);
		b->nins = &qbe_insb[NIns] - qbe_curi;
		qbe_idup(&b->ins, qbe_curi, b->nins);
	} while (b != fn->start);

	if (qbe_debug['A']) {
		fprintf(stderr, "\n> After ABI lowering:\n");
		qbe_printfn(fn, stderr);
	}
}

/* abi0 for apple target; introduces
 * necessary sign extensions in calls
 * and returns
 */
void
qbe_apple_extsb(Fn *fn)
{
	Blk *b;
	Ins *i0, *i1, *i;
	int j, op;
	Ref r;

	for (b=fn->start; b; b=b->link) {
		qbe_curi = &qbe_insb[NIns];
		j = b->jmp.type;
		if (isretbh(j)) {
			r = qbe_newtmp("abi", Kw, fn);
			op = Oextsb + (j - Jretsb);
			qbe_emit(op, Kw, r, b->jmp.arg, R);
			b->jmp.arg = r;
			b->jmp.type = Jretw;
		}
		for (i=&b->ins[b->nins]; i>b->ins;) {
			qbe_emiti(*--i);
			if (i->op != Ocall)
				continue;
			for (i0=i1=i; i0>b->ins; i0--)
				if (!isarg((i0-1)->op))
					break;
			for (i=i1; i>i0;) {
				qbe_emiti(*--i);
				if (isargbh(i->op)) {
					i->to = qbe_newtmp("abi", Kl, fn);
					qbe_curi->arg[0] = i->to;
				}
			}
			for (i=i1; i>i0;)
				if (isargbh((--i)->op)) {
					op = Oextsb + (i->op - Oargsb);
					qbe_emit(op, Kw, i->to, i->arg[0], R);
				}
		}
		b->nins = &qbe_insb[NIns] - qbe_curi;
		qbe_idup(&b->ins, qbe_curi, b->nins);
	}

	if (qbe_debug['A']) {
		fprintf(stderr, "\n> After Apple pre-ABI:\n");
		qbe_printfn(fn, stderr);
	}
}
