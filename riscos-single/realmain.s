@
@ riscos-single/realmain.s
@
@ (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>
@
@ Part of Arcem released under the GNU GPL, see file COPYING
@ for details.
@
@ Simple wrapper for main() that creates one large stack frame for us to use.
@ This allows us to skip performing stack extension checks in the main code.
@

@ 256K should be far more than enough
#define STACK_SIZE (256*1024)

.text

.global	main

main:
	mov	ip, sp
	stmfd	sp!, {fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	ip, sp, #STACK_SIZE
	bl	__rt_stkovf_split_big
	bl	fakemain
	ldmea	fp, {fp, sp, pc}

