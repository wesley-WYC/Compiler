#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "symbol.h"
#include "temp.h"
#include "table.h"
#include "tree.h"
#include "frame.h"

/*Lab5: Your implementation here.*/

const int F_WORD_SIZE = 4; /* 4 byte */
static const int F_MAX_REG = 6;  /* paras in regs number */ 

struct F_frame_ {
	Temp_label name;
	F_accessList formals;
	int local_count;/*local-var numb*/
};


struct F_access_ {
	enum {inFrame, inReg} kind;
	union {
		int offs;
		Temp_temp reg;
	} u;
};/* describe area-var and reg */

static F_access InFrame(int offs);
static F_access InReg(Temp_temp reg);
static F_accessList makeFormalAccessList(F_frame, U_boolList);
static F_accessList F_AccessList(F_access, F_accessList);

F_frame F_newFrame(Temp_label name, U_boolList formals) {
	F_frame f = checked_malloc(sizeof(*f));
	f->name = name;
	f->formals = makeFormalAccessList(f, formals);
	f->local_count = 0;
	return f;
}

F_access F_allocLocal(F_frame f, bool escape) {
	/* give a not-paras-var alloc space just is in let-in-end-exp decl a var */
	f->local_count++;
	if (escape) {
		return InFrame(-1 * F_WORD_SIZE * f->local_count);
	}
	return InReg(Temp_newtemp());
}

F_accessList F_formals(F_frame f) {
	return f->formals;
}

static F_accessList makeFormalAccessList(F_frame f, U_boolList formals) {
	U_boolList fmls = formals;
	F_accessList head = NULL, tail = NULL;
	int i = 0;
	for (; fmls; fmls = fmls->tail, i++) {
		F_access ac = NULL;
		if (i < F_MAX_REG && !fmls->head) {
			ac = InReg(Temp_newtemp());
		} else {
			/*keep a space for return*/
			ac = InFrame((i/* + 1*/) * F_WORD_SIZE);
		}
		if (head) {
			tail->tail = F_AccessList(ac, NULL);
			tail = tail->tail;
		} else {
			head = F_AccessList(ac, NULL);
			tail = head;
		}
	}
	return head;
}

static F_accessList F_AccessList(F_access head, F_accessList tail) {
	F_accessList l = checked_malloc(sizeof(*l));
	l->head = head;
	l->tail = tail;
	return l;
}

static F_access InFrame(int offs) {
	F_access a = checked_malloc(sizeof(*a));
	a->kind = inFrame;
	a->u.offs = offs;
	return a;
}

static F_access InReg(Temp_temp t) {
	F_access a = checked_malloc(sizeof(*a));
	a->kind = inReg;
	a->u.reg = t;
	return a;
}

/*******IR*******/
F_frag F_StringFrag(Temp_label label, string str) {
	F_frag strfrag = checked_malloc(sizeof(*strfrag));
	strfrag->kind = F_stringFrag;
	strfrag->u.stringg.label = label;
	strfrag->u.stringg.str = str;
	return strfrag;
}

F_frag F_ProcFrag(T_stm body, F_frame frame) {
	F_frag pfrag = checked_malloc(sizeof(*pfrag));
	pfrag->kind = F_procFrag;
	pfrag->u.proc.body = body;
	pfrag->u.proc.frame = frame;
	return pfrag;
}

F_fragList F_FragList(F_frag head, F_fragList tail) {
	F_fragList fl = checked_malloc(sizeof(*fl));
	fl->head = head;
	fl->tail = tail;
	return fl;
}

#define INIT_REG(Reg, Name) (Reg ? Reg : (Reg = Temp_newtemp(), Temp_enter(F_tempMap, Reg, Name), Reg))

static Temp_temp fp = NULL;
Temp_temp F_FP() { return INIT_REG(fp, "ebp"); } 

static Temp_temp sp = NULL;
Temp_temp F_SP() { return INIT_REG(fp, "esp"); }

static Temp_temp rv = NULL; 
Temp_temp F_RV() { return INIT_REG(rv, "eax"); }

static Temp_temp ra = NULL;
Temp_temp F_RA() { return INIT_REG(ra, "---"); }

static Temp_tempList callersaves() 
{
	/* assist-function of calldefs() */

	Temp_temp ebx = Temp_newtemp(),
			  ecx = Temp_newtemp(),
			  edx = Temp_newtemp(),
			  edi = Temp_newtemp(),
			  esi = Temp_newtemp();
	Temp_enter(F_tempMap, ebx, "ebx");
	Temp_enter(F_tempMap, ecx, "ecx");
	Temp_enter(F_tempMap, edx, "edx");
	Temp_enter(F_tempMap, edi, "edi");
	Temp_enter(F_tempMap, esi, "esi");
	return Temp_TempList(F_RV(), Temp_TempList(ebx, Temp_TempList(ecx, Temp_TempList(edx, Temp_TempList(edi, Temp_TempList(esi, NULL))))));
}

static Temp_tempList sepecialregs()
{
    static Temp_tempList spcregs = NULL;
    if (!spcregs) spcregs = Temp_TempList(F_SP(), Temp_TempList(F_FP(), Temp_TempList(F_RV(), NULL)));
    return spcregs;
}

/*-short argsregs, because pass arg by stack*/

static Temp_tempList calleesaves() 
{   
    /* callee protect sp, fp, ebx */
    static Temp_tempList calleeregs = NULL;
    if (!calleeregs) {
        Temp_temp ebx = Temp_newtemp();
        Temp_enter(F_tempMap, ebx, "ebx");
        calleeregs = Temp_TempList(F_SP(), Temp_TempList(F_FP(), Temp_TempList(ebx, NULL)));
    }
    return calleeregs;
}


Temp_tempList F_calldefs() 
{
	/* some registers that may raise side-effect (caller procted, return-val-reg, return-addr-reg) */
	static Temp_tempList protected_regs = NULL;
	return protected_regs ? protected_regs : (protected_regs = callersaves());
}

T_exp F_Exp(F_access access, T_exp framePtr){ /* visit frame-offs addr & get content */
	if (access->kind == inFrame) {
		return T_Mem(T_Binop(T_plus, framePtr, T_Const(access->u.offs)));
	} else {
		return T_Temp(access->u.reg);
	}
}

T_exp F_externalCall(string str, T_expList args) {
	return T_Call(T_Name(Temp_namedlabel(str)), args);
}

T_stm F_procEntryExit1(F_frame frame, T_stm stm) {
	return stm;
}

