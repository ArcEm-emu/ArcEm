
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_EQ|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_NE|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_CS|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_CC|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_VS|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_VC|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_MI|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_PL|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_HI|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_LS|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_GE|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_LT|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_GT|
	EXPORT	|ARMul_Emulate26_ARMINSRegNorm_LE|


|ARMul_Emulate26_ARMINSRegNorm_EQ|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #428]
	cmp	__a3, #0
        moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_NE|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #428]
	cmp	__a3, #0
	movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_CS|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #432]
	cmp	__a3, #0
        moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_CC|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #432]
	cmp	__a3, #0
        movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_VS|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #436]
	cmp	__a3, #0
        moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_VC|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #436]
	cmp	__a3, #0
        movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_MI|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	cmp	__a3, #0
        moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_PL|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	cmp	__a3, #0
        movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_HI|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #432]
	cmp	__a3, #0
        moveqs	__pc, __lr
	ldr	__a3, [ __a2, #428]
	cmp	__a3, #0
        movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_LS|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #432]
	cmp	__a3, #0
        beq	|ARMul_Emulate26_ARMINSRegNorm|
	ldr	__a3, [ __a2, #428]
	cmp	__a3, #0
        moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_GE|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	ldr	__a4, [ __a2, #436]
	cmp	__a3, #0
	bne	|ARMul_Emulate26_ARMINSRegNorm_GE_1|
	cmp	__a4, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm|
|ARMul_Emulate26_ARMINSRegNorm_GE_1|
	cmp	__a3, #0
	moveqs	__pc, __lr
	cmp	__a4, #0
	moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_LT|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	ldr	__a4, [ __a2, #436]
	cmp	__a3, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm_LT_1|
	cmp	__a4, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm|
|ARMul_Emulate26_ARMINSRegNorm_LT_1|
	cmp	__a3, #0
	movnes	__pc, __lr
	cmp	__a4, #0
	moveqs	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_GT|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	ldr	__a4, [ __a2, #428]
	ldr	__a2, [ __a2, #436]
	cmp	__a3, #0
	bne	|ARMul_Emulate26_ARMINSRegNorm_GT_1|
	cmp	__a2, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm_GT_2|
|ARMul_Emulate26_ARMINSRegNorm_GT_1|
	cmp	__a3, #0
	moveqs	__pc, __lr
	cmp	__a2, #0
	moveqs	__pc, __lr
|ARMul_Emulate26_ARMINSRegNorm_GT_2|
	cmp	__a4, #0
	movnes	__pc, __lr
	B	|ARMul_Emulate26_ARMINSRegNorm|

|ARMul_Emulate26_ARMINSRegNorm_LE|
	ldr	__a2, |statevars|
	ldr	__a3, [ __a2, #424]
	ldr	__a4, [ __a2, #428]
	ldr	__a2, [ __a2, #436]
	cmp	__a3, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm_LE_1|
	cmp	__a2, #0
	beq	|ARMul_Emulate26_ARMINSRegNorm|
|ARMul_Emulate26_ARMINSRegNorm_LE_1|
	cmp	__a3, #0
	bne	|ARMul_Emulate26_ARMINSRegNorm_LE_2|
	cmp	__a2, #0
	bne	|ARMul_Emulate26_ARMINSRegNorm|
|ARMul_Emulate26_ARMINSRegNorm_LE_2|
	cmp	__a4, #0
	moveqs	__pc, __lr
	; fall through


|ARMul_Emulate26_ARMINSRegNorm|
	mov	__a2, __a1, asl #20
	mov	__a2, __a2, lsr #20
	cmp	__a2, #14
	bhi	|ARMul_Emulate26_ARMINSRegNorm_HasShift|

	ldr	__a4, |statevars|
	and	__a3, __a1, #15
	add	__a2, __a4, #8
	ldr	__a2, [__a2, __a3, asl #2]




