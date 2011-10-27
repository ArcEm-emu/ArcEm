@ 
@ riscos-single/soundbuf.s
@ 
@ (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>
@ 
@ Basic SharedSound sound driver for ArcEm, based in part on my SharedSound
@ tutorial from http://www.iconbar.com/forums/viewthread.php?threadid=10778
@
@ This code handles filling the SharedSound buffer from the contents of the
@ buffer in ArcEm's RISC OS frontend (riscos-single/sound.c).
@
@ At the moment only basic sample rate conversion is applied - no linear
@ interpolation or anything like that.
@

.set X_Bit, 0x20000
.set XSharedSound_RemoveHandler, 0x04B441 + X_Bit

.text

.global buffer_fill
.global error_handler

routine_params:
	.word sound_buffer	@ R0
	.word sound_buffer_in	@ R3
buffer_out_ptr:
	.word sound_buffer_out	@ R4
	.word sound_rate	@ R5
	.word sound_buff_mask	@ R6
frac_step:
	.word 0 @ R7, current fractional step

sound_handler_id_ptr:
	.word sound_handler_id

buffer_fill:
	@ R0 = our parameter (unused)
	@ R1 = buffer base
	@ R2 = buffer end
	@ R3 = flags (ignored)
	@ R4 = Sample frequency (ignored)
	@ R5 = Sample period (ignored)
	@ R6 = Fractional step (ignored)
	@ R7 = LR volume (ignored)
	@ R8 = Pointer to fill code (ignored)

#if 0
	@ First, check if we need to clear the buffer or add to it
	TST R3,#1
	ORR R3,R3,#1
	STMFD R13!,{R0-R10,R14}
	BNE nozeros
	MOV R9,#0
	MOV R10,R1
zeroloop:
	STR R9,[R10],#4
	CMP R10,R2
	BLT zeroloop
nozeros:
	@ Get our params
	ADR R0,routine_params
	LDMIA R0,{R0,R3-R7}
	LDR R3,[R3]
	LDR R4,[R4]
	LDR R5,[R5]
	LDR R6,[R6]
	@ Loop and fill
	SUBS R3,R3,R4 @ Number of samples available
	BLE endloop @ No more input data
mainloop:
	AND R8,R4,R6 @ Pos to read from
	LDR R10,[R1] @ dest data
	LDR R9,[R0,R8,LSL #1] @ Get input pair
	ADD R10,R10,R9 @ Mix in (no overflow checks!)
	STR R10,[R1],#4
	ADD R7,R7,R5 @ Increment fraction accumulator
	MOV R10,R7,LSR #24 @ Get any whole movements
	BIC R7,R7,#0xFF000000 @ Get rid of whole part
	ADD R4,R4,R10,LSL #1 @ Increment out pos
	@ Check for buffer ends
	SUBS R3,R3,R10,LSL #1
	BLE endloop
	CMP R1,R2
	BLT mainloop
endloop:
	@ Store back updated values
	LDR R3,buffer_out_ptr
	STR R7,frac_step
	STR R4,[R3]
	LDMFD R13!,{R0-R10,PC}
#else
	@ New version for the new sound mixer
	@ sound_rate just acts as a pause flag, since the data should be at the correct rate already
	TST R3,#1
	ORR R3,R3,#1
	STMFD R13!,{R0-R8,R14}
	MOVEQ R14,#0 @ R14 becomes mix flag
	@ Get our params
	ADR R0,routine_params
	LDMIA R0,{R0,R3-R6}
	LDR R3,[R3]
	LDR R4,[R4]
	LDR R5,[R5]
	LDR R6,[R6]
	CMP R3,#0
	BEQ nodata
	SUBS R3,R3,R4 @ Number of samples available
	BLE nodata @ No more input data
	CMP R14,#0
	BEQ copyloop
mixloop:
	AND R8,R4,R6 @ Pos to read from
	LDR R14,[R1] @ dest data
	ADD R4,R4,#4 @ Increment out pos
	LDR R7,[R0,R8,LSL #1] @ Get input pair
	SUBS R3,R3,#2
	ADD R14,R14,R7 @ Mix in (no overflow checks!)
	STR R14,[R1],#2
	@ Check for buffer ends
	BLE mix_done
	CMP R1,R2
	BLT mixloop
mix_done:
	@ Store back updated values
	LDR R3,buffer_out_ptr
	STR R4,[R3]
	LDMFD R13!,{R0-R8,PC}

copyloop:
	AND R8,R4,R6 @ Pos to read from
	LDR R9,[R0,R8,LSL #1] @ Get input pair
	ADD R4,R4,#2 @ Increment out pos
	STR R9,[R1],#4
	@ Check for buffer ends
	SUBS R3,R3,#2
	BLE zeroloop
	CMP R1,R2
	BLT copyloop
	@ Store back updated values
	LDR R3,buffer_out_ptr
	STR R4,[R3]
	LDMFD R13!,{R0-R8,PC}

nodata:
	CMP R14,#0
	LDMNEFD R13!,{R0-R8,PC} @ No source data, and mixing in, therefore no work to do
	@ Else must fill buffer with zeros
	MOV R3,#0 @ Note: Alternate entry points should have R3 as zero already
zeroloop:
	CMP R1,R2
	STRLT R3,[R1],#4
	BLT zeroloop
	@ Store back updated values
	LDR R3,buffer_out_ptr
	STR R4,[R3]
	LDMFD R13!,{R0-R8,PC}
#endif

error_handler:
	STMFD R13!,{R0-R2,R14}
	MRS R2,CPSR
	@ Check if sound handler installed, remove
	LDR R1,sound_handler_id_ptr
	LDR R0,[R1]
	CMP R0,#0
	SWINE XSharedSound_RemoveHandler
	MOV R0,#0
	STR R0,[R1]
	MSR CPSR_f,R2
	LDMFD R13!,{R0-R2,PC}
