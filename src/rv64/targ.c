#include "all.h"

Rv64Op qbe_rv64_op[NOp] = {
#define O(op, t, x) [O##op] =
#define V(imm) { imm },
#include "../ops.h"
};

int qbe_rv64_rsave[] = {
	T0, T1, T2, T3, T4, T5,
	A0, A1, A2, A3, A4, A5, A6, A7,
	FA0, FA1, FA2,  FA3,  FA4, FA5, FA6, FA7,
	FT0, FT1, FT2,  FT3,  FT4, FT5, FT6, FT7,
	FT8, FT9, FT10,
	-1
};
int qbe_rv64_rclob[] = {
	     S1,  S2,   S3,   S4,  S5,  S6,  S7,
	S8,  S9,  S10,  S11,
	FS0, FS1, FS2,  FS3,  FS4, FS5, FS6, FS7,
	FS8, FS9, FS10, FS11,
	-1
};

#define RGLOB (BIT(FP) | BIT(SP) | BIT(GP) | BIT(TP) | BIT(RA))

static int
rv64_memargs(int op)
{
	(void)op;
	return 0;
}

Target qbe_T_rv64 = {
	.name = "rv64",
	.gpr0 = T0,
	.ngpr = NGPR,
	.fpr0 = FT0,
	.nfpr = NFPR,
	.rglob = RGLOB,
	.nrglob = 5,
	.rsave = qbe_rv64_rsave,
	.nrsave = {NGPS, NFPS},
	.retregs = qbe_rv64_retregs,
	.argregs = qbe_rv64_argregs,
	.memargs = rv64_memargs,
	.abi0 = qbe_elimsb,
	.abi1 = qbe_rv64_abi,
	.isel = qbe_rv64_isel,
	.emitfn = qbe_rv64_emitfn,
	.emitfin = qbe_elf_emitfin,
	.asloc = ".L",
};

MAKESURE(rsave_size_ok, sizeof qbe_rv64_rsave == (NGPS+NFPS+1) * sizeof(int));
MAKESURE(rclob_size_ok, sizeof qbe_rv64_rclob == (NCLR+1) * sizeof(int));
