#include "all.h"

void
qbe_liveon(BSet *v, Blk *b, Blk *s)
{
	Phi *p;
	uint a;

	qbe_bscopy(v, s->in);
	for (p=s->phi; p; p=p->link)
		if (rtype(p->to) == RTmp)
			qbe_bsclr(v, p->to.val);
	for (p=s->phi; p; p=p->link)
		for (a=0; a<p->narg; a++)
			if (p->blk[a] == b)
			if (rtype(p->arg[a]) == RTmp) {
				qbe_bsset(v, p->arg[a].val);
				qbe_bsset(b->gen, p->arg[a].val);
			}
}

static void
bset(Ref r, Blk *b, int *nlv, Tmp *tmp)
{

	if (rtype(r) != RTmp)
		return;
	qbe_bsset(b->gen, r.val);
	if (!bshas(b->in, r.val)) {
		nlv[KBASE(tmp[r.val].cls)]++;
		qbe_bsset(b->in, r.val);
	}
}

/* liveness analysis
 * requires rpo computation
 */
void
qbe_filllive(Fn *f)
{
	Blk *b;
	Ins *i;
	int k, t, m[2], n, chg, nlv[2];
	BSet u[1], v[1];
	Mem *ma;

	qbe_bsinit(u, f->ntmp);
	qbe_bsinit(v, f->ntmp);
	for (b=f->start; b; b=b->link) {
		qbe_bsinit(b->in, f->ntmp);
		qbe_bsinit(b->out, f->ntmp);
		qbe_bsinit(b->gen, f->ntmp);
	}
	chg = 1;
Again:
	for (n=f->nblk-1; n>=0; n--) {
		b = f->rpo[n];

		qbe_bscopy(u, b->out);
		if (b->s1) {
			qbe_liveon(v, b, b->s1);
			qbe_bsunion(b->out, v);
		}
		if (b->s2) {
			qbe_liveon(v, b, b->s2);
			qbe_bsunion(b->out, v);
		}
		chg |= !qbe_bsequal(b->out, u);

		memset(nlv, 0, sizeof nlv);
		b->out->t[0] |= qbe_T.rglob;
		qbe_bscopy(b->in, b->out);
		for (t=0; qbe_bsiter(b->in, &t); t++)
			nlv[KBASE(f->tmp[t].cls)]++;
		if (rtype(b->jmp.arg) == RCall) {
			assert((int)qbe_bscount(b->in) == qbe_T.nrglob &&
				b->in->t[0] == qbe_T.rglob);
			b->in->t[0] |= qbe_T.retregs(b->jmp.arg, nlv);
		} else
			bset(b->jmp.arg, b, nlv, f->tmp);
		for (k=0; k<2; k++)
			b->nlive[k] = nlv[k];
		for (i=&b->ins[b->nins]; i!=b->ins;) {
			if ((--i)->op == Ocall && rtype(i->arg[1]) == RCall) {
				b->in->t[0] &= ~qbe_T.retregs(i->arg[1], m);
				for (k=0; k<2; k++) {
					nlv[k] -= m[k];
					/* caller-save registers are used
					 * by the callee, in that sense,
					 * right in the middle of the call,
					 * they are live: */
					nlv[k] += qbe_T.nrsave[k];
					if (nlv[k] > b->nlive[k])
						b->nlive[k] = nlv[k];
				}
				b->in->t[0] |= qbe_T.argregs(i->arg[1], m);
				for (k=0; k<2; k++) {
					nlv[k] -= qbe_T.nrsave[k];
					nlv[k] += m[k];
				}
			}
			if (!req(i->to, R)) {
				assert(rtype(i->to) == RTmp);
				t = i->to.val;
				if (bshas(b->in, t))
					nlv[KBASE(f->tmp[t].cls)]--;
				qbe_bsset(b->gen, t);
				qbe_bsclr(b->in, t);
			}
			for (k=0; k<2; k++)
				switch (rtype(i->arg[k])) {
				case RMem:
					ma = &f->mem[i->arg[k].val];
					bset(ma->base, b, nlv, f->tmp);
					bset(ma->index, b, nlv, f->tmp);
					break;
				default:
					bset(i->arg[k], b, nlv, f->tmp);
					break;
				}
			for (k=0; k<2; k++)
				if (nlv[k] > b->nlive[k])
					b->nlive[k] = nlv[k];
		}
	}
	if (chg) {
		chg = 0;
		goto Again;
	}

	if (qbe_debug['L']) {
		fprintf(stderr, "\n> Liveness analysis:\n");
		for (b=f->start; b; b=b->link) {
			fprintf(stderr, "\t%-10sin:   ", b->name);
			qbe_dumpts(b->in, f->tmp, stderr);
			fprintf(stderr, "\t          out:  ");
			qbe_dumpts(b->out, f->tmp, stderr);
			fprintf(stderr, "\t          gen:  ");
			qbe_dumpts(b->gen, f->tmp, stderr);
			fprintf(stderr, "\t          live: ");
			fprintf(stderr, "%d %d\n", b->nlive[0], b->nlive[1]);
		}
	}
}
