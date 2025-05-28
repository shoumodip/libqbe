#include "all.h"
#include <stdarg.h>

static void
adduse(Tmp *tmp, int ty, Blk *b, ...)
{
	Use *u;
	int n;
	va_list ap;

	if (!tmp->use)
		return;
	va_start(ap, b);
	n = tmp->nuse;
	qbe_vgrow(&tmp->use, ++tmp->nuse);
	u = &tmp->use[n];
	u->type = ty;
	u->bid = b->id;
	switch (ty) {
	case UPhi:
		u->u.phi = va_arg(ap, Phi *);
		break;
	case UIns:
		u->u.ins = va_arg(ap, Ins *);
		break;
	case UJmp:
		break;
	default:
		die("unreachable");
	}
	va_end(ap);
}

/* fill usage, width, phi, and class information
 * must not change .visit fields
 */
void
qbe_filluse(Fn *fn)
{
	Blk *b;
	Phi *p;
	Ins *i;
	int m, t, tp, w;
	uint a;
	Tmp *tmp;

	/* todo, is this the correct file? */
	tmp = fn->tmp;
	for (t=Tmp0; t<fn->ntmp; t++) {
		tmp[t].def = 0;
		tmp[t].bid = -1u;
		tmp[t].ndef = 0;
		tmp[t].nuse = 0;
		tmp[t].cls = 0;
		tmp[t].phi = 0;
		tmp[t].width = WFull;
		if (tmp[t].use == 0)
			tmp[t].use = qbe_vnew(0, sizeof(Use), PFn);
	}
	for (b=fn->start; b; b=b->link) {
		for (p=b->phi; p; p=p->link) {
			assert(rtype(p->to) == RTmp);
			tp = p->to.val;
			tmp[tp].bid = b->id;
			tmp[tp].ndef++;
			tmp[tp].cls = p->cls;
			tp = qbe_phicls(tp, fn->tmp);
			for (a=0; a<p->narg; a++)
				if (rtype(p->arg[a]) == RTmp) {
					t = p->arg[a].val;
					adduse(&tmp[t], UPhi, b, p);
					t = qbe_phicls(t, fn->tmp);
					if (t != tp)
						tmp[t].phi = tp;
				}
		}
		for (i=b->ins; i<&b->ins[b->nins]; i++) {
			if (!req(i->to, R)) {
				assert(rtype(i->to) == RTmp);
				w = WFull;
				if (isparbh(i->op))
					w = Wsb + (i->op - Oparsb);
				if (isload(i->op) && i->op != Oload)
					w = Wsb + (i->op - Oloadsb);
				if (isext(i->op))
					w = Wsb + (i->op - Oextsb);
				if (w == Wsw || w == Wuw)
				if (i->cls == Kw)
					w = WFull;
				t = i->to.val;
				tmp[t].width = w;
				tmp[t].def = i;
				tmp[t].bid = b->id;
				tmp[t].ndef++;
				tmp[t].cls = i->cls;
			}
			for (m=0; m<2; m++)
				if (rtype(i->arg[m]) == RTmp) {
					t = i->arg[m].val;
					adduse(&tmp[t], UIns, b, i);
				}
		}
		if (rtype(b->jmp.arg) == RTmp)
			adduse(&tmp[b->jmp.arg.val], UJmp, b);
	}
}

static Ref
refindex(int t, Fn *fn)
{
	return qbe_newtmp(fn->tmp[t].name, fn->tmp[t].cls, fn);
}

static void
phiins(Fn *fn)
{
	BSet u[1], defs[1];
	Blk *a, *b, **blist, **be, **bp;
	Ins *i;
	Phi *p;
	Use *use;
	Ref r;
	int t, nt, ok;
	uint n, defb;
	short k;

	qbe_bsinit(u, fn->nblk);
	qbe_bsinit(defs, fn->nblk);
	blist = qbe_emalloc(fn->nblk * sizeof blist[0]);
	be = &blist[fn->nblk];
	nt = fn->ntmp;
	for (t=Tmp0; t<nt; t++) {
		fn->tmp[t].visit = 0;
		if (fn->tmp[t].phi != 0)
			continue;
		if (fn->tmp[t].ndef == 1) {
			ok = 1;
			defb = fn->tmp[t].bid;
			use = fn->tmp[t].use;
			for (n=fn->tmp[t].nuse; n--; use++)
				ok &= use->bid == defb;
			if (ok || defb == fn->start->id)
				continue;
		}
		qbe_bszero(u);
		k = -1;
		bp = be;
		for (b=fn->start; b; b=b->link) {
			b->visit = 0;
			r = R;
			for (i=b->ins; i<&b->ins[b->nins]; i++) {
				if (!req(r, R)) {
					if (req(i->arg[0], TMP(t)))
						i->arg[0] = r;
					if (req(i->arg[1], TMP(t)))
						i->arg[1] = r;
				}
				if (req(i->to, TMP(t))) {
					if (!bshas(b->out, t)) {
						r = refindex(t, fn);
						i->to = r;
					} else {
						if (!bshas(u, b->id)) {
							qbe_bsset(u, b->id);
							*--bp = b;
						}
						if (qbe_clsmerge(&k, i->cls))
							die("invalid input");
					}
				}
			}
			if (!req(r, R) && req(b->jmp.arg, TMP(t)))
				b->jmp.arg = r;
		}
		qbe_bscopy(defs, u);
		while (bp != be) {
			fn->tmp[t].visit = t;
			b = *bp++;
			qbe_bsclr(u, b->id);
			for (n=0; n<b->nfron; n++) {
				a = b->fron[n];
				if (a->visit++ == 0)
				if (bshas(a->in, t)) {
					p = qbe_alloc(sizeof *p);
					p->cls = k;
					p->to = TMP(t);
					p->link = a->phi;
					p->arg = qbe_vnew(0, sizeof p->arg[0], PFn);
					p->blk = qbe_vnew(0, sizeof p->blk[0], PFn);
					a->phi = p;
					if (!bshas(defs, a->id))
					if (!bshas(u, a->id)) {
						qbe_bsset(u, a->id);
						*--bp = a;
					}
				}
			}
		}
	}
	free(blist);
}

typedef struct Name Name;
struct Name {
	Ref r;
	Blk *b;
	Name *up;
};

static Name *namel;

static Name *
nnew(Ref r, Blk *b, Name *up)
{
	Name *n;

	if (namel) {
		n = namel;
		namel = n->up;
	} else
		/* could use alloc, here
		 * but namel should be reset
		 */
		n = qbe_emalloc(sizeof *n);
	n->r = r;
	n->b = b;
	n->up = up;
	return n;
}

static void
nfree(Name *n)
{
	n->up = namel;
	namel = n;
}

static void
rendef(Ref *r, Blk *b, Name **stk, Fn *fn)
{
	Ref r1;
	int t;

	t = r->val;
	if (req(*r, R) || !fn->tmp[t].visit)
		return;
	r1 = refindex(t, fn);
	fn->tmp[r1.val].visit = t;
	stk[t] = nnew(r1, b, stk[t]);
	*r = r1;
}

static Ref
getstk(int t, Blk *b, Name **stk)
{
	Name *n, *n1;

	n = stk[t];
	while (n && !qbe_dom(n->b, b)) {
		n1 = n;
		n = n->up;
		nfree(n1);
	}
	stk[t] = n;
	if (!n) {
		/* uh, oh, warn */
		return UNDEF;
	} else
		return n->r;
}

static void
renblk(Blk *b, Name **stk, Fn *fn)
{
	Phi *p;
	Ins *i;
	Blk *s, **ps, *succ[3];
	int t, m;

	for (p=b->phi; p; p=p->link)
		rendef(&p->to, b, stk, fn);
	for (i=b->ins; i<&b->ins[b->nins]; i++) {
		for (m=0; m<2; m++) {
			t = i->arg[m].val;
			if (rtype(i->arg[m]) == RTmp)
			if (fn->tmp[t].visit)
				i->arg[m] = getstk(t, b, stk);
		}
		rendef(&i->to, b, stk, fn);
	}
	t = b->jmp.arg.val;
	if (rtype(b->jmp.arg) == RTmp)
	if (fn->tmp[t].visit)
		b->jmp.arg = getstk(t, b, stk);
	succ[0] = b->s1;
	succ[1] = b->s2 == b->s1 ? 0 : b->s2;
	succ[2] = 0;
	for (ps=succ; (s=*ps); ps++)
		for (p=s->phi; p; p=p->link) {
			t = p->to.val;
			if ((t=fn->tmp[t].visit)) {
				m = p->narg++;
				qbe_vgrow(&p->arg, p->narg);
				qbe_vgrow(&p->blk, p->narg);
				p->arg[m] = getstk(t, b, stk);
				p->blk[m] = b;
			}
		}
	for (s=b->dom; s; s=s->dlink)
		renblk(s, stk, fn);
}

/* require rpo and use */
void
qbe_ssa(Fn *fn)
{
	Name **stk, *n;
	int d, nt;
	Blk *b, *b1;

	nt = fn->ntmp;
	stk = qbe_emalloc(nt * sizeof stk[0]);
	d = qbe_debug['L'];
	qbe_debug['L'] = 0;
	qbe_filldom(fn);
	if (qbe_debug['N']) {
		fprintf(stderr, "\n> Dominators:\n");
		for (b1=fn->start; b1; b1=b1->link) {
			if (!b1->dom)
				continue;
			fprintf(stderr, "%10s:", b1->name);
			for (b=b1->dom; b; b=b->dlink)
				fprintf(stderr, " %s", b->name);
			fprintf(stderr, "\n");
		}
	}
	qbe_fillfron(fn);
	qbe_filllive(fn);
	phiins(fn);
	renblk(fn->start, stk, fn);
	while (nt--)
		while ((n=stk[nt])) {
			stk[nt] = n->up;
			nfree(n);
		}
	qbe_debug['L'] = d;
	free(stk);
	if (qbe_debug['N']) {
		fprintf(stderr, "\n> After SSA construction:\n");
		qbe_printfn(fn, stderr);
	}
}

static int
phicheck(Phi *p, Blk *b, Ref t)
{
	Blk *b1;
	uint n;

	for (n=0; n<p->narg; n++)
		if (req(p->arg[n], t)) {
			b1 = p->blk[n];
			if (b1 != b && !qbe_sdom(b, b1))
				return 1;
		}
	return 0;
}

/* require use and ssa */
void
qbe_ssacheck(Fn *fn)
{
	Tmp *t;
	Ins *i;
	Phi *p;
	Use *u;
	Blk *b, *bu;
	Ref r;

	for (t=&fn->tmp[Tmp0]; t-fn->tmp < fn->ntmp; t++) {
		if (t->ndef > 1)
			qbe_err("ssa temporary %%%s defined more than once",
				t->name);
		if (t->nuse > 0 && t->ndef == 0) {
			bu = fn->rpo[t->use[0].bid];
			goto Err;
		}
	}
	for (b=fn->start; b; b=b->link) {
		for (p=b->phi; p; p=p->link) {
			r = p->to;
			t = &fn->tmp[r.val];
			for (u=t->use; u<&t->use[t->nuse]; u++) {
				bu = fn->rpo[u->bid];
				if (u->type == UPhi) {
					if (phicheck(u->u.phi, b, r))
						goto Err;
				} else
					if (bu != b && !qbe_sdom(b, bu))
						goto Err;
			}
		}
		for (i=b->ins; i<&b->ins[b->nins]; i++) {
			if (rtype(i->to) != RTmp)
				continue;
			r = i->to;
			t = &fn->tmp[r.val];
			for (u=t->use; u<&t->use[t->nuse]; u++) {
				bu = fn->rpo[u->bid];
				if (u->type == UPhi) {
					if (phicheck(u->u.phi, b, r))
						goto Err;
				} else {
					if (bu == b) {
						if (u->type == UIns)
							if (u->u.ins <= i)
								goto Err;
					} else
						if (!qbe_sdom(b, bu))
							goto Err;
				}
			}
		}
	}
	return;
Err:
	if (t->visit)
		die("%%%s violates ssa invariant", t->name);
	else
		qbe_err("ssa temporary %%%s is used undefined in @%s",
			t->name, bu->name);
}
