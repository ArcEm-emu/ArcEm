


__r0	RN	0
__r1	RN	1
__r2	RN	2
__r3	RN	3
__r4	RN	4
__r5	RN	5
__r6	RN	6
__r7	RN	7
__r8	RN	8
__r9	RN	9
__r10	RN	10
__r11	RN	11
__r12	RN	12
__r13	RN	13
__r14	RN	14
__r15	RN	15
__a1	RN	0
__a2	RN	1
__a3	RN	2
__a4	RN	3
__v1	RN	4
__v2	RN	5
__v3	RN	6
__v4	RN	7
__v5	RN	8
__v6	RN	9
__sl	RN	10
__fp	RN	11
__ip	RN	12
__sp	RN	13
__lr	RN	14
__pc	RN	15
__f0	FN	0
__f1	FN	1
__f2	FN	2
__f3	FN	3
__f4	FN	4
__f5	FN	5
__f6	FN	6
__f7	FN	7


	AREA |C$$code1|, CODE, READONLY

	IMPORT	|GetDPRegRHS|
	IMPORT	|GetDPSRegRHS|

|statevars|
	DCD	|statestr|
	DCD	|emu_state|


#include "armsuppmov.s"

	mov	__a1, __a1, lsr #10
	and	__a1, __a1, #60
	add	__a4, __a4, #8
	str	__a2, [__a4, __a1]

	movs	__pc, __lr

|ARMul_Emulate26_MovRegNorm_HasShift|
	stmfd	__sp!, {__v1, __lr}

	mov	__v1, __a1
	ldr	__a1, |statevars|+4
	ldr	__a1, [__a1, #0]
	mov	__a2, __v1
	bl	|GetDPRegRHS|
	ldr	__a4, |statevars|

	mov	__a2, __v1, lsr #10
	and	__a2, __a2, #60
	add	__a3, __a4, #8
	str	__a1, [__a3, __a2]

	ldmia	__sp!, {__v1, __pc}^



#include "armsuppmovs.s"

	mov	__a1, __a1, lsr #10
	and	__a1, __a1, #60
	add	__a4, __a4, #8
	str	__a2, [__a4, __a1]

	movs	__a3, __a2, lsr #31
	beq	|ARMul_Emulate26_MovsRegNorm_Status1|
	add	__a4, __a4, #424 - 8
	mov	__a1, #1
	mov	__a2, #0
	stmia	__a4, {__a1, __a2}
	movs	__pc, __lr

|ARMul_Emulate26_MovsRegNorm_Status1|
	cmp	__a2, #0
	streq	__a3, [__a4, #424 - 8]
	moveq	__a3, #1
	streq	__a3, [__a4, #428 - 8]

	strne	__a3, [__a4, #424 - 8]
	strne	__a3, [__a4, #428 - 8]
	movs	__pc, __lr

|ARMul_Emulate26_MovsRegNorm_HasShift|
	stmfd	__sp!, {__v1, __lr}

	mov	__v1, __a1
	ldr	__a1, |statevars|+4
	ldr	__a1, [__a1, #0]
	mov	__a2, __v1
	bl	|GetDPSRegRHS|
	ldr	__a4, |statevars|

	mov	__a2, __v1, lsr #10
	and	__a2, __a2, #60
	add	__a3, __a4, #8
	str	__a1, [__a3, __a2]

	movs	__v1, __a1, lsr #31
	beq	|ARMul_Emulate26_MovsRegNorm_Status2|
	add	__a4, __a3, #424 - 8

	mov	__a1, #1
	mov	__a2, #0
	stmia	__a4, {__a1, __a2}
	ldmia	__sp!, {__v1, __pc}^

|ARMul_Emulate26_MovsRegNorm_Status2|
	cmp	__a1, #0
	streq	__v1, [__a4, #424]
	moveq	__v1, #1
	streq	__v1, [__a4, #428]

	strne	__v1, [__a4, #424]
	strne	__v1, [__a4, #428]

	ldmia	__sp!, {__v1, __pc}^



#include "armsuppmvn.s"

	mvn	__a2, __a2

	mov	__a1, __a1, lsr #10
	and	__a1, __a1, #60
	add	__a4, __a4, #8
	str	__a2, [__a4, __a1]

	movs	__pc, __lr

|ARMul_Emulate26_MvnRegNorm_HasShift|
	stmfd	__sp!, {__v1, __lr}

	mov	__v1, __a1
	ldr	__a1, |statevars|+4
	ldr	__a1, [__a1, #0]
	mov	__a2, __v1
	bl	|GetDPRegRHS|
	mvn	__a1, __a1
	ldr	__a4, |statevars|

	mov	__a2, __v1, lsr #10
	and	__a2, __a2, #60
	add	__a3, __a4, #8
	str	__a1, [__a3, __a2]

	ldmia	__sp!, {__v1, __pc}^



#include "armsuppmvns.s"

	mvn	__a2, __a2		; MVN

	mov	__a1, __a1, lsr #10
	and	__a1, __a1, #60
	add	__a4, __a4, #8
	str	__a2, [__a4, __a1]

	movs	__a3, __a2, lsr #31
	beq	|ARMul_Emulate26_MvnsRegNorm_Status1|
	add	__a4, __a4, #424 - 8
	mov	__a1, #1
	mov	__a2, #0
	stmia	__a4, {__a1, __a2}
	movs	__pc, __lr

|ARMul_Emulate26_MvnsRegNorm_Status1|
	cmp	__a2, #0
	streq	__a3, [__a4, #424 - 8]
	moveq	__a3, #1
	streq	__a3, [__a4, #428 - 8]

	strne	__a3, [__a4, #424 - 8]
	strne	__a3, [__a4, #428 - 8]
	movs	__pc, __lr

|ARMul_Emulate26_MvnsRegNorm_HasShift|
	stmfd	__sp!, {__v1, __lr}

	mov	__v1, __a1
	ldr	__a1, |statevars|+4
	ldr	__a1, [__a1, #0]
	mov	__a2, __v1
	bl	|GetDPSRegRHS|
	mvn	__a1, __a1		; MVN
	ldr	__a4, |statevars|

	mov	__a2, __v1, lsr #10
	and	__a2, __a2, #60
	add	__a3, __a4, #8
	str	__a1, [__a3, __a2]

	movs	__v1, __a1, lsr #31
	beq	|ARMul_Emulate26_MvnsRegNorm_Status2|
	add	__a4, __a3, #424 - 8

	mov	__a1, #1
	mov	__a2, #0
	stmia	__a4, {__a1, __a2}
	ldmia	__sp!, {__v1, __pc}^

|ARMul_Emulate26_MvnsRegNorm_Status2|
	cmp	__a1, #0
	streq	__v1, [__a4, #424]
	moveq	__v1, #1
	streq	__v1, [__a4, #428]

	strne	__v1, [__a4, #424]
	strne	__v1, [__a4, #428]

	ldmia	__sp!, {__v1, __pc}^


