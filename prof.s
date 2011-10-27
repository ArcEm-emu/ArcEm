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

prof_buffer_ptr:
.word	0
prof_buffer_size:
.word	0
prof_buffer_offset:
.word	0
prof_fmt_str:
.ascii	"%s,%u,%u\012\000"
.align	2

#define OS_EnterOS 0x16
#define OS_LeaveOS 0x7c
#define OS_Hardware 0x7a

//#define cortex

#ifdef cortex
#define read_timer(out) mrc	p15, 0, out, c9, c13, 0 @ counts up
#else
#define read_timer(out) mrc	p6, 0, out, c3, c1, 0 @ counts down!
#endif

.global	Prof_Init
.global	Prof_BeginFunc
.global	Prof_Begin
.global	Prof_EndFunc
.global	Prof_End
.global	Prof_Dump

@ force dynamic areas off (unixlib seems to be using them even though we don't have arcem$heap set?)
.global	__dynamic_no_da
__dynamic_no_da:

Prof_Init:
	mov	ip, sp
	stmfd	sp!, {v5-v6, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	a1, #4
	bl	malloc
	sub	v5, a1, #0x8000
	str	v5, prof_buffer_size
	bl	free
	mov	a1, v5
	bl	malloc
	str	a1, prof_buffer_ptr
	sub	a2, a1, #0x8000
	str	a2, prof_buffer_offset
	mov	a2, #0
	mov	a3, v5
	bl	memset
#ifdef cortex
	@ configure cycle count performance counter
	swi	OS_EnterOS
	mov	a1, #1
	mcr	p15, 0, a1, c9, c14, 0 @ enable user access to perf counters
	mvn	a1, #0
	mcr	p15, 0, a1, c9, c12, 2 @ disable counters
	mcr	p15, 0, a1, c9, c14, 2 @ disable interrupts
	mov	a1, #0x80000000
	mcr	p15, 0, a1, c9, c12, 1 @ enable cycle count
	mov	a1, #7
	mcr	p15, 0, a1, c9, c12, 0 @ enable & reset
	swi	OS_LeaveOS
#else
	@ No user-mode access to performance counters on XScale, so user timer
	@ 1 instead
	@ Configure it via HAL calls for simplicity
	mov	v5, #0
	mov	a1, #1
	mov	v6, #13
	swi	OS_Hardware @ get IRQ
	mov	v6, #2
	swi	OS_Hardware @ disable IRQ
	mov	a1, #1
	mvn	a2, #0
	mov	v6, #16
	swi	OS_Hardware @ set period to max, enables timer
#endif
	ldmea	fp, {v5-v6, fp, sp, pc}

Prof_BeginFunc:
	@ a1 = name-poked func ptr
	ldrb	a2, [a1, #-4]!
	sub	a1, a1, a2
Prof_Begin:
	@ a1 = word-aligned const string ptr, must be >= 8 bytes!
	ldr	a3, prof_buffer_offset
	read_timer(a2)
	ldr	a4, [a1, a3]!
#ifdef cortex
	sub	a4, a4, a2 @ sub start time, add finish time
#else
	add	a4, a4, a2 @ add start countdown, sub finish countdown
#endif
	str	a4, [a1]
	mov	pc, lr

Prof_EndFunc:
	@ a1 = name-poked func ptr
	ldrb	a2, [a1, #-4]!
	sub	a1, a1, a2
Prof_End:
	@ a1 = word-aligned const string ptr, must be >= 8 bytes!
	ldr	a3, prof_buffer_offset
	read_timer(a2)
	add	a1, a1, a3
	ldmia	a1, {a3,a4}
#ifdef cortex
	add	a3, a3, a2
#else
	sub	a3, a3, a2
#endif
	add	a4, a4, #1
	stmia	a1, {a3,a4}
	mov	pc, lr

Prof_Dump:
	mov	ip, sp
	stmfd	sp!, {v1-v3,fp,ip,lr,pc}
	sub	fp, ip, #4
	ldr	v1, prof_buffer_ptr
	ldr	v2, prof_buffer_size
	ldr	v3, prof_buffer_offset
	add	v2, v1, v2
	add	v3, v3, #8
.loop:
	ldr	a3, [v1], #4
	cmp	a3, #0
	beq	.skip
	ldr	a4, [v1], #4
	sub	a2, v1, v3
	adr	a1, prof_fmt_str
	bl	printf
.skip:
	cmp	v1, v2
	blo	.loop
	ldmea	fp, {v1-v3,fp,sp,pc}
