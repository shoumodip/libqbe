#include "../all.h"

typedef struct Rv64Op Rv64Op;

enum Rv64Reg {
	/* caller-save */
	T0 = RXX + 1, T1, T2, T3, T4, T5,
	A0, A1, A2, A3, A4, A5, A6, A7,

	/* callee-save */
	S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11,

	/* globally live */
	FP, SP, GP, TP, RA,

	/* FP caller-save */
	FT0, FT1, FT2, FT3, FT4, FT5, FT6, FT7, FT8, FT9, FT10,
	FA0, FA1, FA2, FA3, FA4, FA5, FA6, FA7,

	/* FP callee-save */
	FS0, FS1, FS2, FS3, FS4, FS5, FS6, FS7, FS8, FS9, FS10, FS11,

	/* reserved (see rv64/emit.c) */
	T6, FT11,

	NFPR = FS11 - FT0 + 1,
	NGPR = RA - T0 + 1,
	NGPS = A7 - T0 + 1,
	NFPS = FA7 - FT0 + 1,
	NCLR = (S11 - S1 + 1) + (FS11 - FS0 + 1),
};
MAKESURE(reg_not_tmp, FT11 < (int)Tmp0);

struct Rv64Op {
	char imm;
};

/* targ.c */
extern int qbe_rv64_rsave[];
extern int qbe_rv64_rclob[];
extern Rv64Op qbe_rv64_op[];

/* abi.c */
bits qbe_rv64_retregs(Ref, int[2]);
bits qbe_rv64_argregs(Ref, int[2]);
void qbe_rv64_abi(Fn *);

/* isel.c */
void qbe_rv64_isel(Fn *);

/* emit.c */
void qbe_rv64_emitfn(Fn *, FILE *);
