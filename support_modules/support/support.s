@ ArcEmSupport - A simple module that currently just exports ArcEm's SWI names
@ into RISC OS, but the implementations do nothing (as they're trapped by
@ ArcEm itself, so here gets called later by RISC OS, who doesn't realise.)
@ This module can also be used in the future for putting other support bits
@ and bobs in.
@
@ Created by Rob Kendrick, 19 Oct 2005.
@
@ $Id$

	ARCEM_SWI_BASE = 0x56ac0

	ArcEm_Shutdown = ARCEM_SWI_BASE + 0



	.global _start

_start:

	.org	0

	.int	0					@ start addr
	.int	0                               	@ init addr
	.int	0					@ final addr
	.int	0                                       @ service addr
	.int	RM_Title				@ title addr
	.int	RM_HelpString				@ help addr
	.int	RM_HC_Table				@ commands addr
	.int	ARCEM_SWI_BASE				@ swi base
	.int	RM_SWI					@ swi handler
	.int	RM_SWI_Table				@ swi table

RM_Title:
	.string	"ArcEmSupport"

RM_HelpString:
	.string	"ArcEm Support\t0.01 (19 Oct 2005) \xa9 ArcEm Developers"
	.align

RM_HC_Table:
	.string	"ArcEm_Shutdown"
	.align
	.int	CMD_Shutdown
	.int	0
	.int	0
	.int	CMD_Shutdown_H
	.int	0

CMD_Shutdown_H:
	.string	"Powers off the emulator, and returns to the host OS."
	.align

RM_SWI_Table:
	.string	"ArcEm"
	.string	"Shutdown"
	.string	"HostFS"
	.string	"Debug"
	.string	"IDEFS"
        .byte	0
	.align

RM_SWI:
        MOVS    PC, R14                                 @ do nothing

CMD_Shutdown:
	SWI	ArcEm_Shutdown
	MOVS	PC, R14					@ incase of non-arcem
