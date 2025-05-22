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

#include <stdlib.h>

#include "dagstandalone.h"
#include "armdefs.h"
#include "armcopro.h"
#include "dbugsys.h"

#include "ArcemConfig.h"
#include "ControlPane.h"

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
 void dagstandalone(ArcemConfig *pConfig) {
  ARMul_State *emu_state = NULL;

  ARMul_EmulateInit();
  emu_state = ARMul_NewState(pConfig);
  ARMul_Reset(emu_state);
  if (!ARMul_MemoryInit(emu_state))
    InitFail(1, "Memory");
#ifdef ARMUL_COPRO_SUPPORT
  if (!ARMul_CoProInit(emu_state))
    InitFail(2, "Co-Processor");
#else
  if (emu_state->HasCP15)
    ControlPane_Error(2, "ARM3 support is not available in this build of ArcEm. Exiting\n");
#endif
  ARMul_Reset(emu_state);

  /* Excecute */
  ARMul_DoProg(emu_state);
  emu_state->Reg[15] -= 8; /* undo the pipeline (bogus?) */

  /* Close and Finalise */
#ifdef ARMUL_COPRO_SUPPORT
  ARMul_CoProExit(emu_state);
#endif
  ARMul_MemoryExit(emu_state);
}
