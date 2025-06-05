#include "all.h"
#include <stdarg.h>

typedef struct Bitset Bitset;
typedef struct Vec Vec;
typedef struct Bucket Bucket;

struct Vec {
	ulong mag;
	Pool pool;
	size_t esz;
	ulong cap;
	union {
		long long ll;
		long double ld;
		void *ptr;
	} align[];
};

struct Bucket {
	uint nstr;
	char **str;
};

enum {
	VMin = 2,
	VMag = 0xcabba9e,
	NPtr = 256,
	IBits = 12,
	IMask = (1<<IBits) - 1,
};

Typ *qbe_typ;
Ins qbe_insb[NIns], *qbe_curi;

static void *ptr[NPtr];
static void **pool = ptr;
static int nptr = 1;

static Bucket itbl[IMask+1]; /* string interning table */

uint32_t
qbe_hash(char *s)
{
	uint32_t h;

	for (h=0; *s; ++s)
		h = *s + 17*h;
	return h;
}

void
qbe_die_(char *file, char *s, ...)
{
	va_list ap;

	fprintf(stderr, "%s: dying: ", file);
	va_start(ap, s);
	vfprintf(stderr, s, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}

void *
qbe_emalloc(size_t n)
{
	void *p;

	p = calloc(1, n);
	if (!p)
		die("emalloc, out of memory");
	return p;
}

void *
qbe_alloc(size_t n)
{
	void **pp;

	if (n == 0)
		return 0;
	if (nptr >= NPtr) {
		pp = qbe_emalloc(NPtr * sizeof(void *));
		pp[0] = pool;
		pool = pp;
		nptr = 1;
	}
	return pool[nptr++] = qbe_emalloc(n);
}

void
qbe_freeall()
{
	void **pp;

	for (;;) {
		for (pp = &pool[1]; pp < &pool[nptr]; pp++)
			free(*pp);
		pp = pool[0];
		if (!pp)
			break;
		free(pool);
		pool = pp;
		nptr = NPtr;
	}
	nptr = 1;
}

void *
qbe_vnew(ulong len, size_t esz, Pool pool)
{
	void *(*f)(size_t);
	ulong cap;
	Vec *v;

	for (cap=VMin; cap<len; cap*=2)
		;
	f = pool == PHeap ? qbe_emalloc : qbe_alloc;
	v = f(cap * esz + sizeof(Vec));
	v->mag = VMag;
	v->cap = cap;
	v->esz = esz;
	v->pool = pool;
	return v + 1;
}

void
qbe_vfree(void *p)
{
	Vec *v;

	v = (Vec *)p - 1;
	assert(v->mag == VMag);
	if (v->pool == PHeap) {
		v->mag = 0;
		free(v);
	}
}

void
qbe_vgrow(void *vp, ulong len)
{
	Vec *v;
	void *v1;

	v = *(Vec **)vp - 1;
	assert(v+1 && v->mag == VMag);
	if (v->cap >= len)
		return;
	v1 = qbe_vnew(len, v->esz, v->pool);
	memcpy(v1, v+1, v->cap * v->esz);
	qbe_vfree(v+1);
	*(Vec **)vp = v1;
}

void
qbe_strf(char str[NString], char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	vsnprintf(str, NString, s, ap);
	va_end(ap);
}

// Modification BEGIN
// Copyright (C) 2025 Shoumodip Kar <shoumodipkar@gmail.com>
void
qbe_free_interns(void)
{
    size_t n = (sizeof(itbl) / sizeof(*itbl));
    for (size_t i = 0; i < n; i++) {
        Bucket *b = &itbl[i];
        if (b->nstr) {
            for (size_t j = 0; j < b->nstr; j++) {
                free(b->str[j]);
            }
            qbe_vfree(b->str);
        }
    }
}
// Modification END

uint32_t
qbe_intern(char *s)
{
	Bucket *b;
	uint32_t h;
	uint i, n;

	h = qbe_hash(s) & IMask;
	b = &itbl[h];
	n = b->nstr;

	for (i=0; i<n; i++)
		if (strcmp(s, b->str[i]) == 0)
			return h + (i<<IBits);

	if (n == 1<<(32-IBits))
		die("interning table overflow");
	if (n == 0)
		b->str = qbe_vnew(1, sizeof b->str[0], PHeap);
	else if ((n & (n-1)) == 0)
		qbe_vgrow(&b->str, n+n);

	b->str[n] = qbe_emalloc(strlen(s)+1);
	b->nstr = n + 1;
	strcpy(b->str[n], s);
	return h + (n<<IBits);
}

char *
qbe_str(uint32_t id)
{
	assert(id>>IBits < itbl[id&IMask].nstr);
	return itbl[id&IMask].str[id>>IBits];
}

int
qbe_isreg(Ref r)
{
	return rtype(r) == RTmp && r.val < Tmp0;
}

int
qbe_iscmp(int op, int *pk, int *pc)
{
	if (Ocmpw <= op && op <= Ocmpw1) {
		*pc = op - Ocmpw;
		*pk = Kw;
	}
	else if (Ocmpl <= op && op <= Ocmpl1) {
		*pc = op - Ocmpl;
		*pk = Kl;
	}
	else if (Ocmps <= op && op <= Ocmps1) {
		*pc = NCmpI + op - Ocmps;
		*pk = Ks;
	}
	else if (Ocmpd <= op && op <= Ocmpd1) {
		*pc = NCmpI + op - Ocmpd;
		*pk = Kd;
	}
	else
		return 0;
	return 1;
}

int
qbe_argcls(Ins *i, int n)
{
	return qbe_optab[i->op].argcls[n][i->cls];
}

void
qbe_emit(int op, int k, Ref to, Ref arg0, Ref arg1)
{
	if (qbe_curi == qbe_insb)
		die("emit, too many instructions");
	*--qbe_curi = (Ins){
		.op = op, .cls = k,
		.to = to, .arg = {arg0, arg1}
	};
}

void
qbe_emiti(Ins i)
{
	qbe_emit(i.op, i.cls, i.to, i.arg[0], i.arg[1]);
}

void
qbe_idup(Ins **pd, Ins *s, ulong n)
{
	*pd = qbe_alloc(n * sizeof(Ins));
	if (n)
		memcpy(*pd, s, n * sizeof(Ins));
}

Ins *
qbe_icpy(Ins *d, Ins *s, ulong n)
{
	if (n)
		memcpy(d, s, n * sizeof(Ins));
	return d + n;
}

static int cmptab[][2] ={
	             /* negation    swap */
	[Ciule]      = {Ciugt,      Ciuge},
	[Ciult]      = {Ciuge,      Ciugt},
	[Ciugt]      = {Ciule,      Ciult},
	[Ciuge]      = {Ciult,      Ciule},
	[Cisle]      = {Cisgt,      Cisge},
	[Cislt]      = {Cisge,      Cisgt},
	[Cisgt]      = {Cisle,      Cislt},
	[Cisge]      = {Cislt,      Cisle},
	[Cieq]       = {Cine,       Cieq},
	[Cine]       = {Cieq,       Cine},
	[NCmpI+Cfle] = {NCmpI+Cfgt, NCmpI+Cfge},
	[NCmpI+Cflt] = {NCmpI+Cfge, NCmpI+Cfgt},
	[NCmpI+Cfgt] = {NCmpI+Cfle, NCmpI+Cflt},
	[NCmpI+Cfge] = {NCmpI+Cflt, NCmpI+Cfle},
	[NCmpI+Cfeq] = {NCmpI+Cfne, NCmpI+Cfeq},
	[NCmpI+Cfne] = {NCmpI+Cfeq, NCmpI+Cfne},
	[NCmpI+Cfo]  = {NCmpI+Cfuo, NCmpI+Cfo},
	[NCmpI+Cfuo] = {NCmpI+Cfo,  NCmpI+Cfuo},
};

int
qbe_cmpneg(int c)
{
	assert(0 <= c && c < NCmp);
	return cmptab[c][0];
}

int
qbe_cmpop(int c)
{
	assert(0 <= c && c < NCmp);
	return cmptab[c][1];
}

int
qbe_clsmerge(short *pk, short k)
{
	short k1;

	k1 = *pk;
	if (k1 == Kx) {
		*pk = k;
		return 0;
	}
	if ((k1 == Kw && k == Kl) || (k1 == Kl && k == Kw)) {
		*pk = Kw;
		return 0;
	}
	return k1 != k;
}

int
qbe_phicls(int t, Tmp *tmp)
{
	int t1;

	t1 = tmp[t].phi;
	if (!t1)
		return t;
	t1 = qbe_phicls(t1, tmp);
	tmp[t].phi = t1;
	return t1;
}

Ref
qbe_newtmp(char *prfx, int k,  Fn *fn)
{
	static int n;
	int t;

	t = fn->ntmp++;
	qbe_vgrow(&fn->tmp, fn->ntmp);
	memset(&fn->tmp[t], 0, sizeof(Tmp));
	if (prfx)
		qbe_strf(fn->tmp[t].name, "%s.%d", prfx, ++n);
	fn->tmp[t].cls = k;
	fn->tmp[t].slot = -1;
	fn->tmp[t].nuse = +1;
	fn->tmp[t].ndef = +1;
	return TMP(t);
}

void
qbe_chuse(Ref r, int du, Fn *fn)
{
	if (rtype(r) == RTmp)
		fn->tmp[r.val].nuse += du;
}

int
qbe_symeq(Sym s0, Sym s1)
{
	return s0.type == s1.type && s0.id == s1.id;
}

Ref
qbe_newcon(Con *c0, Fn *fn)
{
	Con *c1;
	int i;

	for (i=1; i<fn->ncon; i++) {
		c1 = &fn->con[i];
		if (c0->type == c1->type
		&& qbe_symeq(c0->sym, c1->sym)
		&& c0->bits.i == c1->bits.i)
			return CON(i);
	}
	qbe_vgrow(&fn->con, ++fn->ncon);
	fn->con[i] = *c0;
	return CON(i);
}

Ref
qbe_getcon(int64_t val, Fn *fn)
{
	int c;

	for (c=1; c<fn->ncon; c++)
		if (fn->con[c].type == CBits
		&& fn->con[c].bits.i == val)
			return CON(c);
	qbe_vgrow(&fn->con, ++fn->ncon);
	fn->con[c] = (Con){.type = CBits, .bits.i = val};
	return CON(c);
}

int
qbe_addcon(Con *c0, Con *c1)
{
	if (c0->type == CUndef)
		*c0 = *c1;
	else {
		if (c1->type == CAddr) {
			if (c0->type == CAddr)
				return 0;
			c0->type = CAddr;
			c0->sym = c1->sym;
		}
		c0->bits.i += c1->bits.i;
	}
	return 1;
}

void
qbe_salloc(Ref rt, Ref rs, Fn *fn)
{
	Ref r0, r1;
	int64_t sz;

	/* we need to make sure
	 * the stack remains aligned
	 * (rsp = 0) mod 16
	 */
	fn->dynalloc = 1;
	if (rtype(rs) == RCon) {
		sz = fn->con[rs.val].bits.i;
		if (sz < 0 || sz >= INT_MAX-15)
			qbe_err("invalid alloc size %"PRId64, sz);
		sz = (sz + 15)  & -16;
		qbe_emit(Osalloc, Kl, rt, qbe_getcon(sz, fn), R);
	} else {
		/* r0 = (r + 15) & -16 */
		r0 = qbe_newtmp("isel", Kl, fn);
		r1 = qbe_newtmp("isel", Kl, fn);
		qbe_emit(Osalloc, Kl, rt, r0, R);
		qbe_emit(Oand, Kl, r0, r1, qbe_getcon(-16, fn));
		qbe_emit(Oadd, Kl, r1, rs, qbe_getcon(15, fn));
		if (fn->tmp[rs.val].slot != -1)
			qbe_err("unlikely alloc argument %%%s for %%%s",
				fn->tmp[rs.val].name, fn->tmp[rt.val].name);
	}
}

void
qbe_bsinit(BSet *bs, uint n)
{
	n = (n + NBit-1) / NBit;
	bs->nt = n;
	bs->t = qbe_alloc(n * sizeof bs->t[0]);
}

MAKESURE(NBit_is_64, NBit == 64);
inline static uint
popcnt(bits b)
{
	b = (b & 0x5555555555555555) + ((b>>1) & 0x5555555555555555);
	b = (b & 0x3333333333333333) + ((b>>2) & 0x3333333333333333);
	b = (b & 0x0f0f0f0f0f0f0f0f) + ((b>>4) & 0x0f0f0f0f0f0f0f0f);
	b += (b>>8);
	b += (b>>16);
	b += (b>>32);
	return b & 0xff;
}

inline static int
firstbit(bits b)
{
	int n;

	n = 0;
	if (!(b & 0xffffffff)) {
		n += 32;
		b >>= 32;
	}
	if (!(b & 0xffff)) {
		n += 16;
		b >>= 16;
	}
	if (!(b & 0xff)) {
		n += 8;
		b >>= 8;
	}
	if (!(b & 0xf)) {
		n += 4;
		b >>= 4;
	}
	n += (char[16]){4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0}[b & 0xf];
	return n;
}

uint
qbe_bscount(BSet *bs)
{
	uint i, n;

	n = 0;
	for (i=0; i<bs->nt; i++)
		n += popcnt(bs->t[i]);
	return n;
}

static inline uint
bsmax(BSet *bs)
{
	return bs->nt * NBit;
}

void
qbe_bsset(BSet *bs, uint elt)
{
	assert(elt < bsmax(bs));
	bs->t[elt/NBit] |= BIT(elt%NBit);
}

void
qbe_bsclr(BSet *bs, uint elt)
{
	assert(elt < bsmax(bs));
	bs->t[elt/NBit] &= ~BIT(elt%NBit);
}

#define BSOP(f, op)                           \
	void                                  \
	f(BSet *a, BSet *b)                   \
	{                                     \
		uint i;                       \
		                              \
		assert(a->nt == b->nt);       \
		for (i=0; i<a->nt; i++)       \
			a->t[i] op b->t[i];   \
	}

BSOP(qbe_bscopy, =)
BSOP(qbe_bsunion, |=)
BSOP(qbe_bsinter, &=)
BSOP(qbe_bsdiff, &= ~)

int
qbe_bsequal(BSet *a, BSet *b)
{
	uint i;

	assert(a->nt == b->nt);
	for (i=0; i<a->nt; i++)
		if (a->t[i] != b->t[i])
			return 0;
	return 1;
}

void
qbe_bszero(BSet *bs)
{
	memset(bs->t, 0, bs->nt * sizeof bs->t[0]);
}

/* iterates on a bitset, use as follows
 *
 * 	for (i=0; bsiter(set, &i); i++)
 * 		use(i);
 *
 */
int
qbe_bsiter(BSet *bs, int *elt)
{
	bits b;
	uint t, i;

	i = *elt;
	t = i/NBit;
	if (t >= bs->nt)
		return 0;
	b = bs->t[t];
	b &= ~(BIT(i%NBit) - 1);
	while (!b) {
		++t;
		if (t >= bs->nt)
			return 0;
		b = bs->t[t];
	}
	*elt = NBit*t + firstbit(b);
	return 1;
}

void
qbe_dumpts(BSet *bs, Tmp *tmp, FILE *f)
{
	int t;

	fprintf(f, "[");
	for (t=Tmp0; qbe_bsiter(bs, &t); t++)
		fprintf(f, " %s", tmp[t].name);
	fprintf(f, " ]\n");
}
