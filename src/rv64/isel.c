#include "all.h"

static int
memarg(Ref *r, int op, Ins *i)
{
	if (isload(op) || op == Ocall)
		return r == &i->arg[0];
	if (isstore(op))
		return r == &i->arg[1];
	return 0;
}

static int
immarg(Ref *r, int op, Ins *i)
{
	return qbe_rv64_op[op].imm && r == &i->arg[1];
}

static void
fixarg(Ref *r, int k, Ins *i, Fn *fn)
{
	char buf[32];
	Ref r0, r1;
	int s, n, op;
	Con *c;

	r0 = r1 = *r;
	op = i ? i->op : Ocopy;
	switch (rtype(r0)) {
	case RCon:
		c = &fn->con[r0.val];
		if (c->type == CAddr && memarg(r, op, i))
			break;
		if (c->type == CBits && immarg(r, op, i))
		if (-2048 <= c->bits.i && c->bits.i < 2048)
			break;
		r1 = qbe_newtmp("isel", k, fn);
		if (KBASE(k) == 1) {
			/* load floating points from memory
			 * slots, they can't be used as
			 * immediates
			 */
			assert(c->type == CBits);
			n = qbe_stashbits(&c->bits, KWIDE(k) ? 8 : 4);
			qbe_vgrow(&fn->con, ++fn->ncon);
			c = &fn->con[fn->ncon-1];
			sprintf(buf, "\"%sfp%d\"", qbe_T.asloc, n);
			*c = (Con){.type = CAddr};
			c->sym.id = qbe_intern(buf);
			qbe_emit(Oload, k, r1, CON(c-fn->con), R);
			break;
		}
		qbe_emit(Ocopy, k, r1, r0, R);
		break;
	case RTmp:
		if (qbe_isreg(r0))
			break;
		s = fn->tmp[r0.val].slot;
		if (s != -1) {
			/* aggregate passed by value on
			 * stack, or fast local address,
			 * replace with slot if we can
			 */
			if (memarg(r, op, i)) {
				r1 = SLOT(s);
				break;
			}
			r1 = qbe_newtmp("isel", k, fn);
			qbe_emit(Oaddr, k, r1, SLOT(s), R);
			break;
		}
		if (k == Kw && fn->tmp[r0.val].cls == Kl) {
			/* TODO: this sign extension isn't needed
			 * for 32-bit arithmetic instructions
			 */
			r1 = qbe_newtmp("isel", k, fn);
			qbe_emit(Oextsw, Kl, r1, r0, R);
		} else {
			assert(k == fn->tmp[r0.val].cls);
		}
		break;
	}
	*r = r1;
}

static void
negate(Ref *pr, Fn *fn)
{
	Ref r;

	r = qbe_newtmp("isel", Kw, fn);
	qbe_emit(Oxor, Kw, *pr, r, qbe_getcon(1, fn));
	*pr = r;
}

static void
selcmp(Ins i, int k, int op, Fn *fn)
{
	Ins *icmp;
	Ref r, r0, r1;
	int sign, swap, neg;

	switch (op) {
	case Cieq:
		r = qbe_newtmp("isel", k, fn);
		qbe_emit(Oreqz, i.cls, i.to, r, R);
		qbe_emit(Oxor, k, r, i.arg[0], i.arg[1]);
		icmp = qbe_curi;
		fixarg(&icmp->arg[0], k, icmp, fn);
		fixarg(&icmp->arg[1], k, icmp, fn);
		return;
	case Cine:
		r = qbe_newtmp("isel", k, fn);
		qbe_emit(Ornez, i.cls, i.to, r, R);
		qbe_emit(Oxor, k, r, i.arg[0], i.arg[1]);
		icmp = qbe_curi;
		fixarg(&icmp->arg[0], k, icmp, fn);
		fixarg(&icmp->arg[1], k, icmp, fn);
		return;
	case Cisge: sign = 1, swap = 0, neg = 1; break;
	case Cisgt: sign = 1, swap = 1, neg = 0; break;
	case Cisle: sign = 1, swap = 1, neg = 1; break;
	case Cislt: sign = 1, swap = 0, neg = 0; break;
	case Ciuge: sign = 0, swap = 0, neg = 1; break;
	case Ciugt: sign = 0, swap = 1, neg = 0; break;
	case Ciule: sign = 0, swap = 1, neg = 1; break;
	case Ciult: sign = 0, swap = 0, neg = 0; break;
	case NCmpI+Cfeq:
	case NCmpI+Cfge:
	case NCmpI+Cfgt:
	case NCmpI+Cfle:
	case NCmpI+Cflt:
		swap = 0, neg = 0;
		break;
	case NCmpI+Cfuo:
		negate(&i.to, fn);
		/* fall through */
	case NCmpI+Cfo:
		r0 = qbe_newtmp("isel", i.cls, fn);
		r1 = qbe_newtmp("isel", i.cls, fn);
		qbe_emit(Oand, i.cls, i.to, r0, r1);
		op = KWIDE(k) ? Oceqd : Oceqs;
		qbe_emit(op, i.cls, r0, i.arg[0], i.arg[0]);
		icmp = qbe_curi;
		fixarg(&icmp->arg[0], k, icmp, fn);
		fixarg(&icmp->arg[1], k, icmp, fn);
		qbe_emit(op, i.cls, r1, i.arg[1], i.arg[1]);
		icmp = qbe_curi;
		fixarg(&icmp->arg[0], k, icmp, fn);
		fixarg(&icmp->arg[1], k, icmp, fn);
		return;
	case NCmpI+Cfne:
		swap = 0, neg = 1;
		i.op = KWIDE(k) ? Oceqd : Oceqs;
		break;
	default:
		assert(0 && "unknown comparison");
	}
	if (op < NCmpI)
		i.op = sign ? Ocsltl : Ocultl;
	if (swap) {
		r = i.arg[0];
		i.arg[0] = i.arg[1];
		i.arg[1] = r;
	}
	if (neg)
		negate(&i.to, fn);
	qbe_emiti(i);
	icmp = qbe_curi;
	fixarg(&icmp->arg[0], k, icmp, fn);
	fixarg(&icmp->arg[1], k, icmp, fn);
}

static void
sel(Ins i, Fn *fn)
{
	Ins *i0;
	int ck, cc;

	if (INRANGE(i.op, Oalloc, Oalloc1)) {
		i0 = qbe_curi - 1;
		qbe_salloc(i.to, i.arg[0], fn);
		fixarg(&i0->arg[0], Kl, i0, fn);
		return;
	}
	if (qbe_iscmp(i.op, &ck, &cc)) {
		selcmp(i, ck, cc, fn);
		return;
	}
	if (i.op != Onop) {
		qbe_emiti(i);
		i0 = qbe_curi; /* fixarg() can change curi */
		fixarg(&i0->arg[0], qbe_argcls(&i, 0), i0, fn);
		fixarg(&i0->arg[1], qbe_argcls(&i, 1), i0, fn);
	}
}

static void
seljmp(Blk *b, Fn *fn)
{
	/* TODO: replace cmp+jnz with beq/bne/blt[u]/bge[u] */
	if (b->jmp.type == Jjnz)
		fixarg(&b->jmp.arg, Kw, 0, fn);
}

void
qbe_rv64_isel(Fn *fn)
{
	Blk *b, **sb;
	Ins *i;
	Phi *p;
	uint n;
	int al;
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
				if (sz > INT_MAX - fn->slot)
					die("alloc too large");
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
				fixarg(&p->arg[n], p->cls, 0, fn);
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
