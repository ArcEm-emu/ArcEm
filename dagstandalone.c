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
#include "arch/ArcemConfig.h"

static ArcemConfig hArcemConfig;

/**
 * dagstandalone
 *
 * Called directly from main() and WinMain() this
 * is in effect the main function of the program.
 *
 *
 */
int dagstandalone(int argc, char *argv[]) {
  ArcemConfig_Result result = Result_Continue;
  ARMul_State *state = NULL;
  int exit_code;

  /* Setup the default values for the config system */
  if (result == Result_Continue)
    result = ArcemConfig_SetupDefaults(&hArcemConfig);

  /* Parse the config file to overrule the defaults */
  if (result == Result_Continue)
    result = ArcemConfig_ParseConfigFile(&hArcemConfig);

  /* Parse any commandline arguments given to the program
     to overrule the defaults */
  if (result == Result_Continue)
    result = ArcemConfig_ParseCommandLine(&hArcemConfig, argc, argv);

  if (result != Result_Continue) {
    ArcemConfig_Free(&hArcemConfig);
    return (result == Result_Success ? EXIT_SUCCESS : EXIT_FAILURE);
  }

  /* Initialise */
  state = ARMul_NewState(&hArcemConfig);
  if (!state) {
    ArcemConfig_Free(&hArcemConfig);
    return EXIT_FAILURE;
  }

  /* Execute */
  exit_code = ARMul_DoProg(state);

  /* Finalise */
  ARMul_FreeState(state);
  ArcemConfig_Free(&hArcemConfig);

  return exit_code;
}
