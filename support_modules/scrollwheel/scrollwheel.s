@Swis
XOS_Module = 0x2001e
XOS_Byte = 0x20006
XOS_Claim = 0x2001f
XOS_Release = 0x20020
XWimp_Initialise = 0x600c0
XWimp_CloseDown = 0x600dd
XWimp_GetWindowState = 0x600cb
XWimp_GetPointerInfo = 0x600cf
XWimp_SendMessage = 0x600e7
Wimp_ReportError = 0x400df
XWimp_Poll = 0x600c7
XOS_Exit = 0x20011
OS_Exit = 0x11

@ Data
workSize = 2048
bufferSize = 10
defaultScrollStep = 64

@ Workspace addresses
taskHandle = 0
scrollStep = 4
pollBlock = 8
pollBlockEnd = 264
bufferWrite = 268
bufferRead = 272
bufferEnd = 276
buffer = 284
stackStart = 1024
stackEnd = 2048

@ Misc
VBIT = 1 << 28

.global _start
_start:
base:
.int start
.int init
.int final
.int service
.int title
.int helpstr
.int commands
.int 0
.int 0
.int 0
.int 0

init:
 ORR LR,LR,#VBIT
 STMFD R13!,{R7-R11,LR}

 MOV R0,#6
 MOV R3,#workSize
 SWI XOS_Module
 LDMVSFD R13!,{R7-R11,PC}
 STR R2,[R12]
 LDR R12,[R12]

 MOV R0,#0
 STR R0,[R12,#taskHandle]

@ set scroll step to default
 MOV R0,#defaultScrollStep
 STR R0,[R12,#scrollStep]

@ initialise circular buffer
 ADD R0,R12,#buffer
 STR R0,[R12,#bufferRead]
 STR R0,[R12,#bufferWrite]
 ADD R0,R0,#bufferSize
 STR R0,[R12,#bufferEnd]

@ claim event vector
 MOV R0,#0x10 @ EventV
 ADR R1,event_handler
 MOV R2,R12
 SWI XOS_Claim

@ enable keypress/release event
 MOV R0,#14
 MOV R1,#11 @ event number
 SWI XOS_Byte


 LDMFD R13!,{R7-R11,LR}
 BICS PC,LR,#VBIT

final:
 STMFD R13!,{R14}

 LDR R12,[R12]

@ Close wimp task if active
 LDR R0,[R12,#taskHandle]
 TEQ R0,#0
 LDRGT R1,TASK
 SWIGT XWimp_CloseDown
 MOV R0,#0
 STR R0,[R12,#taskHandle] @ just for neatness

@ undo enabling of event
 MOV R0,#13
 MOV R1,#11
 SWI XOS_Byte

@ release event vector
 MOV R0,#0x10
 ADR R1,event_handler
 MOV R2,R12
 SWI XOS_Release

@ Free memory
 MOV R2,R12
 MOV R0,#7
 SWI XOS_Module @ free temp buffer

 LDMFD R13!,{PC}

event_handler:
 TEQ R0,#11 @ are we interested?
 MOVNES PC,R14 @ no, get out as soon as possible:
 AND R3,R2,#0xF0
 CMP R3,#0x70
 MOVNES PC,R14 @ if not mouse event, we are also not interested
@ store button in circular buffer
 LDR R0,[R12,#bufferWrite]
 AND R3,R2,#0x0F @ get low byte (button code)
 STRB R3,[R0],#1
 LDR R1,[R12,#bufferEnd]
 TEQ R1,R0 @ has the write pointer reached the end of the buffer?
 ADDEQ R0,R12,#buffer @ if so, set write pointer to start of buffer
 STR R0,[R12,#bufferWrite]
 MOVS PC,R14

service:
 LDR R12,[R12]
 TEQ R1,#0x49
 BEQ service_startWimp
 TEQ R1,#0x27
 BEQ service_reset
 TEQ R1,#0x4A
 BEQ service_startedWimp
 MOV PC,R14

service_reset:
 STMFD R13!,{LR}
 MOV R14,#0
 STR R14,[R12,#taskHandle]

 @ Reclaim EventV and enable event 11
 MOV R0,#0x10 @ EventV
 ADR R1,event_handler
 MOV R2,R12
 SWI XOS_Claim
 MOV R0,#14
 MOV R1,#11 @ event number
 SWI XOS_Byte


 LDMFD R13!,{PC}

service_startedWimp:
 STMFD R13!,{LR}
 LDR R14,[R12,#taskHandle]
 CMN LR,#1
 MOVEQ LR,#0
 STREQ LR,[R12,#taskHandle]
 LDMFD R13!,{PC}

service_startWimp:
 STMFD R13!,{LR}
 LDR R14,[R12,#taskHandle]
 TEQ R14,#0
 MVNEQ R14,#0 @ = -1 prevent us trying again if there is an error
 STREQ R14,[R12,#taskHandle]
 ADREQ R0,start_command
 MOVEQ R1,#0
 LDMFD R13!,{PC}

title:
.asciz "ArcEmScrollWheel"
.align

helpstr:
.ascii "ArcEm Scroll Wheel"
.byte 9
.asciz "0.01 (19 Jan 2005)"
.align

start_command:
.asciz "Desktop_ArcEmScroll"
.align

commands:
.asciz "Desktop_ArcEmScroll"
.align

.int command_ArcEmScroll
.int 0
.int 0
.int 0

command_ArcEmScroll:
 STMFD R13!,{R14}
@ Run the module task
 MOV R2,R0 @ command tail
 ADR R1,title
 MOV R0,#2
 SWI XOS_Module
 LDMFD R13!,{PC}

TASK:
.ascii "TASK"

taskTitle:
.asciz "ArcEm Scroll Wheel Support"
.align

wimp_version:
.int 310

start:
 LDR R12,[R12]
 LDR R0,[R12,#taskHandle]
 TEQ R0,#0 @ are we active?
 LDRGT R1,TASK @ yes, close ourselves down first
 SWIGT XWimp_CloseDown
 MOVGT R0,#0
 STRGT R0,[R12,#taskHandle]

 LDR R0,wimp_version 
 LDR R1,TASK
 ADR R2,taskTitle
 SWI XWimp_Initialise
 BVS error_close_down
 STR R1,[R12,#taskHandle]

ADD SP,R12,#stackEnd



poll_loop:
 ADD SP,R12,#stackEnd
 MOV R0,#0 @ do not mask out null reason code
 ADD R1,R12,#pollBlock @ pointer to poll block
 SWI XWimp_Poll
 BVS error_close_down

 TEQ R0,#0
 BEQ null_reason
 TEQ R0,#17
 BEQ user_message

 B poll_loop

null_reason:
@ our main interest, here we look to see if there are scrollwheel
@ events in our module's buffer
 LDR R0,[R12,#bufferRead]
 LDR R1,[R12,#bufferWrite]
 LDR R2,[R12,#bufferEnd]


 TEQ R0,R1 @ if the read and write are in the same place, there is nothing to do
 BEQ poll_loop

 LDRB R3,[R0],#1 @ otherwise get a byte
 CMP R0,R2 @ reached end of buffer?
 ADDEQ R0,R12,#buffer @ if so reset read to start of buffer
 STR R0,[R12,#bufferRead]
@ now we can process the scrollwheel event in R3:
 TEQ R3,#3
 TEQNE R3,#4
 BNE poll_loop @ ignore unrecognised codes

 ADD R1,R12,#pollBlock @ give it a block to write in
 SWI XWimp_GetPointerInfo
 BVS report_error

 ADD R1,R1,#12 @ point to block starting with window handle
 SWI XWimp_GetWindowState
 BVS report_error

 LDR R0,[R1,#32] @ get window flags
@ now we check if the window has a vertical scroll bar
 TST R0,#1<<31 @ new style flags?
 BEQ scroll_check_old @ no, check old style flag
 TST R0,#1<<28 @ yes, look in bit 28
 BEQ poll_loop @ no scroll bar, return
 B scroll_check_done
scroll_check_old :
 TST R0,#1<<2  @ no, use old bit 2
 BEQ poll_loop @ no scroll bar, return

scroll_check_done:
 
 TST R0,#1<<8    @ is scroll_request flag set?
 BNE send_scroll_request

@ otherwise just need to do an open_window_request
 LDR R0,[R1,#24] @ get y scroll offset
 LDR R4,[R12,#scrollStep]
 TEQ R3,#3
 BEQ scroll_up
scroll_down:
 SUB R0,R0,R4
 B do_scroll
scroll_up:
 ADD R0,R0,R4 @ alter it appropriately
do_scroll:
 STR R0,[R1,#24]

 LDR R2,[R1,#0]
 MOV R0,#2      @ now send an open_window_request
 SWI XWimp_SendMessage
 BVS report_error

 B poll_loop

send_scroll_request:
 TEQ R3,#3
 MOVEQ R4,#1   @ +1
 MVNNE R4,#0   @ -1
 STR R4,[R1,#36]

 MOV R0,#10
 LDR R2,[R1,#0]
 SWI XWimp_SendMessage
 BVS report_error
 B poll_loop

user_message:
 LDR R0,[R1,#12]
 TEQ R0,#0          @ Message_Quit?
 BEQ close_down
 B poll_loop


report_error:
SWI Wimp_ReportError
B poll_loop

error_close_down:
 SWI Wimp_ReportError
close_down:
 LDR R0,[R12,#taskHandle]
 LDR R1,TASK
 SWI XWimp_CloseDown
 MOV R0,#0
 STR R0,[R12,#taskHandle]
 SWI OS_Exit
