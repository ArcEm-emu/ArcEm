


@ r0	RN	0
@ r1	RN	1
@ r2	RN	2
@ r3	RN	3
@ r4	RN	4
@ r5	RN	5
@ r6	RN	6
@ r7	RN	7
@ r8	RN	8
@ r9	RN	9
@ r10	RN	10
@ r11	RN	11
@ r12	RN	12
@ r13	RN	13
@ r14	RN	14
@ r15	RN	15
@ a1	RN	0
@ a2	RN	1
@ a3	RN	2
@ a4	RN	3
@ v1	RN	4
@ v2	RN	5
@ v3	RN	6
@ v4	RN	7
@ v5	RN	8
@ v6	RN	9
@ sl	RN	10
@ fp	RN	11
@ ip	RN	12
@ sp	RN	13
@ lr	RN	14
@ pc	RN	15
@ f0	FN	0
@ f1	FN	1
@ f2	FN	2
@ f3	FN	3
@ f4	FN	4
@ f5	FN	5
@ f6	FN	6
@ f7	FN	7


.text

statevars:
.word	statestr
.word	emu_state


#include "armsuppmov.s"

	mov	a1, a1, lsr #10
	and	a1, a1, #60
	add	a4, a4, #8
	str	a2, [a4, a1]

	mov	pc, lr

ARMul_Emulate26_MovRegNorm_HasShift:
	stmfd	sp!, {v1, lr}

	mov	v1, a1
	ldr	a1, statevars+4
	ldr	a1, [a1, #0]
	mov	a2, v1
	bl	GetDPRegRHS
	ldr	a4, statevars

	mov	a2, v1, lsr #10
	and	a2, a2, #60
	add	a3, a4, #8
	str	a1, [a3, a2]

	ldmia	sp!, {v1, pc}



#include "armsuppmovs.s"

	mov	a1, a1, lsr #10
	and	a1, a1, #60
	add	a4, a4, #8
	str	a2, [a4, a1]

	movs	a3, a2, lsr #31
	beq	ARMul_Emulate26_MovsRegNorm_Status1
	add	a4, a4, #424 - 8
	mov	a1, #1
	mov	a2, #0
	stmia	a4, {a1, a2}
	mov	pc, lr

ARMul_Emulate26_MovsRegNorm_Status1:
	cmp	a2, #0
	streq	a3, [a4, #424 - 8]
	moveq	a3, #1
	streq	a3, [a4, #428 - 8]

	strne	a3, [a4, #424 - 8]
	strne	a3, [a4, #428 - 8]
	mov	pc, lr

ARMul_Emulate26_MovsRegNorm_HasShift:
	stmfd	sp!, {v1, lr}

	mov	v1, a1
	ldr	a1, statevars+4
	ldr	a1, [a1, #0]
	mov	a2, v1
	bl	GetDPSRegRHS
	ldr	a4, statevars

	mov	a2, v1, lsr #10
	and	a2, a2, #60
	add	a3, a4, #8
	str	a1, [a3, a2]

	movs	v1, a1, lsr #31
	beq	ARMul_Emulate26_MovsRegNorm_Status2
	add	a4, a3, #424 - 8

	mov	a1, #1
	mov	a2, #0
	stmia	a4, {a1, a2}
	ldmia	sp!, {v1, pc}

ARMul_Emulate26_MovsRegNorm_Status2:
	cmp	a1, #0
	streq	v1, [a4, #424]
	moveq	v1, #1
	streq	v1, [a4, #428]

	strne	v1, [a4, #424]
	strne	v1, [a4, #428]

	ldmia	sp!, {v1, pc}



#include "armsuppmvn.s"

	mvn	a2, a2

	mov	a1, a1, lsr #10
	and	a1, a1, #60
	add	a4, a4, #8
	str	a2, [a4, a1]

	mov	pc, lr

ARMul_Emulate26_MvnRegNorm_HasShift:
	stmfd	sp!, {v1, lr}

	mov	v1, a1
	ldr	a1, statevars+4
	ldr	a1, [a1, #0]
	mov	a2, v1
	bl	GetDPRegRHS
	mvn	a1, a1
	ldr	a4, statevars

	mov	a2, v1, lsr #10
	and	a2, a2, #60
	add	a3, a4, #8
	str	a1, [a3, a2]

	ldmia	sp!, {v1, pc}



#include "armsuppmvns.s"

	mvn	a2, a2		@ MVN

	mov	a1, a1, lsr #10
	and	a1, a1, #60
	add	a4, a4, #8
	str	a2, [a4, a1]

	movs	a3, a2, lsr #31
	beq	ARMul_Emulate26_MvnsRegNorm_Status1
	add	a4, a4, #424 - 8
	mov	a1, #1
	mov	a2, #0
	stmia	a4, {a1, a2}
	mov	pc, lr

ARMul_Emulate26_MvnsRegNorm_Status1:
	cmp	a2, #0
	streq	a3, [a4, #424 - 8]
	moveq	a3, #1
	streq	a3, [a4, #428 - 8]

	strne	a3, [a4, #424 - 8]
	strne	a3, [a4, #428 - 8]
	mov	pc, lr

ARMul_Emulate26_MvnsRegNorm_HasShift:
	stmfd	sp!, {v1, lr}

	mov	v1, a1
	ldr	a1, statevars+4
	ldr	a1, [a1, #0]
	mov	a2, v1
	bl	GetDPSRegRHS
	mvn	a1, a1		@ MVN
	ldr	a4, statevars

	mov	a2, v1, lsr #10
	and	a2, a2, #60
	add	a3, a4, #8
	str	a1, [a3, a2]

	movs	v1, a1, lsr #31
	beq	ARMul_Emulate26_MvnsRegNorm_Status2
	add	a4, a3, #424 - 8

	mov	a1, #1
	mov	a2, #0
	stmia	a4, {a1, a2}
	ldmia	sp!, {v1, pc}

ARMul_Emulate26_MvnsRegNorm_Status2:
	cmp	a1, #0
	streq	v1, [a4, #424]
	moveq	v1, #1
	streq	v1, [a4, #428]

	strne	v1, [a4, #424]
	strne	v1, [a4, #428]

	ldmia	sp!, {v1, pc}


