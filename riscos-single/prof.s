@
@ prof.s
@
@ (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>
@
@ Part of Arcem released under the GNU GPL, see file COPYING
@ for details.
@
@ Very nasty CPU profiling code for RISC OS hosts running on Iyonix or
@ Cortex-A8 machines. This code relies upon the following:
@
@ * Function name poking (-mpoke-function-names) being enabled
@ * 'cortex' #define being set correctly for whether IOP321 or Cortex-A8 code
@   should be used
@ * On IOP321, HAL timer 1 must be free. This also means the results will be in
@   200MHz clock ticks instead of CPU cycles.
@ * On Cortex-A8, the cycle count performance counter must be free
@ * C heap allocations must come from application space, not a dynamic area (a
@   simple malloc is used to estimate how large the program is by looking at the
@   allocation address). This requirement is currently enforced by
@   riscos-single/DispKbd.c
@

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
.global	Prof_Reset

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

Prof_Reset:
	ldr	a1, prof_buffer_ptr
	mov	a2, #0
	ldr	a3, prof_buffer_size
	b	memset

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
	stmfd	sp!, {a1,v1-v3,fp,ip,lr,pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	ldr	v1, prof_buffer_ptr
	ldr	v2, prof_buffer_size
	ldr	v3, prof_buffer_offset
	add	v2, v1, v2
	add	v3, v3, #8
.loop:
	ldr	a4, [v1], #4
	cmp	a4, #0
	beq	.skip
	ldr	ip, [v1], #4
	str	ip, [sp]
	sub	a3, v1, v3
	adr	a2, prof_fmt_str
	ldr	a1, [sp, #4]
	bl	fprintf
.skip:
	cmp	v1, v2
	blo	.loop
	ldmea	fp, {v1-v3,fp,sp,pc}
