#include "all.h"

Amd64Op qbe_amd64_op[NOp] = {
#define O(op, t, x) [O##op] =
#define X(nm, zf, lf) { nm, zf, lf, },
	#include "../ops.h"
};

static int
amd64_memargs(int op)
{
	return qbe_amd64_op[op].nmem;
}

#define AMD64_COMMON \
	.gpr0 = RAX, \
	.ngpr = NGPR, \
	.fpr0 = XMM0, \
	.nfpr = NFPR, \
	.rglob = BIT(RBP) | BIT(RSP), \
	.nrglob = 2, \
	.rsave = qbe_amd64_sysv_rsave, \
	.nrsave = {NGPS, NFPS}, \
	.retregs = qbe_amd64_sysv_retregs, \
	.argregs = qbe_amd64_sysv_argregs, \
	.memargs = amd64_memargs, \
	.abi0 = qbe_elimsb, \
	.abi1 = qbe_amd64_sysv_abi, \
	.isel = qbe_amd64_isel, \
	.emitfn = qbe_amd64_emitfn, \

Target qbe_T_amd64_sysv = {
	.name = "amd64_sysv",
	.emitfin = qbe_elf_emitfin,
	.asloc = ".L",
	AMD64_COMMON
};

Target qbe_T_amd64_apple = {
	.name = "amd64_apple",
	.apple = 1,
	.emitfin = qbe_macho_emitfin,
	.asloc = "L",
	.assym = "_",
	AMD64_COMMON
};
