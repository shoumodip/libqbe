#include "all.h"

Blk *
qbe_newblk()
{
	static Blk z;
	Blk *b;

	b = qbe_alloc(sizeof *b);
	*b = z;
	return b;
}

void
qbe_edgedel(Blk *bs, Blk **pbd)
{
	Blk *bd;
	Phi *p;
	uint a;
	int mult;

	bd = *pbd;
	mult = 1 + (bs->s1 == bs->s2);
	*pbd = 0;
	if (!bd || mult > 1)
		return;
	for (p=bd->phi; p; p=p->link) {
		for (a=0; p->blk[a]!=bs; a++)
			assert(a+1<p->narg);
		p->narg--;
		memmove(&p->blk[a], &p->blk[a+1],
			sizeof p->blk[0] * (p->narg-a));
		memmove(&p->arg[a], &p->arg[a+1],
			sizeof p->arg[0] * (p->narg-a));
	}
	if (bd->npred != 0) {
		for (a=0; bd->pred[a]!=bs; a++)
			assert(a+1<bd->npred);
		bd->npred--;
		memmove(&bd->pred[a], &bd->pred[a+1],
			sizeof bd->pred[0] * (bd->npred-a));
	}
}

static void
addpred(Blk *bp, Blk *bc)
{
	if (!bc->pred) {
		bc->pred = qbe_alloc(bc->npred * sizeof bc->pred[0]);
		bc->visit = 0;
	}
	bc->pred[bc->visit++] = bp;
}

/* fill predecessors information in blocks */
void
qbe_fillpreds(Fn *f)
{
	Blk *b;

	for (b=f->start; b; b=b->link) {
		b->npred = 0;
		b->pred = 0;
	}
	for (b=f->start; b; b=b->link) {
		if (b->s1)
			b->s1->npred++;
		if (b->s2 && b->s2 != b->s1)
			b->s2->npred++;
	}
	for (b=f->start; b; b=b->link) {
		if (b->s1)
			addpred(b, b->s1);
		if (b->s2 && b->s2 != b->s1)
			addpred(b, b->s2);
	}
}

static int
rporec(Blk *b, uint x)
{
	Blk *s1, *s2;

	if (!b || b->id != -1u)
		return x;
	b->id = 1;
	s1 = b->s1;
	s2 = b->s2;
	if (s1 && s2 && s1->loop > s2->loop) {
		s1 = b->s2;
		s2 = b->s1;
	}
	x = rporec(s1, x);
	x = rporec(s2, x);
	b->id = x;
	assert(x != -1u);
	return x - 1;
}

/* fill the rpo information */
void
qbe_fillrpo(Fn *f)
{
	uint n;
	Blk *b, **p;

	for (b=f->start; b; b=b->link)
		b->id = -1u;
	n = 1 + rporec(f->start, f->nblk-1);
	f->nblk -= n;
	f->rpo = qbe_alloc(f->nblk * sizeof f->rpo[0]);
	for (p=&f->start; (b=*p);) {
		if (b->id == -1u) {
			qbe_edgedel(b, &b->s1);
			qbe_edgedel(b, &b->s2);
			*p = b->link;
		} else {
			b->id -= n;
			f->rpo[b->id] = b;
			p = &b->link;
		}
	}
}

/* for dominators computation, read
 * "A Simple, Fast Dominance Algorithm"
 * by K. Cooper, T. Harvey, and K. Kennedy.
 */

static Blk *
inter(Blk *b1, Blk *b2)
{
	Blk *bt;

	if (b1 == 0)
		return b2;
	while (b1 != b2) {
		if (b1->id < b2->id) {
			bt = b1;
			b1 = b2;
			b2 = bt;
		}
		while (b1->id > b2->id) {
			b1 = b1->idom;
			assert(b1);
		}
	}
	return b1;
}

void
qbe_filldom(Fn *fn)
{
	Blk *b, *d;
	int ch;
	uint n, p;

	for (b=fn->start; b; b=b->link) {
		b->idom = 0;
		b->dom = 0;
		b->dlink = 0;
	}
	do {
		ch = 0;
		for (n=1; n<fn->nblk; n++) {
			b = fn->rpo[n];
			d = 0;
			for (p=0; p<b->npred; p++)
				if (b->pred[p]->idom
				||  b->pred[p] == fn->start)
					d = inter(d, b->pred[p]);
			if (d != b->idom) {
				ch++;
				b->idom = d;
			}
		}
	} while (ch);
	for (b=fn->start; b; b=b->link)
		if ((d=b->idom)) {
			assert(d != b);
			b->dlink = d->dom;
			d->dom = b;
		}
}

int
qbe_sdom(Blk *b1, Blk *b2)
{
	assert(b1 && b2);
	if (b1 == b2)
		return 0;
	while (b2->id > b1->id)
		b2 = b2->idom;
	return b1 == b2;
}

int
qbe_dom(Blk *b1, Blk *b2)
{
	return b1 == b2 || qbe_sdom(b1, b2);
}

static void
addfron(Blk *a, Blk *b)
{
	uint n;

	for (n=0; n<a->nfron; n++)
		if (a->fron[n] == b)
			return;
	if (!a->nfron)
		a->fron = qbe_vnew(++a->nfron, sizeof a->fron[0], PFn);
	else
		qbe_vgrow(&a->fron, ++a->nfron);
	a->fron[a->nfron-1] = b;
}

/* fill the dominance frontier */
void
qbe_fillfron(Fn *fn)
{
	Blk *a, *b;

	for (b=fn->start; b; b=b->link)
		b->nfron = 0;
	for (b=fn->start; b; b=b->link) {
		if (b->s1)
			for (a=b; !qbe_sdom(a, b->s1); a=a->idom)
				addfron(a, b->s1);
		if (b->s2)
			for (a=b; !qbe_sdom(a, b->s2); a=a->idom)
				addfron(a, b->s2);
	}
}

static void
loopmark(Blk *hd, Blk *b, void f(Blk *, Blk *))
{
	uint p;

	if (b->id < hd->id || b->visit == hd->id)
		return;
	b->visit = hd->id;
	f(hd, b);
	for (p=0; p<b->npred; ++p)
		loopmark(hd, b->pred[p], f);
}

void
qbe_loopiter(Fn *fn, void f(Blk *, Blk *))
{
	uint n, p;
	Blk *b;

	for (b=fn->start; b; b=b->link)
		b->visit = -1u;
	for (n=0; n<fn->nblk; ++n) {
		b = fn->rpo[n];
		for (p=0; p<b->npred; ++p)
			if (b->pred[p]->id >= n)
				loopmark(b, b->pred[p], f);
	}
}

void
qbe_multloop(Blk *hd, Blk *b)
{
	(void)hd;
	b->loop *= 10;
}

void
qbe_fillloop(Fn *fn)
{
	Blk *b;

	for (b=fn->start; b; b=b->link)
		b->loop = 1;
	qbe_loopiter(fn, qbe_multloop);
}

static void
uffind(Blk **pb, Blk **uf)
{
	Blk **pb1;

	pb1 = &uf[(*pb)->id];
	if (*pb1) {
		uffind(pb1, uf);
		*pb = *pb1;
	}
}

/* requires rpo and no phis, breaks cfg */
void
qbe_simpljmp(Fn *fn)
{

	Blk **uf; /* union-find */
	Blk **p, *b, *ret;

	ret = qbe_newblk();
	ret->id = fn->nblk++;
	ret->jmp.type = Jret0;
	uf = qbe_emalloc(fn->nblk * sizeof uf[0]);
	for (b=fn->start; b; b=b->link) {
		assert(!b->phi);
		if (b->jmp.type == Jret0) {
			b->jmp.type = Jjmp;
			b->s1 = ret;
		}
		if (b->nins == 0)
		if (b->jmp.type == Jjmp) {
			uffind(&b->s1, uf);
			if (b->s1 != b)
				uf[b->id] = b->s1;
		}
	}
	for (p=&fn->start; (b=*p); p=&b->link) {
		if (b->s1)
			uffind(&b->s1, uf);
		if (b->s2)
			uffind(&b->s2, uf);
		if (b->s1 && b->s1 == b->s2) {
			b->jmp.type = Jjmp;
			b->s2 = 0;
		}
	}
	*p = ret;
	free(uf);
}
