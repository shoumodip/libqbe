#include "all.h"

static void
aggreg(Blk *hd, Blk *b)
{
	int k;

	/* aggregate looping information at
	 * loop headers */
	qbe_bsunion(hd->gen, b->gen);
	for (k=0; k<2; k++)
		if (b->nlive[k] > hd->nlive[k])
			hd->nlive[k] = b->nlive[k];
}

static void
tmpuse(Ref r, int use, int loop, Fn *fn)
{
	Mem *m;
	Tmp *t;

	if (rtype(r) == RMem) {
		m = &fn->mem[r.val];
		tmpuse(m->base, 1, loop, fn);
		tmpuse(m->index, 1, loop, fn);
	}
	else if (rtype(r) == RTmp && r.val >= Tmp0) {
		t = &fn->tmp[r.val];
		t->nuse += use;
		t->ndef += !use;
		t->cost += loop;
	}
}

/* evaluate spill costs of temporaries,
 * this also fills usage information
 * requires rpo, preds
 */
void
qbe_fillcost(Fn *fn)
{
	int n;
	uint a;
	Blk *b;
	Ins *i;
	Tmp *t;
	Phi *p;

	qbe_loopiter(fn, aggreg);
	if (qbe_debug['S']) {
		fprintf(stderr, "\n> Loop information:\n");
		for (b=fn->start; b; b=b->link) {
			for (a=0; a<b->npred; ++a)
				if (b->id <= b->pred[a]->id)
					break;
			if (a != b->npred) {
				fprintf(stderr, "\t%-10s", b->name);
				fprintf(stderr, " (% 3d ", b->nlive[0]);
				fprintf(stderr, "% 3d) ", b->nlive[1]);
				qbe_dumpts(b->gen, fn->tmp, stderr);
			}
		}
	}
	for (t=fn->tmp; t-fn->tmp < fn->ntmp; t++) {
		t->cost = t-fn->tmp < Tmp0 ? UINT_MAX : 0;
		t->nuse = 0;
		t->ndef = 0;
	}
	for (b=fn->start; b; b=b->link) {
		for (p=b->phi; p; p=p->link) {
			t = &fn->tmp[p->to.val];
			tmpuse(p->to, 0, 0, fn);
			for (a=0; a<p->narg; a++) {
				n = p->blk[a]->loop;
				t->cost += n;
				tmpuse(p->arg[a], 1, n, fn);
			}
		}
		n = b->loop;
		for (i=b->ins; i<&b->ins[b->nins]; i++) {
			tmpuse(i->to, 0, n, fn);
			tmpuse(i->arg[0], 1, n, fn);
			tmpuse(i->arg[1], 1, n, fn);
		}
		tmpuse(b->jmp.arg, 1, n, fn);
	}
	if (qbe_debug['S']) {
		fprintf(stderr, "\n> Spill costs:\n");
		for (n=Tmp0; n<fn->ntmp; n++)
			fprintf(stderr, "\t%-10s %d\n",
				fn->tmp[n].name,
				fn->tmp[n].cost);
		fprintf(stderr, "\n");
	}
}

static BSet *fst; /* temps to prioritize in registers (for tcmp1) */
static Tmp *tmp;  /* current temporaries (for tcmpX) */
static int ntmp;  /* current # of temps (for limit) */
static int locs;  /* stack size used by locals */
static int slot4; /* next slot of 4 bytes */
static int slot8; /* ditto, 8 bytes */
static BSet mask[2][1]; /* class masks */

static int
tcmp0(const void *pa, const void *pb)
{
	uint ca, cb;

	ca = tmp[*(int *)pa].cost;
	cb = tmp[*(int *)pb].cost;
	return (cb < ca) ? -1 : (cb > ca);
}

static int
tcmp1(const void *pa, const void *pb)
{
	int c;

	c = bshas(fst, *(int *)pb) - bshas(fst, *(int *)pa);
	return c ? c : tcmp0(pa, pb);
}

static Ref
slot(int t)
{
	int s;

	assert(t >= Tmp0 && "cannot spill register");
	s = tmp[t].slot;
	if (s == -1) {
		/* specific to NAlign == 3 */
		/* nice logic to pack stack slots
		 * on demand, there can be only
		 * one hole and slot4 points to it
		 *
		 * invariant: slot4 <= slot8
		 */
		if (KWIDE(tmp[t].cls)) {
			s = slot8;
			if (slot4 == slot8)
				slot4 += 2;
			slot8 += 2;
		} else {
			s = slot4;
			if (slot4 == slot8) {
				slot8 += 2;
				slot4 += 1;
			} else
				slot4 = slot8;
		}
		s += locs;
		tmp[t].slot = s;
	}
	return SLOT(s);
}

/* restricts b to hold at most k
 * temporaries, preferring those
 * present in f (if given), then
 * those with the largest spill
 * cost
 */
static void
limit(BSet *b, int k, BSet *f)
{
	static int *tarr, maxt;
	int i, t, nt;

	nt = qbe_bscount(b);
	if (nt <= k)
		return;
	if (nt > maxt) {
		free(tarr);
		tarr = qbe_emalloc(nt * sizeof tarr[0]);
		maxt = nt;
	}
	for (i=0, t=0; qbe_bsiter(b, &t); t++) {
		qbe_bsclr(b, t);
		tarr[i++] = t;
	}
	if (nt > 1) {
		if (!f)
			qsort(tarr, nt, sizeof tarr[0], tcmp0);
		else {
			fst = f;
			qsort(tarr, nt, sizeof tarr[0], tcmp1);
		}
	}
	for (i=0; i<k && i<nt; i++)
		qbe_bsset(b, tarr[i]);
	for (; i<nt; i++)
		slot(tarr[i]);
}

/* spills temporaries to fit the
 * target limits using the same
 * preferences as limit(); assumes
 * that k1 gprs and k2 fprs are
 * currently in use
 */
static void
limit2(BSet *b1, int k1, int k2, BSet *f)
{
	BSet b2[1];

	qbe_bsinit(b2, ntmp); /* todo, free those */
	qbe_bscopy(b2, b1);
	qbe_bsinter(b1, mask[0]);
	qbe_bsinter(b2, mask[1]);
	limit(b1, qbe_T.ngpr - k1, f);
	limit(b2, qbe_T.nfpr - k2, f);
	qbe_bsunion(b1, b2);
}

static void
sethint(BSet *u, bits r)
{
	int t;

	for (t=Tmp0; qbe_bsiter(u, &t); t++)
		tmp[qbe_phicls(t, tmp)].hint.m |= r;
}

/* reloads temporaries in u that are
 * not in v from their slots
 */
static void
reloads(BSet *u, BSet *v)
{
	int t;

	for (t=Tmp0; qbe_bsiter(u, &t); t++)
		if (!bshas(v, t))
			qbe_emit(Oload, tmp[t].cls, TMP(t), slot(t), R);
}

static void
store(Ref r, int s)
{
	if (s != -1)
		qbe_emit(Ostorew + tmp[r.val].cls, 0, R, r, SLOT(s));
}

static int
regcpy(Ins *i)
{
	return i->op == Ocopy && qbe_isreg(i->arg[0]);
}

static Ins *
dopm(Blk *b, Ins *i, BSet *v)
{
	int n, t;
	BSet u[1];
	Ins *i1;
	bits r;

	qbe_bsinit(u, ntmp); /* todo, free those */
	/* consecutive copies from
	 * registers need to be handled
	 * as one large instruction
	 *
	 * fixme: there is an assumption
	 * that calls are always followed
	 * by copy instructions here, this
	 * might not be true if previous
	 * passes change
	 */
	i1 = ++i;
	do {
		i--;
		t = i->to.val;
		if (!req(i->to, R))
		if (bshas(v, t)) {
			qbe_bsclr(v, t);
			store(i->to, tmp[t].slot);
		}
		qbe_bsset(v, i->arg[0].val);
	} while (i != b->ins && regcpy(i-1));
	qbe_bscopy(u, v);
	if (i != b->ins && (i-1)->op == Ocall) {
		v->t[0] &= ~qbe_T.retregs((i-1)->arg[1], 0);
		limit2(v, qbe_T.nrsave[0], qbe_T.nrsave[1], 0);
		for (n=0, r=0; qbe_T.rsave[n]>=0; n++)
			r |= BIT(qbe_T.rsave[n]);
		v->t[0] |= qbe_T.argregs((i-1)->arg[1], 0);
	} else {
		limit2(v, 0, 0, 0);
		r = v->t[0];
	}
	sethint(v, r);
	reloads(u, v);
	do
		qbe_emiti(*--i1);
	while (i1 != i);
	return i;
}

static void
merge(BSet *u, Blk *bu, BSet *v, Blk *bv)
{
	int t;

	if (bu->loop <= bv->loop)
		qbe_bsunion(u, v);
	else
		for (t=0; qbe_bsiter(v, &t); t++)
			if (tmp[t].slot == -1)
				qbe_bsset(u, t);
}

/* spill code insertion
 * requires spill costs, rpo, liveness
 *
 * Note: this will replace liveness
 * information (in, out) with temporaries
 * that must be in registers at block
 * borders
 *
 * Be careful with:
 * - Ocopy instructions to ensure register
 *   constraints
 */
void
qbe_spill(Fn *fn)
{
	Blk *b, *s1, *s2, *hd, **bp;
	int j, l, t, k, lvarg[2] = {0};
	uint n;
	BSet u[1], v[1], w[1];
	Ins *i;
	Phi *p;
	Mem *m;
	bits r;

	tmp = fn->tmp;
	ntmp = fn->ntmp;
	qbe_bsinit(u, ntmp);
	qbe_bsinit(v, ntmp);
	qbe_bsinit(w, ntmp);
	qbe_bsinit(mask[0], ntmp);
	qbe_bsinit(mask[1], ntmp);
	locs = fn->slot;
	slot4 = 0;
	slot8 = 0;
	for (t=0; t<ntmp; t++) {
		k = 0;
		if (t >= qbe_T.fpr0 && t < qbe_T.fpr0 + qbe_T.nfpr)
			k = 1;
		if (t >= Tmp0)
			k = KBASE(tmp[t].cls);
		qbe_bsset(mask[k], t);
	}

	for (bp=&fn->rpo[fn->nblk]; bp!=fn->rpo;) {
		b = *--bp;
		/* invariant: all blocks with bigger rpo got
		 * their in,out updated. */

		/* 1. find temporaries in registers at
		 * the end of the block (put them in v) */
		qbe_curi = 0;
		s1 = b->s1;
		s2 = b->s2;
		hd = 0;
		if (s1 && s1->id <= b->id)
			hd = s1;
		if (s2 && s2->id <= b->id)
		if (!hd || s2->id >= hd->id)
			hd = s2;
		if (hd) {
			/* back-edge */
			qbe_bszero(v);
			hd->gen->t[0] |= qbe_T.rglob; /* don't spill registers */
			for (k=0; k<2; k++) {
				n = k == 0 ? qbe_T.ngpr : qbe_T.nfpr;
				qbe_bscopy(u, b->out);
				qbe_bsinter(u, mask[k]);
				qbe_bscopy(w, u);
				qbe_bsinter(u, hd->gen);
				qbe_bsdiff(w, hd->gen);
				if (qbe_bscount(u) < n) {
					j = qbe_bscount(w); /* live through */
					l = hd->nlive[k];
					limit(w, n - (l - j), 0);
					qbe_bsunion(u, w);
				} else
					limit(u, n, 0);
				qbe_bsunion(v, u);
			}
		} else if (s1) {
			/* avoid reloading temporaries
			 * in the middle of loops */
			qbe_bszero(v);
			qbe_liveon(w, b, s1);
			merge(v, b, w, s1);
			if (s2) {
				qbe_liveon(u, b, s2);
				merge(v, b, u, s2);
				qbe_bsinter(w, u);
			}
			limit2(v, 0, 0, w);
		} else {
			qbe_bscopy(v, b->out);
			if (rtype(b->jmp.arg) == RCall)
				v->t[0] |= qbe_T.retregs(b->jmp.arg, 0);
		}
		for (t=Tmp0; qbe_bsiter(b->out, &t); t++)
			if (!bshas(v, t))
				slot(t);
		qbe_bscopy(b->out, v);

		/* 2. process the block instructions */
		if (rtype(b->jmp.arg) == RTmp) {
			t = b->jmp.arg.val;
			assert(KBASE(tmp[t].cls) == 0);
			lvarg[0] = bshas(v, t);
			qbe_bsset(v, t);
			qbe_bscopy(u, v);
			limit2(v, 0, 0, NULL);
			if (!bshas(v, t)) {
				if (!lvarg[0])
					qbe_bsclr(u, t);
				b->jmp.arg = slot(t);
			}
			reloads(u, v);
		}
		qbe_curi = &qbe_insb[NIns];
		for (i=&b->ins[b->nins]; i!=b->ins;) {
			i--;
			if (regcpy(i)) {
				i = dopm(b, i, v);
				continue;
			}
			qbe_bszero(w);
			if (!req(i->to, R)) {
				assert(rtype(i->to) == RTmp);
				t = i->to.val;
				if (bshas(v, t))
					qbe_bsclr(v, t);
				else {
					/* make sure we have a reg
					 * for the result */
					assert(t >= Tmp0 && "dead reg");
					qbe_bsset(v, t);
					qbe_bsset(w, t);
				}
			}
			j = qbe_T.memargs(i->op);
			for (n=0; n<2; n++)
				if (rtype(i->arg[n]) == RMem)
					j--;
			for (n=0; n<2; n++)
				switch (rtype(i->arg[n])) {
				case RMem:
					t = i->arg[n].val;
					m = &fn->mem[t];
					if (rtype(m->base) == RTmp) {
						qbe_bsset(v, m->base.val);
						qbe_bsset(w, m->base.val);
					}
					if (rtype(m->index) == RTmp) {
						qbe_bsset(v, m->index.val);
						qbe_bsset(w, m->index.val);
					}
					break;
				case RTmp:
					t = i->arg[n].val;
					lvarg[n] = bshas(v, t);
					qbe_bsset(v, t);
					if (j-- <= 0)
						qbe_bsset(w, t);
					break;
				}
			qbe_bscopy(u, v);
			limit2(v, 0, 0, w);
			for (n=0; n<2; n++)
				if (rtype(i->arg[n]) == RTmp) {
					t = i->arg[n].val;
					if (!bshas(v, t)) {
						/* do not reload if the
						 * argument is dead
						 */
						if (!lvarg[n])
							qbe_bsclr(u, t);
						i->arg[n] = slot(t);
					}
				}
			reloads(u, v);
			if (!req(i->to, R)) {
				t = i->to.val;
				store(i->to, tmp[t].slot);
				if (t >= Tmp0)
					/* in case i->to was a
					 * dead temporary */
					qbe_bsclr(v, t);
			}
			qbe_emiti(*i);
			r = v->t[0]; /* Tmp0 is NBit */
			if (r)
				sethint(v, r);
		}
		if (b == fn->start)
			assert(v->t[0] == (qbe_T.rglob | fn->reg));
		else
			assert(v->t[0] == qbe_T.rglob);

		for (p=b->phi; p; p=p->link) {
			assert(rtype(p->to) == RTmp);
			t = p->to.val;
			if (bshas(v, t)) {
				qbe_bsclr(v, t);
				store(p->to, tmp[t].slot);
			} else if (bshas(b->in, t))
				/* only if the phi is live */
				p->to = slot(p->to.val);
		}
		qbe_bscopy(b->in, v);
		b->nins = &qbe_insb[NIns] - qbe_curi;
		qbe_idup(&b->ins, qbe_curi, b->nins);
	}

	/* align the locals to a 16 byte boundary */
	/* specific to NAlign == 3 */
	slot8 += slot8 & 3;
	fn->slot += slot8;

	if (qbe_debug['S']) {
		fprintf(stderr, "\n> Block information:\n");
		for (b=fn->start; b; b=b->link) {
			fprintf(stderr, "\t%-10s (% 5d) ", b->name, b->loop);
			qbe_dumpts(b->out, fn->tmp, stderr);
		}
		fprintf(stderr, "\n> After spilling:\n");
		qbe_printfn(fn, stderr);
	}
}
