/*  armos.c -- ARMulator OS interface:  ARM6 Instruction Emulator.
    Copyright (C) 1994 Advanced RISC Machines Ltd.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* This file contains a model of Demon, ARM Ltd's Debug Monitor,
including all the SWI's required to support the C library. The code in
it is not really for the faint-hearted (especially the abort handling
code), but it is a complete example. Defining NOOS will disable all the
fun, and definign VAILDATE will define SWI 1 to enter SVC mode, and SWI
0x11 to halt the emulator. */

#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#ifdef __STDC__
#define unlink(s) remove(s)
#endif

#ifdef __sun__
#include <unistd.h>	/* For SEEK_SET etc */
#endif

#ifdef __riscos
extern int _fisatty(FILE *);
#define isatty_(f) _fisatty(f)
#else
#ifdef __ZTC__
#include <io.h>
#define isatty_(f) isatty((f)->_file)
#else
#ifdef macintosh
#include <ioctl.h>
#define isatty_(f) (~ioctl((f)->_file,FIOINTERACTIVE,NULL))
#else
#define isatty_(f) isatty(fileno(f))
#endif
#endif
#endif

#include "armdefs.h"
#include "armos.h"
#ifndef NOOS
#ifndef VALIDATE
/* #ifndef ASIM */
#include "armfpe.h"
/* #endif */
#endif
#endif

extern unsigned ARMul_OSInit(ARMul_State *state) ;
extern void ARMul_OSExit(ARMul_State *state) ;
extern ARMword ARMul_OSLastErrorP(ARMul_State *state) ;
extern ARMword ARMul_Debug(ARMul_State *state, ARMword pc, ARMword instr) ;

#define BUFFERSIZE 4096
#ifndef FOPEN_MAX
#define FOPEN_MAX 64
#endif
#define UNIQUETEMPS 256

/***************************************************************************\
*                          OS private Information                           *
\***************************************************************************/

struct OSblock {
   ARMword Time0 ;
   ARMword ErrorP ;
   ARMword ErrorNo ;
   FILE *FileTable[FOPEN_MAX] ;
   char FileFlags[FOPEN_MAX] ;
   char *tempnames[UNIQUETEMPS] ;
   } ;

#define NOOP 0
#define BINARY 1
#define READOP 2
#define WRITEOP 4

#ifdef macintosh
#define FIXCRLF(t,c) ((t & BINARY)?c:((c=='\n'||c=='\r')?(c ^ 7):c))
#else                   
#define FIXCRLF(t,c) c 
#endif

#if 0
static ARMword softvectorcode[] =
{   /* basic: swi tidyexception + event; mov pc, lr;
              ldmia r11,{r11,pc}; swi generateexception + event
     */
  0xef000090, 0xe1a0e00f, 0xe89b8800, 0xef000080, /*Reset*/
  0xef000091, 0xe1a0e00f, 0xe89b8800, 0xef000081, /*Undef*/
  0xef000092, 0xe1a0e00f, 0xe89b8800, 0xef000082, /*SWI  */
  0xef000093, 0xe1a0e00f, 0xe89b8800, 0xef000083, /*Prefetch abort*/
  0xef000094, 0xe1a0e00f, 0xe89b8800, 0xef000084, /*Data abort*/
  0xef000095, 0xe1a0e00f, 0xe89b8800, 0xef000085, /*Address exception*/
  0xef000096, 0xe1a0e00f, 0xe89b8800, 0xef000086, /*IRQ*/
  0xef000097, 0xe1a0e00f, 0xe89b8800, 0xef000087, /*FIQ*/
  0xef000098, 0xe1a0e00f, 0xe89b8800, 0xef000088, /*Error*/
  0xe1a0f00e /* default handler */
};
#endif

/***************************************************************************\
*            Time for the Operating System to initialise itself.            *
\***************************************************************************/

unsigned ARMul_OSInit(ARMul_State *state)
{
 return(TRUE) ;
}

void ARMul_OSExit(ARMul_State *state)
{
 free((char *)state->OSptr) ;
}


/***************************************************************************\
*                  Return the last Operating System Error.                  *
\***************************************************************************/

ARMword ARMul_OSLastErrorP(ARMul_State *state)
{
  return ((struct OSblock *)(void *)state->OSptr)->ErrorP;
}

