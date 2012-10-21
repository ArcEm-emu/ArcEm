/*  dagstandalone.c -- ARMulator RDP/RDI interface:  ARM6 Instruction Emulator.
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

/*********************************************************************/
/* Modified by Dave Gilbert (arcem@treblig.org) to be more           */
/* independent. Not very cleanly done - more of a hack than anything */
/*********************************************************************/

#include <sys/types.h>
#include <signal.h>

#ifdef LINUX
#include <linux/time.h>
#endif

#include "dagstandalone.h"
#include "armdefs.h"

#include "ArcemConfig.h"
#include "ControlPane.h"

/**************************************************************/
/* Signal handler that terminates excecution in the ARMulator */
/**************************************************************/
#ifndef WIN32
#ifndef AMIGA
static void dagstandalone_handlesignal(int sig) {
#ifdef DEBUG
  fprintf(stderr, "Terminate ARMulator - excecution\n");
#endif
#ifdef BENCHMARKEXIT
  printf("Emulated cycles = %ld\n", ARMul_Time);
  exit(0);
#endif
  if (sig != SIGUSR1) {
    fprintf(stderr,"Unsupported signal.\n");
    return;
  }
  exit(0); /* ??? */
}
#endif
#endif

static void InitFail(int exitcode, char const *which) {
  ControlPane_Error(exitcode,"%s interface failed to initialise. Exiting\n",
                            which);
}

/**
 * dagstandalone
 *
 * Called directly from main() and WinMain() this
 * is in effect the main function of the program.
 *
 *
 */
 void dagstandalone(void) {
#ifndef WIN32
#ifndef AMIGA
  struct sigaction action;
#endif
#endif
  ARMul_State *emu_state = NULL;

#ifndef WIN32
#ifndef AMIGA
  /* Setup a signal handler for SIGUSR1 */
  action.sa_handler = dagstandalone_handlesignal;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  
  sigaction(SIGUSR1, &action, (struct sigaction *) 0);
#endif
#endif

  ARMul_EmulateInit();
  emu_state = ARMul_NewState();
  ARMul_Reset(emu_state);
  if (ARMul_MemoryInit(emu_state) == FALSE)
    InitFail(1, "Memory");
  if (ARMul_CoProInit(emu_state) == FALSE)
    InitFail(2, "Co-Processor");
  ARMul_Reset(emu_state);

  /* Excecute */
  ARMul_DoProg(emu_state);
  emu_state->Reg[15] -= 8; /* undo the pipeline (bogus?) */

  /* Close and Finalise */
  ARMul_CoProExit(emu_state);
  ARMul_MemoryExit(emu_state);
}
