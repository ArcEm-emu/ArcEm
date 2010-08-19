
.global ARMul_Emulate26_ARMINSRegNorm
.global ARMul_Emulate26_ARMINSRegNorm_EQ
.global ARMul_Emulate26_ARMINSRegNorm_NE
.global ARMul_Emulate26_ARMINSRegNorm_CS
.global ARMul_Emulate26_ARMINSRegNorm_CC
.global ARMul_Emulate26_ARMINSRegNorm_VS
.global ARMul_Emulate26_ARMINSRegNorm_VC
.global ARMul_Emulate26_ARMINSRegNorm_MI
.global ARMul_Emulate26_ARMINSRegNorm_PL
.global ARMul_Emulate26_ARMINSRegNorm_HI
.global ARMul_Emulate26_ARMINSRegNorm_LS
.global ARMul_Emulate26_ARMINSRegNorm_GE
.global ARMul_Emulate26_ARMINSRegNorm_LT
.global ARMul_Emulate26_ARMINSRegNorm_GT
.global ARMul_Emulate26_ARMINSRegNorm_LE


ARMul_Emulate26_ARMINSRegNorm_EQ:
	ldr	a2, statevars
	ldr	a3, [ a2, #428]
	cmp	a3, #0
        moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_NE:
	ldr	a2, statevars
	ldr	a3, [ a2, #428]
	cmp	a3, #0
	movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_CS:
	ldr	a2, statevars
	ldr	a3, [ a2, #432]
	cmp	a3, #0
        moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_CC:
	ldr	a2, statevars
	ldr	a3, [ a2, #432]
	cmp	a3, #0
        movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_VS:
	ldr	a2, statevars
	ldr	a3, [ a2, #436]
	cmp	a3, #0
        moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_VC:
	ldr	a2, statevars
	ldr	a3, [ a2, #436]
	cmp	a3, #0
        movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_MI:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	cmp	a3, #0
        moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_PL:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	cmp	a3, #0
        movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_HI:
	ldr	a2, statevars
	ldr	a3, [ a2, #432]
	cmp	a3, #0
        moveq	pc, lr
	ldr	a3, [ a2, #428]
	cmp	a3, #0
        movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_LS:
	ldr	a2, statevars
	ldr	a3, [ a2, #432]
	cmp	a3, #0
        beq	ARMul_Emulate26_ARMINSRegNorm
	ldr	a3, [ a2, #428]
	cmp	a3, #0
        moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_GE:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	ldr	a4, [ a2, #436]
	cmp	a3, #0
	bne	ARMul_Emulate26_ARMINSRegNorm_GE_1
	cmp	a4, #0
	beq	ARMul_Emulate26_ARMINSRegNorm
ARMul_Emulate26_ARMINSRegNorm_GE_1:
	cmp	a3, #0
	moveq	pc, lr
	cmp	a4, #0
	moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_LT:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	ldr	a4, [ a2, #436]
	cmp	a3, #0
	beq	ARMul_Emulate26_ARMINSRegNorm_LT_1
	cmp	a4, #0
	beq	ARMul_Emulate26_ARMINSRegNorm
ARMul_Emulate26_ARMINSRegNorm_LT_1:
	cmp	a3, #0
	movne	pc, lr
	cmp	a4, #0
	moveq	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_GT:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	ldr	a4, [ a2, #428]
	ldr	a2, [ a2, #436]
	cmp	a3, #0
	bne	ARMul_Emulate26_ARMINSRegNorm_GT_1
	cmp	a2, #0
	beq	ARMul_Emulate26_ARMINSRegNorm_GT_2
ARMul_Emulate26_ARMINSRegNorm_GT_1:
	cmp	a3, #0
	moveq	pc, lr
	cmp	a2, #0
	moveq	pc, lr
ARMul_Emulate26_ARMINSRegNorm_GT_2:
	cmp	a4, #0
	movne	pc, lr
	B	ARMul_Emulate26_ARMINSRegNorm

ARMul_Emulate26_ARMINSRegNorm_LE:
	ldr	a2, statevars
	ldr	a3, [ a2, #424]
	ldr	a4, [ a2, #428]
	ldr	a2, [ a2, #436]
	cmp	a3, #0
	beq	ARMul_Emulate26_ARMINSRegNorm_LE_1
	cmp	a2, #0
	beq	ARMul_Emulate26_ARMINSRegNorm
ARMul_Emulate26_ARMINSRegNorm_LE_1:
	cmp	a3, #0
	bne	ARMul_Emulate26_ARMINSRegNorm_LE_2
	cmp	a2, #0
	bne	ARMul_Emulate26_ARMINSRegNorm
ARMul_Emulate26_ARMINSRegNorm_LE_2:
	cmp	a4, #0
	moveq	pc, lr
	@ fall through


ARMul_Emulate26_ARMINSRegNorm:
	mov	a2, a1, asl #20
	mov	a2, a2, lsr #20
	cmp	a2, #14
	bhi	ARMul_Emulate26_ARMINSRegNorm_HasShift

	ldr	a4, statevars
	and	a3, a1, #15
	add	a2, a4, #8
	ldr	a2, [a2, a3, asl #2]




